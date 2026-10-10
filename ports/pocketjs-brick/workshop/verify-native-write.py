#!/usr/bin/env python3
"""Compile an isolated test entry and exercise actual native chunk staging.
Uses a temporary SD fixture, never project credentials or model requests."""
import argparse,os,shutil,subprocess
from pathlib import Path
r=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();assert not out.exists();pak=out/'pak';pak.mkdir(parents=True)
bun=r/'build/pocketjs-port/bun/bun-darwin-aarch64/bun';up=r/'build/pocketjs-port/upstream'
subprocess.run([str(bun),str(up/'tools/build.ts'),str(r/'ports/pocketjs-brick/workshop/verify-native-write.tsx'),'--framework=solid','--no-config','--density=1','--hz=60','--font-regular='+str(r/'fonts/font1.ttf'),'--font-bold='+str(r/'fonts/font1.ttf'),'--project-root='+str(r),'--outdir='+str(pak)],check=True,stdout=subprocess.DEVNULL)
shutil.copyfile(pak/'verify-native-write.js',pak/'brick-app.js');shutil.copyfile(pak/'verify-native-write.pak',pak/'brick-app.pak');shutil.copyfile(a.host.resolve(),pak/'pocketjs-app');shutil.copymode(a.host.resolve(),pak/'pocketjs-app');shutil.copyfile(r/'fonts/NotoSansSC-Regular.otf',pak/'NotoSansSC-Regular.otf')
data=out/'sd/.userdata/shared/ai-game-workshop';(data/'projects').mkdir(parents=True);(data/'project-info').mkdir();roms=out/'sd/Roms/AI生成游戏 (P8)';roms.mkdir(parents=True)
env=dict(os.environ,POCKETJS_WORKSHOP='1',POCKETJS_HARDWARE='0',POCKETJS_DATA=str(data),POCKETJS_ROMS=str(roms),POCKETJS_FONT=str(r/'fonts/font1.ttf'))
for k in ['POCKETJS_VOICE','POCKETJS_TEST','POCKETJS_WORKSHOP_RESUME']:env.pop(k,None)
result=subprocess.run([str(pak/'pocketjs-app'),'--workshop-ready'],cwd=pak,env=env,capture_output=True,text=True,timeout=60);(out/'native.log').write_text(result.stdout+result.stderr)
message=(data/'prompt.txt').read_text();assert result.returncode==0 and message.startswith('PASS'),result.stdout+result.stderr+message;print(message)
