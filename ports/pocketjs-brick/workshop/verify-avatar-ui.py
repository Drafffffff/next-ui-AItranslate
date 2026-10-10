#!/usr/bin/env python3
"""Exercise the actual bloub engine/native rasterizer in isolated PocketJS data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image,ImageChops
root=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host',type=Path,required=True)
parser.add_argument('--project-fixture',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--reduced-motion',action='store_true')
args=parser.parse_args();host=args.host.resolve();output=args.output.resolve()
output.mkdir(parents=True,exist_ok=True);sd=output/'sd';assert not sd.exists(),'Use a fresh output'
shutil.copytree(next(args.project_fixture.glob('fixture-*/sd')),sd)
shared=sd/'.userdata/shared';data=shared/'ai-game-workshop';roms=sd/'Roms/AI生成游戏 (P8)'
settings=json.loads((data/'config.json').read_text());settings.update(endpoint='http://127.0.0.1:1/v1',key='',reducedMotion=args.reduced_motion);(data/'config.json').write_text(json.dumps(settings))
(shared/'ai-keys.txt').write_text('')
env=dict(os.environ,POCKETJS_WORKSHOP='1',POCKETJS_HARDWARE='0',POCKETJS_DATA=str(data),POCKETJS_ROMS=str(roms),POCKETJS_FONT=str(root/'fonts/font1.ttf'),SHARED_USERDATA_PATH=str(shared),NEXTUI_AI_KEYS_FILE=str(shared/'ai-keys.txt'))
for name in ['POCKETJS_VOICE','POCKETJS_VOICE_SOCKET','POCKETJS_WORKSHOP_RESUME','POCKETJS_TEST']:env.pop(name,None)
old={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in roms.glob('*.p8')}
p=subprocess.run([str(host),'--workshop-avatar-test','--dump',str(output/'avatar')],cwd=host.parent,env=env,capture_output=True,text=True,timeout=100)
(output/'stdout.log').write_text(p.stdout);(output/'stderr.log').write_text(p.stderr);assert p.returncode==0,p.stdout+'\n'+p.stderr
states=['idle','listening','thinking','working','done','error'];tiles=[];hashes=set()
for i,name in enumerate(states):
 images=[Image.open(output/f'avatar.{i}.{n}.ppm').convert('RGB') for n in [24,48]]
 for image,n in zip(images,[24,48]):image.save(output/f'{name}.{n}.png')
 crops=[image.crop((56,28,136,108)) for image in images]
 if args.reduced_motion:assert ImageChops.difference(*crops).getbbox() is None,name+' moved with reducedMotion'
 else:assert ImageChops.difference(*crops).getbbox(),name+' did not animate in native rasterizer'
 hashes.add(hashlib.sha256(crops[0].tobytes()).hexdigest());tiles.append(crops[0].resize((192,192)))
 # Avatar never crosses the header divider or version text area.
 assert ImageChops.difference(images[0].crop((56,118,968,120)),images[1].crop((56,118,968,120))).getbbox() is None
assert len(hashes)>=5,'Native state renders collapsed together (thinking and working share orbit)'
if not args.reduced_motion:
 long=[Image.open(output/f'avatar.2.{n}.ppm').convert('RGB').crop((56,28,136,108)) for n in [624,648]]
 assert ImageChops.difference(*long).getbbox(),'Orbit stopped during a long model request'
for path,digest in old.items():assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
sheet=Image.new('RGB',(192*6,192))
for i,tile in enumerate(tiles):sheet.paste(tile,(i*192,0))
sheet.save(output/'states.png')
print(p.stdout.strip());print('PASS: native state pixels and motion mode; long orbit; bounded 80px header; unchanged ROM. '+str(output))
