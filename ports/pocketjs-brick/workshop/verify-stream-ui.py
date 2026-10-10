#!/usr/bin/env python3
"""Live native curl/SSE/Pi/UI pipeline against a deliberately slow local server."""
import argparse,importlib.util,json,os,sys,threading,subprocess
from pathlib import Path
from http.server import ThreadingHTTPServer
from PIL import Image
spec=importlib.util.spec_from_file_location('workshop_verify',Path(__file__).with_name('verify.py'));verify=importlib.util.module_from_spec(spec);sys.modules[spec.name]=verify;spec.loader.exec_module(verify)
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--markdown',action='store_true');args=parser.parse_args();host=args.host.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
data=output/'data';assert not data.exists(),'Use a fresh output';(data/'projects').mkdir(parents=True);roms=output/'roms';roms.mkdir();(output/'ai-keys.txt').write_text('')
final='STREAM-BEGIN\n'+('这是流式生成的回复，方向键移动，O 重开。🎮\n'*65)+'STREAM-END'
if args.markdown:final='STREAM-BEGIN\n## 玩法计划\n**先做手感**，再完善关卡。\n- [x] 方向键移动\n```lua\n'+('-- **literal** 这是流式生成的代码。🎮\n'*65)+'```\n> 按 `SR` 试玩\n**STREAM-END**'
class State(verify.MockState):
 def completion(self,*args):
  value=super().completion(*args)
  if value['choices'][0]['message'].get('content'):value['choices'][0]['message']['content']=final
  return value
state=State();server=ThreadingHTTPServer(('127.0.0.1',0),verify.make_handler(state));server.daemon_threads=True;server.stream_delay=.02;threading.Thread(target=server.serve_forever,daemon=True).start()
(data/'config.json').write_text(json.dumps(dict(key=verify.FAKE_KEY,endpoint=f'http://127.0.0.1:{server.server_port}/v1',model='deepseek-flash',timeoutMs=15000)))
env=dict(os.environ,POCKETJS_WORKSHOP='1',POCKETJS_HARDWARE='0',POCKETJS_DATA=str(data),POCKETJS_ROMS=str(roms),POCKETJS_FONT=str(verify.REPOSITORY/'fonts/font1.ttf'),SHARED_USERDATA_PATH=str(output),NEXTUI_AI_KEYS_FILE=str(output/'ai-keys.txt'),NO_PROXY='127.0.0.1,localhost',no_proxy='127.0.0.1,localhost')
for key in ['POCKETJS_VOICE','POCKETJS_VOICE_SOCKET','POCKETJS_WORKSHOP_RESUME','POCKETJS_TEST']:env.pop(key,None)
try:
 p=subprocess.run([str(host),'--workshop-stream-test','--dump',str(output/'stream.ppm')],cwd=host.parent,env=env,capture_output=True,text=True,timeout=80)
 (output/'stdout.log').write_text(p.stdout);(output/'stderr.log').write_text(p.stderr);assert p.returncode==0,p.stdout+'\n'+p.stderr
 project=json.loads(next((data/'projects').glob('*.json')).read_text());assert project['revision']==1
 text='\n'.join(c.get('text','') for c in project['messages'][-1]['content'] if c.get('type')=='text');assert text==final,'Split UTF-8 or SSE corrupted the stored answer'
 assert not state.errors and len(state.requests)==6,state.errors
 for path in output.glob('*.ppm'):Image.open(path).save(path.with_suffix('.png'))
 print(p.stdout.strip());print('PASS: slow native stream visible early, final Chinese/emoji preserved, real five-tool publication, no real model/ASR calls. '+str(output))
finally:server.shutdown();server.server_close()
