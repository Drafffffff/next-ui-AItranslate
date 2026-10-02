#!/usr/bin/env python3
"""Single-instance, BLE-central Brick Mic receiver; no GUI and no audio files."""
import argparse
import asyncio
import base64
import contextlib
import fcntl
import json
import logging
import os
from pathlib import Path
import re
import signal
import socket
import time
import uuid

from asr import Recognition,ASRError,DEFAULT_ENDPOINT,MODEL
from input import Keyboard
from protocol import SERVICE,AUDIO,CONTROL,Assembler,decode

ROOT=Path.home()/'.config/brick-mic'
LOG=logging.getLogger('brick-mic')

async def power_on_bluetooth():
    # Use BlueZ's normal power setting. Don't reset controllers, unblock rfkill,
    # remove pairings, or attempt to override denied system/user permissions.
    process=None
    try:
        process=await asyncio.create_subprocess_exec('/usr/bin/bluetoothctl','--timeout','5','power','on',
            stdout=asyncio.subprocess.DEVNULL,stderr=asyncio.subprocess.DEVNULL)
        return await asyncio.wait_for(process.wait(),7)==0
    except (OSError,asyncio.TimeoutError):return False
    finally:
        if process is not None and process.returncode is None:
            process.kill();await process.wait()

def config():
    ROOT.mkdir(parents=True,exist_ok=True,mode=0o700)
    path=ROOT/'receiver.json'
    if not path.exists():
        value={'id':str(uuid.uuid4()),'name':socket.gethostname(),'endpoint':DEFAULT_ENDPOINT}
        tmp=path.with_suffix('.new');tmp.write_text(json.dumps(value));tmp.chmod(0o600);tmp.replace(path)
    value=json.loads(path.read_text())
    if not re.fullmatch(r'[A-Za-z0-9_-]{8,64}',value.get('id','')) or not 0<len(value.get('name','').encode())<=96 or any(ord(c)<32 for c in value['name']):raise ValueError('invalid receiver identity')
    key=os.environ.get('DASHSCOPE_API_KEY','')
    credentials=ROOT/'credentials.env'
    if credentials.exists():
        if credentials.stat().st_mode&0o077:raise ValueError('credentials.env must be private (chmod 600)')
        for line in credentials.read_text().splitlines():
            if line.startswith('DASHSCOPE_API_KEY='):key=line.split('=',1)[1].strip()
    if not re.fullmatch(r'sk-[A-Za-z0-9_-]{10,}',key):raise ValueError('configure DASHSCOPE_API_KEY in credentials.env')
    value['key']=key;return value

