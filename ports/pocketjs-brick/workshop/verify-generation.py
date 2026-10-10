#!/usr/bin/env python3
"""Offline native generation/long-save/retry benchmark. Optional SSH uses only
the supplied isolated fixture root, never the user's real projects or model key."""
import argparse,json,os,shutil,subprocess,threading,socket,gzip,hashlib,tempfile,time
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
ROOT=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host',type=Path,required=True);parser.add_argument('--sd',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True);parser.add_argument('--baseline',type=Path)
parser.add_argument('--ssh');parser.add_argument('--remote-root');parser.add_argument('--retries',action='store_true');parser.add_argument('--voice',action='store_true',help='Synthetic ASR IPC and controller input on SSH fixture; does not test actual ASR')
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
requests=[];failures=[];injected=set()
def messages_from(p,data):
 if not p.get("history"):return p["messages"]
 result=[]
 for page in p["history"]["pages"]:
  raw=(data/page["file"]).read_bytes();assert hashlib.sha256(raw).hexdigest()==page["hash"];result.extend(json.loads(raw))
 assert len(result)==p["history"]["count"];return result
THINKING='检查当前源码与声音资源，保留唯一游戏入口。'
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_POST(self):
  try:
   body=json.loads(self.rfile.read(int(self.headers['Content-Length'])));messages=body['messages']
   if not body.get('tools'):
    size=int(self.headers['Content-Length']);assert size<1500000,'Summary input exceeds 500k window'
    requests.append(dict(bytes=size,summary=True,max_tokens=body.get('max_tokens')))
    raw=json.dumps({'choices':[{'message':{'role':'assistant','content':'保留方向移动、背景音乐和操作反馈；最新需求是修改速度。'},'finish_reason':'stop'}],'usage':{'prompt_tokens':100,'completion_tokens':30,'total_tokens':130}},ensure_ascii=False).encode()
    self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw);return
   start=max(i for i,m in enumerate(messages) if m['role']=='user')
   names={c['id']:c['function']['name'] for m in messages[start+1:] for c in m.get('tool_calls',[])}
   results={names[m['tool_call_id']]:json.loads(m['content']) for m in messages[start+1:] if m['role']=='tool'}
   if len(requests) and any(m.get('tool_calls') for m in messages[start+1:]):
    assert all(m.get('reasoning_content')==THINKING for m in messages[start+1:] if m['role']=='assistant' and m.get('tool_calls'))
   requests.append(dict(bytes=int(self.headers['Content-Length']),thinking=body.get('thinking'),max_tokens=body.get('max_tokens'),request_hash=hashlib.sha256(json.dumps(body,sort_keys=True,ensure_ascii=False).encode()).hexdigest(),results=list(results)))
   if args.retries and 'check_game' in results and 'publish_game' not in results and 'checked' not in injected:
    injected.add('checked');self.send_response(503);self.send_header('Content-Length','0');self.end_headers();return
   if 'read_game' not in results:name='read_game';params={}
   elif 'write_game' not in results:name='write_game';params={'edits':[{'oldText':'speed=1','newText':'speed=2'}]}
   elif 'check_game' not in results:name='check_game';params={}
   elif 'publish_game' not in results:name='publish_game';params={}
   else:name=None;params={}
   frames=[{'choices':[{'delta':{'reasoning_content':THINKING},'finish_reason':None}]}]
   if name:frames.append({'choices':[{'delta':{'tool_calls':[{'index':0,'id':'gen-'+name,'function':{'name':name,'arguments':json.dumps(params)}}]},'finish_reason':None}]})
   else:frames.append({'choices':[{'delta':{'content':'已更新速度，按 SR 试玩。'},'finish_reason':None}]})
   frames.append({'choices':[{'delta':{},'finish_reason':'tool_calls' if name else 'stop'}]})
   wire=''.join('data: '+json.dumps(f,ensure_ascii=False)+'\n\n' for f in frames)+'data: [DONE]\n\n';raw=wire.encode()
   if args.retries and 'publish_game' in results and 'published' not in injected:
    injected.add('published');self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Content-Length',str(len(raw)+256));self.end_headers();self.wfile.write(raw[:80]);self.wfile.flush();self.connection.shutdown(socket.SHUT_RDWR);return
   self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
  except Exception as e:
   failures.append(str(e));raw=json.dumps({'error':{'message':str(e)}}).encode();self.send_response(400);self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start();port=server.server_address[1]
