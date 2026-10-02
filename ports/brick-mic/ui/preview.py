#!/usr/bin/env python3
"""The real PocketJS UI with a local Unix-socket microphone mock; no cloud calls."""
import argparse,json,math,os,socket,subprocess,tempfile,threading,time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
PACKAGE=ROOT/'build/brick-mic/pocketjs-preview'
TEXT='今天讨论的是掌机语音输入的体验。任务页面应该保留最新回复，识别结束后先展示本次口述内容。\n\n我希望操作提示集中在底部，间距一致，长段文字能够继续翻阅。'*4

class Mock:
    def __init__(self,path,state='ready',controls=False):
        self.server=socket.socket(socket.AF_UNIX);self.server.bind(str(path));self.server.listen()
        self.commands=[];self.state=state;self.session=1;self.started=time.monotonic();self.finish=0;self.alive=True
        self.churn_after=0;self.draft=state=='result';self.short_reply=False;self.focus_churn=False;self.reply_revision=0;self.reply_at=0
        self.alerts=[];self.remote_allowed=True;self.controls=controls;self.mode='ordinary';self.review=False;self.reply_page=None;self.target='';self.task_id='00000000-0000-4000-8000-000000000001'
        self.identical_reply=False;self.live_reply_at=0;self.offline_until=0;self.recovered_pings=0
        self.thread=threading.Thread(target=self.serve,daemon=True);self.thread.start()
    def serve(self):
        while self.alive:
            try:
                conn,_=self.server.accept()
                with conn:
                    command=conn.recv(128).decode().strip();now=time.monotonic()
                    if now<self.offline_until:continue
                    if command=='ping' and self.offline_until:self.recovered_pings+=1
                    if command in ('start','stop','cancel','sleep') or command.startswith(('control:','hosts:')):
                        self.commands.append(command)
                        if command=='start':self.state='recording';self.started=now;self.session+=1
                        elif command=='stop' and self.state=='recording':self.state='processing';self.finish=now+.35
                        elif command in ('cancel','sleep'):self.state='ready';self.finish=0
                    if command=='control:mode:codex':self.mode='codex'
                    if command=='control:mode:ordinary':self.mode='ordinary';self.target='';self.reply_page=None
                    if command.startswith('control:choose:'):self.target='开发 Brick Mic';self.reply_page=0;self.draft=False
                    if command=='control:reply:open':self.reply_page=0
                    if command=='control:reply:next':self.reply_page=min(1,(self.reply_page or 0)+1)
                    if command=='control:reply:prev':self.reply_page=max(0,(self.reply_page or 0)-1)
                    if command=='control:reply:close':self.reply_page=0
                    if command=='control:submit':self.reply_at=now+.30;self.draft=False
                    if self.live_reply_at and now>=self.live_reply_at:self.reply_revision+=1;self.reply_page=0;self.live_reply_at=0
                    if command.startswith('control:reply:seen:'):self.alerts=[a for a in self.alerts if a['kind']!='completed']
                    if self.reply_at and now>=self.reply_at:self.reply_revision=1;self.reply_page=0;self.reply_at=0
                    if self.state=='processing' and self.finish and now>=self.finish:self.state='result';self.draft=True
                    if command=='ping':payload=b'ready\n'
                    else:
                        elapsed=now-self.started
                        data=dict(state='ready' if self.state=='result' else self.state,connected=self.state not in ('bluetooth','service','disconnected','error'),session=self.session,seconds=elapsed,rms=int(3500+2600*math.sin(elapsed*6)),text=TEXT if self.state=='result' else '',battery=dict(percent=95,charging=False),control=dict(draftAvailable=self.draft,remoteAllowed=self.remote_allowed,mode=self.mode,targetID=self.task_id if self.target else '',focusPending=self.focus_churn and elapsed>self.churn_after and int(elapsed*10)%2==1,codexAvailable=self.controls,target=self.target,tasks=[dict(id=self.task_id,title='开发 Brick Mic',status='idle',unread=len(self.alerts))] if self.controls else [],unread=len(self.alerts),alerts=self.alerts,controlHint=''),error='蓝牙服务暂时不可用，请按 A 重试。' if self.state=='error' else '')
                        if self.review:
                            data['control'].update(mode='codex',target='开发 Brick Mic',replyAvailable=True,approval=dict(id=self.task_id,task=self.task_id,title='开发 Brick Mic',tool='Bash',summary='允许执行以下操作吗？\n运行项目测试，确认修改没有引入错误。\n命令：go test ./...\n工作目录：当前项目',expiresAt=time.time()+90,allowAvailable=True))
                        if self.mode=='codex' and self.target:
                            data['control']['replyAvailable']=True
                            page=self.reply_page or 0
                            data['control']['reply']=dict(revision=str(self.reply_revision),task=self.task_id,title=self.target,text=('这是发送后的新回复。' if self.reply_revision and not self.identical_reply else '这是最新 AI 回复。' if page==0 else '这是回复第二页。')+('' if self.short_reply else '\n'+'\n'.join('任务回复第 '+str(n)+' 行。' for n in range(1,14))),page=page,pages=1 if self.short_reply else 2,truncated=False)
                        data['hosts']=dict(selected='mac-host-123',active='mac-host-123',discovering=False,known=[dict(id='mac-host-123',name='Mac mini'),dict(id='linux-host-123',name='bazzite')])
                        payload=(json.dumps(data,ensure_ascii=False,separators=(',',':'))+'\n').encode()
                    conn.sendall(payload)
            except OSError:
                if self.alive:raise
    def close(self):self.alive=False;self.server.close()

