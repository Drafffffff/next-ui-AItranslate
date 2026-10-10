#!/usr/bin/env python3
"""Native return-view cache, full-history hydration and invalid-cache fallback."""
import argparse,hashlib,json,os,shutil,subprocess,time
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host',type=Path,required=True)
parser.add_argument('--baseline',type=Path)
parser.add_argument('--sd',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();host=args.host.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
sd=out/'sd';assert not sd.exists();shutil.copytree(args.sd,sd)
data=sd/'.userdata/shared/ai-game-workshop';roms=sd/'Roms/AI生成游戏 (P8)'
(data/'config.json').write_text(json.dumps({'key':'','endpoint':'https://example.invalid','model':'deepseek-flash','timeoutMs':120000,'reducedMotion':True}))
env=dict(os.environ,POCKETJS_WORKSHOP='1',POCKETJS_HARDWARE='0',POCKETJS_DATA=str(data),POCKETJS_ROMS=str(roms),POCKETJS_FONT=str(ROOT/'fonts/font1.ttf'),SHARED_USERDATA_PATH=str(sd/'.userdata/shared'),POCKETJS_P8_LAUNCH=shutil.which('true') or '/usr/bin/true')
for k in ['POCKETJS_VOICE','POCKETJS_VOICE_SOCKET','POCKETJS_WORKSHOP_RESUME','POCKETJS_TEST']:env.pop(k,None)
def run(binary,mode,label,resume=False):
 e=dict(env)
 if resume:e['POCKETJS_WORKSHOP_RESUME']='1'
 t=time.monotonic();r=subprocess.run([str(binary),mode,'--dump',str(out/(label+'.ppm'))],cwd=binary.parent,env=e,capture_output=True,text=True,timeout=60)
 (out/(label+'.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
 print(label,round((time.monotonic()-t)*1000),'ms process');print(r.stdout.strip())
def hashes():return {str(f.relative_to(sd)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*data.glob('projects/*.json'),*roms.glob('*.p8')]}
run(host,'--workshop-return-seed','seed')
id=(data/'last-project.txt').read_text().strip();meta=data/'project-info'/(id+'.json');original=json.loads(meta.read_text());assert original['returnView']['rows']
before=hashes()
if args.baseline:run(args.baseline.resolve(),'--workshop-return-baseline','baseline',True);assert hashes()==before
run(host,'--workshop-return-test','cached',True);assert hashes()==before
image=Image.open(out/'cached.ppm').convert('RGB');image.save(out/'cached.png')
assert len(set(image.crop((56,151,944,619)).getdata()))>4,'Cached result not rendered'
assert len(set(image.crop((56,620,680,628)).getdata()))==1,'Cached rows overlap shortcuts'
for label,patch in [('wrong-revision',{'revision':original['revision']+1}),('wrong-layout',{'layout':999})]:
 wrong=dict(original,returnView=dict(original['returnView'],**patch));meta.write_text(json.dumps(wrong,ensure_ascii=False));run(host,'--workshop-return-fallback',label,True);assert hashes()==before
meta.write_text('corrupt');run(host,'--workshop-return-fallback','corrupt',True);assert hashes()==before
meta.unlink();run(host,'--workshop-return-fallback','missing',True);assert hashes()==before
print('PASS: cached latest page before full-history parsing; corruption/revision/layout/missing fallback; no project/ROM mutation; clear footer.')