for label,binary in [('baseline',args.baseline),('current',args.host)]:
 if not binary:continue
 sd=out/(label+'-sd');assert not sd.exists();shutil.copytree(args.sd,sd)
 data=sd/'.userdata/shared/ai-game-workshop';roms=sd/'Roms/AI生成游戏 (P8)'
 (data/'config.json').write_text(json.dumps(dict(key='generation-fixture',endpoint=f'http://127.0.0.1:{port}/v1',model='deepseek-flash',timeoutMs=10000,reducedMotion=True)))
 originals={f.name:messages_from(json.loads(f.read_text()),data) for f in (data/'projects').glob('*.json')}
 if args.ssh:
  import tarfile,shlex
  assert args.remote_root and args.remote_root.startswith('/mnt/SDCARD/.userdata/shared/workshop-optimization-')
  fixture=label+'-'+out.name+'-sd';remote=args.remote_root+'/'+fixture;archive=out/(label+'.tar.gz')
  with tarfile.open(archive,'w:gz') as tar:tar.add(sd,arcname=fixture)
  with archive.open('rb') as file:subprocess.run(['ssh','-T',args.ssh,f'cd {shlex.quote(args.remote_root)} && tar -xzf -'],stdin=file,check=True)
  pak=args.remote_root+'/'+label
  command=f'export LD_LIBRARY_PATH=/mnt/SDCARD/.system/tg5040/lib:/usr/trimui/lib PATH=/mnt/SDCARD/.system/tg5040/bin:/usr/trimui/bin:$PATH POCKETJS_WORKSHOP=1 POCKETJS_HARDWARE=0 POCKETJS_DATA={shlex.quote(remote+"/.userdata/shared/ai-game-workshop")} POCKETJS_ROMS={shlex.quote(remote+"/Roms/AI生成游戏 (P8)")} SHARED_USERDATA_PATH={shlex.quote(remote+"/.userdata/shared")} POCKETJS_FONT=/mnt/SDCARD/.system/res/font1.ttf; unset POCKETJS_VOICE POCKETJS_TEST POCKETJS_WORKSHOP_RESUME; cd {shlex.quote(pak)}; ./pocketjs-app.elf --dummy-video --workshop-generation-perf'
  if args.voice:
   helper=out/'voice-fixture';subprocess.run(['go','build','-o',str(helper),str(ROOT/'ports/pocketjs-brick/workshop/verify-voice-fixture.go')],env=dict(os.environ,GOOS='linux',GOARCH='arm64',CGO_ENABLED='0'),check=True)
   remote_helper=remote+'/voice-fixture';socket_name='/tmp/workshop-qa-'+str(port)+'.sock'
   subprocess.run(['ssh','-T',args.ssh,'cat > '+shlex.quote(remote_helper)+' && chmod +x '+shlex.quote(remote_helper)],input=helper.read_bytes(),check=True)
   command=command.replace('unset POCKETJS_VOICE POCKETJS_TEST POCKETJS_WORKSHOP_RESUME;', 'unset POCKETJS_VOICE POCKETJS_TEST POCKETJS_WORKSHOP_RESUME; export POCKETJS_VOICE_SOCKET='+shlex.quote(socket_name)+';')
   command=command.replace('./pocketjs-app.elf --dummy-video --workshop-generation-perf','./pocketjs-app.elf --dummy-video --workshop-generation-voice-perf')
   command=shlex.quote(remote_helper)+' '+shlex.quote(socket_name)+' & voice_pid=$!; trap \'kill "$voice_pid"; rm -f '+shlex.quote(socket_name)+'\' EXIT; '+command
  result=subprocess.run(['ssh','-T','-o','ExitOnForwardFailure=yes','-R',f'{port}:127.0.0.1:{port}',args.ssh,command],capture_output=True,text=True,timeout=300)
  # Fetch the isolated project's final state for content/one-publication checks.
  project=next((data/'projects').glob('*.json'));read=subprocess.run(['ssh',args.ssh,'gzip -c '+shlex.quote(remote+'/.userdata/shared/ai-game-workshop/projects/'+project.name)],capture_output=True,check=True);project.write_bytes(gzip.decompress(read.stdout))
  if json.loads(project.read_text()).get('history'):
   import io
   histories=subprocess.check_output(['ssh',args.ssh,'cd '+shlex.quote(remote+'/.userdata/shared/ai-game-workshop')+' && tar -czf - history']);
   with tarfile.open(fileobj=io.BytesIO(histories)) as tar:tar.extractall(data,filter='data')
 else:
  env=dict(os.environ,POCKETJS_WORKSHOP='1',POCKETJS_HARDWARE='0',POCKETJS_DATA=str(data),POCKETJS_ROMS=str(roms),SHARED_USERDATA_PATH=str(sd/'.userdata/shared'),POCKETJS_FONT=str(ROOT/'fonts/font1.ttf'))
  for k in ['POCKETJS_VOICE','POCKETJS_TEST','POCKETJS_WORKSHOP_RESUME']:env.pop(k,None)
  if args.voice:
   helper=out/'voice-fixture';subprocess.run(['go','build','-o',str(helper),str(ROOT/'ports/pocketjs-brick/workshop/verify-voice-fixture.go')],check=True)
   with tempfile.TemporaryDirectory(prefix='wg-qa-',dir='/tmp') as ipc:
    socket_name=ipc+'/asr.sock';env['POCKETJS_VOICE_SOCKET']=socket_name
    with (out/'voice.log').open('w') as log:
     voice=subprocess.Popen([str(helper),socket_name],stderr=log)
     try:
      for _ in range(100):
       if Path(socket_name).exists():break
       time.sleep(.01)
      result=subprocess.run([str(binary.resolve()),'--dummy-video','--workshop-generation-voice-perf'],cwd=binary.resolve().parent,env=env,capture_output=True,text=True,timeout=120)
     finally:voice.terminate();voice.wait(timeout=5)
    result.stderr+='\n'+(out/'voice.log').read_text()
  else:result=subprocess.run([str(binary.resolve()),'--dummy-video','--workshop-generation-perf'],cwd=binary.resolve().parent,env=env,capture_output=True,text=True,timeout=120)
 (out/(label+'.log')).write_text(result.stdout+result.stderr);assert result.returncode==0,result.stdout+result.stderr
 print(label,result.stdout.strip())
 for file in (data/'projects').glob('*.json'):
  project=json.loads(file.read_text());assert messages_from(project,data)[:len(originals[file.name])]==originals[file.name];assert project['revision']==2;assert len(project['versions'])==2
 print(label,'PASS: full historical prefix preserved; one checked publication; no real API.')
assert not failures,failures
if args.voice:assert 'VOICE_FIXTURE start' in result.stderr and 'VOICE_FIXTURE stop' in result.stderr
if args.retries:
 regular=[r for r in requests if not r.get('summary')]
 assert injected=={'checked','published'} and len(regular)==7
 assert regular[3]['request_hash']==regular[4]['request_hash'] and regular[5]['request_hash']==regular[6]['request_hash']
(out/'requests.json').write_text(json.dumps(requests,ensure_ascii=False,indent=2));server.shutdown()
