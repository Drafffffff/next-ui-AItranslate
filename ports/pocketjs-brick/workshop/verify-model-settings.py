#!/usr/bin/env python3
"""Real PocketJS controller/save/reboot checks. Only writes isolated fixture data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image

REPOSITORY=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--project-fixture',type=Path)
args=parser.parse_args()
host,output=args.host.resolve(),args.output.resolve()
output.mkdir(parents=True,exist_ok=True)
sd=output/'sd'
assert not sd.exists(),'Use a fresh output directory'
if args.project_fixture:
 shutil.copytree(next(args.project_fixture.glob('fixture-*/sd')),sd)
else: sd.mkdir()
shared=sd/'.userdata/shared'
data=shared/'ai-game-workshop'
roms=sd/'Roms/AI生成游戏 (P8)'
(data/'projects').mkdir(parents=True,exist_ok=True)
roms.mkdir(parents=True,exist_ok=True)
# Save four long-titled drafts to inspect compact homepage layout and model menu.
if args.project_fixture:
 source=json.loads(next((data/'projects').glob('*.json')).read_text())
 for i in range(3):
  project=dict(source,id=f'game-model-draft-{i}',title='很长的项目名称用来检查列表文字重叠问题',revision=0,messages=[],versions=[],updatedAt=source['updatedAt']-i-1)
  (data/'projects'/(project['id']+'.json')).write_text(json.dumps(project,ensure_ascii=False))
settings=dict(model='deepseek-flash',endpoint='http://127.0.0.1:1/v1',key='isolated-deepseek-fixture',timeoutMs=10000,
 asrKey='isolated-voice-key',asrModel='qwen-audio-3.1-asr-flash-message',asrEndpoint='wss://workspace.ap-southeast-1.maas.aliyuncs.com/api-ws/v1/inference',contextWindowTokens=64000,unrelated={'preserve':True})
(data/'config.json').write_text(json.dumps(settings))
# No shared provider keys and no voice socket. Merely opening/saving the menu cannot make a request.
(shared/'ai-keys.txt').write_text('')
files={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in [*(data/'projects').glob('*.json'),*roms.glob('*.p8')]}
env=dict(os.environ,POCKETJS_WORKSHOP='1',POCKETJS_HARDWARE='0',POCKETJS_DATA=str(data),POCKETJS_ROMS=str(roms),POCKETJS_FONT=str(REPOSITORY/'fonts/font1.ttf'),SHARED_USERDATA_PATH=str(shared),NEXTUI_AI_KEYS_FILE=str(shared/'ai-keys.txt'))
for key in ['POCKETJS_WORKSHOP_RESUME','POCKETJS_VOICE','POCKETJS_TEST','POCKETJS_VOICE_SOCKET']:env.pop(key,None)
dump=output/'models'
p=subprocess.run([str(host),'--workshop-models-test','--dump',str(dump)],cwd=host.parent,env=env,capture_output=True,text=True,timeout=100)
(output/'stdout.log').write_text(p.stdout)
(output/'stderr.log').write_text(p.stderr)
assert p.returncode==0,p.stdout+'\n'+p.stderr
saved=json.loads((data/'config.json').read_text())
assert saved['model']=='deepseek-v4.1-flash' and saved['provider']=='bailian'
assert saved['key']=='' and saved['endpoint']=='https://dashscope.aliyuncs.com/compatible-mode/v1'
assert saved['generationChannels']['deepseek']=={'endpoint':settings['endpoint'],'key':settings['key']}
for key in ['asrKey','asrModel','asrEndpoint','timeoutMs','contextWindowTokens','unrelated']:assert saved[key]==settings[key],key
for path,digest in files.items():assert hashlib.sha256(path.read_bytes()).hexdigest()==digest,path
for name in ['options','home','reopened']:
 image=Image.open(str(dump)+'.'+name+'.ppm');image.save(output/(name+'.png'))
 # Nothing may escape the right edge of the content region, including the sixth homepage shortcut.
 background=image.getpixel((1023,700))
 assert all(image.getpixel((x,y))==background for x in range(970,1024) for y in range(700,768)),name+' bottom shortcut overflow'
print('PASS: native controller selection/cancel/save/reboot, ASR and provider credential preservation, unchanged projects/ROMs, compact footer bounds. '+str(output))