class Receiver:
    def __init__(self,settings):
        self.config=settings;self.client=None;self.token='';self.assembler=Assembler();self.keyboard=Keyboard()
        self.messages=asyncio.Queue(maxsize=256);self.writes=asyncio.Lock();self.control_id=0;self.state_writes=asyncio.Lock()
        self.sid=0;self.frames=0;self.samples=0;self.buffer=bytearray();self.asr=None;self.recognition=None
        self.destination=None;self.release=0;self.connected=False;self.last_audio=0;self.overflow=False
        self.finalizers=set()
        self.recognizers=set();self.began=0;self.release_delay=0
        self.status={'connection':'starting','model':MODEL,'recording':False,'processing':False,'completed':0,'inserted':False,'controls_received':0,'controls_posted':0,'controls_rejected':0,'bluetooth_recoveries':0}
    def cancel(self):
        if self.recognition:self.recognition.cancel()
        self.recognition=None;self.asr=None;self.buffer.clear();self.sid=0;self.status['recording']=False;self.status['processing']=False
    async def write(self,value):
        data=json.dumps(value,ensure_ascii=False,separators=(',',':')).encode()
        if len(data)>512:raise ValueError('oversize control message')
        async with self.writes:
            if self.client is None or not self.client.is_connected:raise ConnectionError('disconnected')
            await self.client.write_gatt_char(CONTROL,data,response=True)
    async def control(self,hint=''):
        async with self.state_writes:
            self.control_id=(self.control_id+1)&0xffffffff
            data=json.dumps({'op':'state','state':{'mode':'ordinary','remoteAllowed':True,'codexAvailable':False,
                'target':self.config['name'],'tasks':[],'unread':0,'controlHint':hint,'alerts':[]}},ensure_ascii=False,separators=(',',':')).encode()
            total=(len(data)+47)//48
            for part in range(total):await self.write({'op':'c','token':self.token,'id':self.control_id,'p':part,'n':total,'d':base64.b64encode(data[part*48:(part+1)*48]).decode()})
    async def result(self,sid,text):
        chunks=[];part=''
        for c in text:
            if len((part+c).encode())>90:chunks.append(part);part=''
            part+=c
        chunks.append(part)
        for i,part in enumerate(chunks):await self.write({'op':'result' if i==0 else 'append','session':sid,'text':part,'final':i==len(chunks)-1})
    async def finish(self,sid):
        try:
            recognizer=self.asr
            text=await recognizer.wait_final() if recognizer else await self.recognition
            if sid!=self.sid or not self.connected:return
            input_began=time.monotonic()
            try:inserted=await self.keyboard.insert(text,self.destination) if text else True
            except Exception:inserted=False
            frames=self.frames;final_ms=round((time.monotonic()-self.release)*1000)
            timing=recognizer.metrics() if recognizer else {}
            timing.update(input_ms=round((time.monotonic()-input_began)*1000),end_to_input_ms=final_ms,
                release_to_end_ms=self.release_delay,receive_extra_ms=max(0,round((self.release-self.began-self.samples/16000)*1000)) if self.began else 0)
            # Close the old recording epoch before publishing READY. A new A
            # press after the final fragment must not be cancelled by this turn.
            # Keep the completed recognizer alive for its bounded socket cleanup.
            self.recognition=None
            self.cancel()
            published=time.monotonic()
            await self.result(sid,text)
            timing['result_publish_ms']=round((time.monotonic()-published)*1000)
            await self.control('已填入' if inserted else '输入目标已变化 · 文字保留')
            self.status.update(completed=self.status['completed']+1,inserted=inserted,final_ms=final_ms,timing=timing)
            LOG.info('recognition completed frames=%d final_ms=%d inserted=%s timing=%s',frames,self.status['final_ms'],inserted,json.dumps(timing,separators=(',',':')))
        except asyncio.CancelledError:raise
        except Exception:
            if self.sid==sid:self.cancel()
            if self.connected:
                with contextlib.suppress(Exception):await self.write({'op':'error','session':sid,'text':'语音识别或文字填入失败，请检查电脑服务'})
    async def failure(self,text):
        sid=self.sid;self.cancel()
        if self.connected:
            with contextlib.suppress(Exception):await self.write({'op':'error','session':sid,'text':text})
    def notification(self,_char,data):
        try:self.messages.put_nowait(bytes(data))
        except asyncio.QueueFull:
            self.status['connection']='audio-overflow';self.overflow=True
    def recognition_done(self,task):
        self.recognizers.discard(task)
        if not task.cancelled():task.exception() # final/error is delivered by wait_final()
    async def consume(self):
        while True:
            raw=await asyncio.wait_for(self.messages.get(),2)
            if self.overflow:raise ValueError('audio receive queue overflow')
            msg=self.assembler.append(raw)
            if msg is None:continue
            kind,sid,frame,data=msg
            if kind==1:
                if self.sid:await self.failure('上次输入尚未结束');continue
                self.sid=sid;self.frames=0;self.samples=0;self.destination=self.keyboard.focus()
                self.last_audio=time.monotonic();self.began=self.last_audio;self.release_delay=0;self.status['recording']=True
            elif kind==2:
                if not self.sid or sid!=self.sid or frame!=self.frames:raise ValueError('audio frame out of order')
                pcm=decode(data);self.frames+=1;self.samples+=len(pcm)//2;self.last_audio=time.monotonic()
                if self.frames%10==0:await self.write({'op':'ack','session':sid,'frame':self.frames})
                if self.asr:self.asr.append(pcm)
                else:
                    self.buffer.extend(pcm)
                    if self.samples>=8000:
                        self.asr=Recognition(self.config['key'],self.config.get('endpoint',DEFAULT_ENDPOINT))
                        self.asr.append(bytes(self.buffer));self.buffer.clear()
                        self.recognition=asyncio.create_task(self.asr.run())
                        self.recognizers.add(self.recognition);self.recognition.add_done_callback(self.recognition_done)
                if self.samples>16000*60:raise ValueError('recording exceeded limit')
            elif kind==3:
                end=json.loads(data)
                if sid!=self.sid or end.get('frames')!=self.frames or end.get('samples')!=self.samples:raise ValueError('audio counts mismatch')
                self.status['recording']=False;self.release=time.monotonic()
                delay=end.get('release_to_end_ms',0)
                self.release_delay=round(delay) if type(delay) in (int,float) and 0<=delay<=60000 else 0
                if not self.asr:await self.result(sid,'');self.cancel()
                else:
                    self.status['processing']=True
                    self.asr.end()
                    task=asyncio.create_task(self.finish(sid));self.finalizers.add(task);task.add_done_callback(self.finalizers.discard)
            elif kind in (4,5):self.cancel()
            elif kind==6:
                v=json.loads(data)
                if v.get('token')!=self.token:self.status['controls_rejected']+=1;continue
                if v.get('op')=='transport':
                    self.status['packet']=max(20,min(244,int(v.get('packet',20))));continue
                if v.get('op') not in ('left','right','up','down','delete','all','copy','paste','undo','redo','space','enter','newline'):continue
                self.status['controls_received']+=1;self.status['last_control']=v.get('op')
                if self.sid:self.status['control_error']='busy';continue
                try:
                    await self.keyboard.command(v.get('op'),v.get('arg',''))
                    self.status['controls_posted']+=1;self.status['control_error']=''
                except Exception as exc:
                    self.status['control_error']=type(exc).__name__
                    await self.control('当前桌面无法输入')
    async def pump(self):
        while True:
            try:await self.consume()
            except asyncio.TimeoutError:
                if self.sid and self.status['recording'] and time.monotonic()-self.last_audio>4:await self.failure('蓝牙音频传输中断')
            except asyncio.CancelledError:raise
            except Exception:await self.failure('蓝牙音频数据异常，本次输入已取消')
    async def connect(self,device):
        from bleak import BleakClient
        gone=asyncio.Event()
        async with BleakClient(device,disconnected_callback=lambda _:gone.set(),timeout=12,services=[SERVICE]) as client:
            self.client=client;self.overflow=False;self.assembler.reset();self.token=uuid.uuid4().hex[:16]
            while not self.messages.empty():self.messages.get_nowait()
            self.consumer=asyncio.create_task(self.pump());self.status["phase"]="subscribe"
            try:
                await client.start_notify(AUDIO,self.notification)
                characteristic=client.services.get_characteristic(CONTROL)
                # BlueZ publishes ATT capacity on the characteristic; mtu_size is 23.
                for _ in range(20):
                    if characteristic.max_write_without_response_size>20:break
                    await asyncio.sleep(.1)
                packet=min(244,characteristic.max_write_without_response_size)
                self.status['packet']=packet;self.status['phase']='hello'
                await self.write({'op':'hello','packet':packet,'acks':True,'version':2,'remote':True,'token':self.token,
                    'negotiated':True,'receiverID':self.config['id'],'receiverName':self.config['name']})
                self.connected=True
                self.status.update(connection='initializing')

                # First claim fixes this receiver to one Brick. It never scans or
                # connects a second Brick while its established partner is absent.
                if not self.config.get('brick_address'):
                    self.config['brick_address']=device.address
                    stored={k:v for k,v in self.config.items() if k!='key'}
                    tmp=ROOT/'receiver.new';tmp.write_text(json.dumps(stored));tmp.chmod(0o600);tmp.replace(ROOT/'receiver.json')
                self.status['phase']='state'
                await self.control()
                self.status['phase']='ready'
                self.status['connection']='connected'
                LOG.info('Bluetooth ready packet=%d',self.status['packet'])
                with contextlib.suppress(Exception):self.keyboard.ensure()
                await gone.wait()
            finally:
                self.connected=False;self.cancel();self.consumer.cancel()
                for task in self.finalizers:task.cancel()
                await asyncio.gather(*self.finalizers,return_exceptions=True)
                for task in self.recognizers:task.cancel()
                await asyncio.gather(*self.recognizers,return_exceptions=True)
                await asyncio.gather(self.consumer,return_exceptions=True);self.client=None
    async def run(self):
        from bleak import BleakScanner
        while True:
            self.status['connection']='scanning'
            delay=12
            try:
                address=self.config.get('brick_address')
                def matches(device,ad):return SERVICE in [s.lower() for s in ad.service_uuids] and (not address or device.address==address)
                device=await BleakScanner.find_device_by_filter(matches,timeout=8)
                if device:delay=3;await self.connect(device)
                else:self.status['connection']='waiting'
            except asyncio.CancelledError:raise
            except Exception as exc:
                # Type only: D-Bus/WebSocket exceptions can contain private values.
                code=getattr(exc,'code',None)
                self.status['connection']='waiting';LOG.info('Bluetooth waiting phase=%s (%s, code=%s)',self.status.get('phase','scan'),type(exc).__name__,str(code) if isinstance(code,int) else '-')
                if await self.recover_bluetooth(exc):delay=1
            await asyncio.sleep(delay)
    async def recover_bluetooth(self,error):
        # Boot-time desktop/Steam settings can switch the radio off after the
        # one-shot boot unit. Only handle the explicit POWERED_OFF condition.
        if getattr(getattr(error,'reason',None),'name',None)!='POWERED_OFF':return False
        self.status['connection']='bluetooth-starting'
        if not await power_on_bluetooth():
            self.status['connection']='bluetooth-off';return False
        self.status['bluetooth_recoveries']+=1
        LOG.info('Bluetooth power enabled; retrying selected Brick')
        return True
    async def ipc(self,r,w):
        try:
            line=await asyncio.wait_for(r.readline(),2)
            if line==b'status\n':w.write(json.dumps(self.status).encode()+b'\n');await w.drain()
        finally:w.close();await w.wait_closed()

