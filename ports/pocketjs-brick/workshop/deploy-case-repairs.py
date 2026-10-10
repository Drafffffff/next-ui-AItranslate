#!/usr/bin/env python3
"""Deploy prepared case repairs with SHA guards and durable publication intent."""
import json,pathlib,subprocess,tarfile,io,datetime,shlex,sys
root=pathlib.Path(__file__).resolve().parents[3];base=pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'build/workshop-case-repairs';m=json.loads((base/'deployment-manifest.json').read_text())
stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S');stage='/mnt/SDCARD/.userdata/shared/ai-game-workshop-backups/'+stamp+'-case-repairs'
for r in m:
 assert r['id'].startswith('game-') and all(c.isalnum() or c=='-' for c in r['id'])
subprocess.run(['ssh','nextui-brick','mkdir -p '+shlex.quote(stage)],check=True)
buffer=io.BytesIO()
with tarfile.open(fileobj=buffer,mode='w:gz') as t:
 for r in m:
  for suffix in ['pending.json','final.json','info.json','game.p8']:t.add(base/(r['id']+'.'+suffix),arcname=r['id']+'.'+suffix)
 t.add(base/'deployment-manifest.json',arcname='deployment-manifest.json')
subprocess.run(['ssh','-T','nextui-brick','cd '+shlex.quote(stage)+' && tar -xzf -'],input=buffer.getvalue(),check=True)
lines=['set -eu','stage='+shlex.quote(stage),'data=/mnt/SDCARD/.userdata/shared/ai-game-workshop',"roms='/mnt/SDCARD/Roms/AI生成游戏 (P8)'",'mkdir -p "$stage/originals"']
# Guard every source and ROM before changing any project.
for r in m:
 id=r['id']
 lines += [f'test "$(sha256sum "$data/projects/{id}.json" | cut -d " " -f 1)" = {r["originalHash"]}',f'test "$(sha256sum "$roms/{id}.p8" | cut -d " " -f 1)" = {r["originalCartHash"]}']
for r in m:
 id=r['id']
 lines += [f'cp "$data/projects/{id}.json" "$stage/originals/{id}.json"',f'cp "$roms/{id}.p8" "$stage/originals/{id}.p8"',f'if [ -f "$data/project-info/{id}.json" ]; then cp "$data/project-info/{id}.json" "$stage/originals/{id}.info.json"; fi']
lines += ['sync']
for r in m:
 id=r['id']
 # Crash between steps is recovered by production recoverProject(p.pending).
 lines += [f'cp "$stage/{id}.pending.json" "$data/projects/{id}.json.new"','sync',f'mv -f "$data/projects/{id}.json.new" "$data/projects/{id}.json"','sync',f'cp "$stage/{id}.game.p8" "$roms/{id}.p8.new"','sync',f'mv -f "$roms/{id}.p8.new" "$roms/{id}.p8"',f'rm -f "$data/previews/{id}.png" "$roms/.media/{id}.png"',f'cp "$stage/{id}.final.json" "$data/projects/{id}.json.new"','sync',f'mv -f "$data/projects/{id}.json.new" "$data/projects/{id}.json"',f'cp "$stage/{id}.info.json" "$data/project-info/{id}.json.new"',f'mv -f "$data/project-info/{id}.json.new" "$data/project-info/{id}.json"','sync',f'test "$(sha256sum "$data/projects/{id}.json" | cut -d " " -f 1)" = {r["finalHash"]}',f'test "$(sha256sum "$roms/{id}.p8" | cut -d " " -f 1)" = {r["cartHash"]}']
result=subprocess.run(['ssh','-T','nextui-brick','sh -s'],input='\n'.join(lines)+'\n',text=True,capture_output=True);assert result.returncode==0,result.stdout+result.stderr
report={'backup':stage,'projects':m,'verified':True,'protocol':'all-input SHA guards; original backups; durable pending intent; atomic ROM/project renames; final SHA verification'}
(base/'deployed.json').write_text(json.dumps(report,ensure_ascii=False,indent=2));print(json.dumps(report,ensure_ascii=False,indent=2))