def run(state,frames=0,output=None,input_test=False,settings=None,power_test=False,wake_record_test=False,control_input_test=False,review_input_test=False,reply_input_test=False,remote_allowed=True,task_main=False,focus_churn=False,statistics=False,navigation_input_test=False,read_input_test=False,aux_input_test=False,short_reply=False,page_test=None,live_reply=False,identical_reply=False,connections_input_test=False):
    with tempfile.TemporaryDirectory(prefix='brick-mic-preview-') as folder:
        data=Path(folder);mock=Mock(data/'mic.sock',state,control_input_test or review_input_test or reply_input_test or task_main or navigation_input_test or aux_input_test or bool(page_test));mock.review=review_input_test;mock.remote_allowed=remote_allowed;mock.focus_churn=focus_churn;mock.short_reply=short_reply
        if live_reply:mock.live_reply_at=time.monotonic()+4.6;mock.identical_reply=identical_reply
        if review_input_test or reply_input_test or task_main:mock.mode='codex';mock.target='开发 Brick Mic';mock.reply_page=0
        if page_test:
            mock.churn_after=1.8
            mock.mode='ordinary' if page_test=='read' else 'codex';mock.target='开发 Brick Mic';mock.reply_page=0
            if page_test=='read':mock.state='result'
            if page_test=='approval':mock.review=True
            mock.alerts=[dict(id='event-one',task=mock.task_id,title='开发 Brick Mic',kind='completed'),dict(id='event-two',task=mock.task_id,title='开发 Brick Mic',kind='waiting')]
        env={**os.environ,'BRICK_MIC_PREVIEW':'1','BRICK_MIC_SOCKET':str(data/'mic.sock'),'POCKETJS_DATA':str(data/'cache'),'BRICK_MIC_FONT':str(ROOT/'fonts/font1.ttf'),'BRICK_MIC_SETTINGS':str(settings or ROOT/'build/brick-mic/theme-preview.txt')}
        args=[str(PACKAGE/('pocketjs-mic' if frames else 'Brick Mic UI Preview.app/Contents/MacOS/pocketjs-mic'))]
        if frames:args+=['--headless','--frames',str(frames)]
        if page_test:args+=['--ui-page-test',page_test]
        if connections_input_test:args+=['--connections-input-test']
        if input_test:args+=['--input-test']
        if power_test:args+=['--power-test']
        if wake_record_test:args+=['--wake-record-test']
        if control_input_test:args+=['--control-input-test']
        if review_input_test:args+=['--review-input-test']
        if reply_input_test:args+=['--live-reply-test' if live_reply else '--reply-input-test']
        if navigation_input_test:args+=['--navigation-input-test']
        if read_input_test:args+=['--read-input-test']
        if aux_input_test:args+=['--aux-input-test']
        if output:args+=['--dump',str(output),'--dump-first',str(output)+'.first.ppm']
        if not frames:
            forwarded=['open','-n','-W']
            for key in ['BRICK_MIC_PREVIEW','BRICK_MIC_SOCKET','POCKETJS_DATA','BRICK_MIC_FONT','BRICK_MIC_SETTINGS']:
                forwarded+=['--env',key+'='+env[key]]
            args=forwarded+[str(PACKAGE/'Brick Mic UI Preview.app')]
        try:
            proc=subprocess.run(args,cwd=PACKAGE,env=env,check=True,timeout=30 if frames else None,capture_output=statistics,text=statistics)
            return (mock.commands,proc.stdout) if statistics else mock.commands
        finally:mock.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--state',default='ready',choices=['bluetooth','service','disconnected','ready','recording','processing','result','error']);parser.add_argument('--frames',type=int,default=0);parser.add_argument('--output',type=Path)
    args=parser.parse_args();run(args.state,args.frames,args.output)