async def serve(settings):
    receiver=Receiver(settings)
    runtime=Path(os.environ.get('XDG_RUNTIME_DIR',f'/run/user/{os.getuid()}'))/'brick-mic'
    runtime.mkdir(mode=0o700,exist_ok=True)
    with (runtime/'receiver.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        path=runtime/'receiver.sock';path.unlink(missing_ok=True)
        server=await asyncio.start_unix_server(receiver.ipc,path=str(path),limit=128);path.chmod(0o600)
        task=asyncio.create_task(receiver.run())
        stop=asyncio.Event();loop=asyncio.get_running_loop()
        for sig in (signal.SIGTERM,signal.SIGINT):loop.add_signal_handler(sig,stop.set)
        try:await stop.wait()
        finally:
            task.cancel();await asyncio.gather(task,return_exceptions=True);receiver.cancel();receiver.keyboard.close();server.close();await server.wait_closed();path.unlink(missing_ok=True)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--status',action='store_true');parser.add_argument('--check-config',action='store_true');args=parser.parse_args()
    if args.status:
        s=socket.socket(socket.AF_UNIX);s.settimeout(2);s.connect(str(Path(os.environ.get('XDG_RUNTIME_DIR',f'/run/user/{os.getuid()}'))/'brick-mic/receiver.sock'));s.sendall(b'status\n');print(s.recv(4096).decode().strip());s.close();return
    settings=config()
    if args.check_config:print(json.dumps({'key_configured':True,'model':MODEL,'receiver':settings['name']}));return
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(message)s')
    try:asyncio.run(serve(settings))
    except BlockingIOError:raise SystemExit('Brick Mic receiver already running')

if __name__=='__main__':main()
