"""Bailian message ASR. Audio uploads during recording; publish final text once."""
import asyncio
import json
import time
import uuid
from urllib.parse import urlsplit

MODEL='qwen-audio-3.1-asr-flash-message'
DEFAULT_ENDPOINT='wss://dashscope.aliyuncs.com/api-ws/v1/inference'

class ASRError(Exception): pass

class Recognition:
    def __init__(self,key,endpoint):
        parts=urlsplit(endpoint)
        if not key or parts.scheme!='wss' or not parts.hostname or parts.path!='/api-ws/v1/inference' or parts.query:
            raise ASRError('请检查百炼 Key 和服务地址')
        self.key=key;self.endpoint=endpoint;self.id=uuid.uuid4().hex
        self.queue=asyncio.Queue();self.queued=0;self.ended=False
        self.finished=asyncio.Event();self.sentences={}
        self.finalized=asyncio.Event();self.final_text='';self.final_error=None
        self.started_at=0;self.socket_at=0;self.ready_at=0;self.end_at=0;self.finish_at=0;self.final_at=0;self.closed_at=0;self.queued_at_end=0
    def append(self,data):
        if self.ended or len(data)%2: raise ASRError('录音格式错误')
        if self.queued+len(data)>96000: raise ASRError('网络上传过慢，本次输入已停止')
        self.queued+=len(data);self.queue.put_nowait(data)
    def end(self):
        if not self.ended:
            self.end_at=time.monotonic();self.queued_at_end=self.queued
            self.ended=True;self.queue.put_nowait(None)
    async def wait_final(self):
        # The complete, task-finished result is usable before the network close
        # handshake. run() still owns and closes that connection in the background.
        await self.finalized.wait()
        if self.final_error is not None:raise self.final_error
        return self.final_text
    def metrics(self):
        def delta(end,start):return max(0,round((end-start)*1000)) if end and start else 0
        return {'connection_ms':delta(self.ready_at,self.started_at),
                'socket_ms':delta(self.socket_at,self.started_at),
                'task_start_ms':delta(self.ready_at,self.socket_at),
                'queued_at_end_ms':round(self.queued_at_end/32),
                'upload_tail_ms':delta(self.finish_at,self.end_at),
                'server_final_ms':delta(self.final_at,self.finish_at),
                'asr_final_ms':delta(self.final_at,self.end_at),
                'cleanup_ms':delta(self.closed_at,self.final_at)}
    def header(self,action): return {'action':action,'task_id':self.id,'streaming':'duplex'}
    async def upload(self,ws):
        while True:
            data=await self.queue.get()
            if data is None:
                self.finished.set()
                self.finish_at=time.monotonic()
                await ws.send(json.dumps({'header':self.header('finish-task'),'payload':{'input':{}}}))
                return
            await ws.send(data);self.queued-=len(data)
    async def run(self):
        self.started_at=time.monotonic()
        try:return await self._run()
        except asyncio.CancelledError:
            if not self.finalized.is_set():
                self.final_error=ASRError('本次识别已取消');self.finalized.set()
            raise
        except Exception as exc:
            if not self.finalized.is_set():self.final_error=exc;self.finalized.set()
            raise
        finally:self.closed_at=time.monotonic()
    async def _run(self):
        from websockets.asyncio.client import connect
        try:
            async with connect(self.endpoint,additional_headers={'Authorization':'Bearer '+self.key},
                    open_timeout=10,close_timeout=2,max_size=65536,max_queue=16) as ws:
                self.socket_at=time.monotonic()
                await ws.send(json.dumps({'header':self.header('run-task'),'payload':{
                    'task_group':'audio','task':'asr','function':'recognition','model':MODEL,'input':{},
                    'parameters':{'format':'pcm','sample_rate':16000,'intermediate_result_enabled':False,'vad_model':'near_meeting_16k'}}}))
                raw=await asyncio.wait_for(ws.recv(),10)
                self.event(json.loads(raw),'task-started')
                self.ready_at=time.monotonic()
                sender=asyncio.create_task(self.upload(ws))
                receiver=asyncio.create_task(self.receive(ws))
                async def deadline():
                    await self.finished.wait()
                    await asyncio.sleep(15)
                    raise ASRError('等待最终识别结果超时')
                timeout=asyncio.create_task(deadline())
                try:
                    done,_=await asyncio.wait([sender,receiver,timeout],return_when=asyncio.FIRST_COMPLETED)
                    if sender in done:
                        sender.result()
                        done,_=await asyncio.wait([receiver,timeout],return_when=asyncio.FIRST_COMPLETED)
                    for task in done: task.result()
                    return receiver.result()
                finally:
                    for task in (sender,receiver,timeout):task.cancel()
                    await asyncio.gather(sender,receiver,timeout,return_exceptions=True)
        except asyncio.CancelledError:raise
        except ASRError:raise
        except Exception as exc:
            # Never include upstream exception text: it may contain auth/audio.
            raise ASRError('百炼连接失败，请检查网络、Key、地域和模型') from None
    def event(self,v,expected=None):
        h=v.get('header',{})
        if h.get('task_id')!=self.id: raise ASRError('识别任务身份不匹配')
        event=h.get('event')
        if event=='task-failed':raise ASRError('百炼识别失败，请检查 Key、地域和模型')
        if expected and event!=expected:raise ASRError('识别任务启动失败')
        return event
    async def receive(self,ws):
        async for raw in ws:
            v=json.loads(raw);event=self.event(v)
            if event=='result-generated':
                s=v.get('payload',{}).get('output',{}).get('sentence',{})
                i=s.get('sentence_id');text=s.get('text','')
                if s.get('heartbeat') is not True and s.get('sentence_end') is True:
                    if type(i) is not int or not 0<i<=4096 or not isinstance(text,str):raise ASRError('识别结果格式错误')
                    self.sentences[i]=text
                    if sum(len(t) for t in self.sentences.values())>24000:raise ASRError('识别结果过长')
            elif event=='task-finished':
                if not self.finished.is_set():raise ASRError('识别结果提前结束')
                text=''.join(self.sentences[k] for k in sorted(self.sentences))
                if len(text.encode())>16000:raise ASRError('识别结果过长')
                self.final_at=time.monotonic();self.final_text=text;self.finalized.set()
                return text
        raise ASRError('识别连接意外断开')
