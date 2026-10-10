#!/usr/bin/env python3
"""Controller/voice question suspension and recovery in actual native PocketJS."""
import argparse,importlib.util,json,os,socketserver,subprocess,sys,tempfile,threading
from pathlib import Path
from http.server import ThreadingHTTPServer
from PIL import Image
spec=importlib.util.spec_from_file_location('experience',Path(__file__).with_name('verify-experience.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
v=m.verify
parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);a=parser.parse_args();host=a.host.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
class Model:
 def __init__(self):self.errors=[];self.requests=[];self.lock=threading.Lock()
 def completion(self,path,auth,body):
  self.requests.append(body);assert auth=='Bearer '+v.FAKE_KEY
  messages=body['messages'];calls={c['id']:c['function']['name'] for x in messages for c in x.get('tool_calls',[])}
  results={calls[x['tool_call_id']]:json.loads(x['content']) for x in messages if x['role']=='tool' and x['tool_call_id'] in calls}
  if 'ask_user_question' not in results:return v.response_for(None,[v.tool_call(1,'ask_user_question',{'question':'这个地牢你更喜欢实时躲闪还是走一步敌人动一步？','options':[{'label':'实时躲闪','description':'方向键移动，抓准时机避开敌人'},{'label':'回合制探索','description':'每走一步再行动，保留灯火与资源取舍'},{'label':'轻松探索','description':'减少战斗，专注收集与寻找出口'}]})])
  if 'read_game' not in results:return v.response_for(None,[v.tool_call(1,'read_game')])
  if 'write_game' not in results:return v.response_for(None,[v.tool_call(1,'write_game',{'lua':v.lua_for_revision(1),'title':'问答地牢','summary':'依据实际选择实现'})])
  if 'check_game' not in results:return v.response_for(None,[v.tool_call(1,'check_game')])
  if 'publish_game' not in results:return v.response_for(None,[v.tool_call(1,'publish_game')])
  return v.response_for('已根据你的选择保存，请按 SR 试玩。')
for label in ['choice','reopen','voice','cancel']:
 data=out/label/'data';roms=out/label/'roms';data.mkdir(parents=True);roms.mkdir();shared=data.parent/'shared';shared.mkdir()
 model=Model();server=ThreadingHTTPServer(('127.0.0.1',0),v.make_handler(model));threading.Thread(target=server.serve_forever,daemon=True).start()
 (data/'config.json').write_text(json.dumps(dict(key=v.FAKE_KEY,endpoint=f'http://127.0.0.1:{server.server_port}/v1',model='deepseek-flash',timeoutMs=10000,reducedMotion=True)))
 env=dict(os.environ,POCKETJS_WORKSHOP='1',POCKETJS_HARDWARE='0',POCKETJS_DATA=str(data),POCKETJS_ROMS=str(roms),POCKETJS_FONT=str(v.REPOSITORY/'fonts/font1.ttf'),SHARED_USERDATA_PATH=str(shared),NO_PROXY='127.0.0.1,localhost')
 for key in ['POCKETJS_VOICE','POCKETJS_VOICE_SOCKET','POCKETJS_WORKSHOP_RESUME','POCKETJS_TEST']:env.pop(key,None)
 try:
  with tempfile.TemporaryDirectory(prefix='wg-q-',dir='/tmp') as ipc:
   voice=None
   if label=='voice':
    voice=m.VoiceFixture(ipc+'/asr.sock');threading.Thread(target=voice.serve_forever,daemon=True).start();env['POCKETJS_VOICE_SOCKET']=ipc+'/asr.sock'
   def run(mode):
    r=subprocess.run([str(host),mode,'--dump',str(out/label/'screen')],cwd=host.parent,env=env,capture_output=True,text=True,timeout=90)
    (out/label/(mode+'.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
   if label=='reopen':
    run('--workshop-question-wait');p=json.loads(next((data/'projects').glob('*.json')).read_text());assert p['question'] and p['revision']==0
    run('--workshop-question-resume')
   else:run('--workshop-question-voice-test' if label=='voice' else '--workshop-question-cancel-test' if label=='cancel' else '--workshop-question-test')
   p=json.loads(next((data/'projects').glob('*.json')).read_text());assert p['revision']==1 and not p.get('question');assert len(list(roms.glob('*.p8')))==1
   answers=[json.loads(x['content'][0]['text']) for x in p['messages'] if x['role']=='toolResult' and x['toolName']=='ask_user_question'];assert len(answers)==1
   assert ('方向键' if label=='voice' else '回合制探索') in answers[0]['answer'],answers
   if voice:assert 'start' in voice.operations and 'stop' in voice.operations;voice.shutdown();voice.server_close()
   image=Image.open(out/label/'screen.question.ppm');image.save(out/label/'question.png');assert len(set(image.crop((56,620,944,628)).getdata()))==1,'Question overlaps footer'
   Image.open(out/label/'screen').save(out/label/'completed.png')
 finally:server.shutdown();server.server_close()
print('PASS: native short-A actual choice, hold-A voice answer, power-loss reopen, X stops/B returns/reopen resumes, one paired answer, same ROM scaffold→revision1, footer bounds. '+str(out))
