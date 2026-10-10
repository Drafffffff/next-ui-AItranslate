#!/usr/bin/env python3
"""Capture a private build input snapshot when other tasks edit the workspace."""
import argparse,datetime,json,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
from run import ROOT,HERE,source_manifest,atomic

def clone(source,target):
 target.parent.mkdir(parents=True,exist_ok=True)
 # APFS copy-on-write clone: independent files, not hard links or symlinks to
 # mutable source/build directories. Copy only declared public build inputs.
 subprocess.run(['/bin/cp','-cR',str(source),str(target)],check=True)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output');p.add_argument('--build',action='store_true');a=p.parse_args()
 stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
 target=Path(a.output).resolve() if a.output else ROOT/'build/workshop-eval/snapshots'/stamp
 if target.exists():raise ValueError('Snapshot directory must be new')
 if sys.platform!='darwin':raise ValueError('Source snapshot clone currently requires Mac/APFS')
 before=source_manifest();target.mkdir(parents=True)
 for relative in ['ports/pocketjs-brick','skills/pico8-game-dev','fonts','workspace/all/common','workspace/tg5040/cores/patches','LICENSE']:
  clone(ROOT/relative,target/relative)
 after=source_manifest()
 if before!=after:raise ValueError('Agent source changed during the snapshot; capture a fresh snapshot before building')
 # No old application data, keys, ROMs, audio recordings or test projects are
 # copied from build/. Only compiler caches and pinned public runtime sources.
 for relative in ['upstream','quickjs','bun','mac-toolchain','mac-target','rustup','cargo','rust-app-target','rust-target']:
  source=ROOT/'build/pocketjs-port'/relative
  if source.exists():clone(source,target/'build/pocketjs-port'/relative)
 clone(ROOT/'build/workshop-fake08',target/'build/workshop-fake08')
 atomic(target/'evaluation-snapshot.json',json.dumps({'source':before,'captured_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'original_workspace':str(ROOT)},indent=2))
 # Validate the copied source using the copied evaluator before compiling.
 snapshot_eval=target/'ports/pocketjs-brick/workshop/eval'
 subprocess.run([sys.executable,str(snapshot_eval/'run.py'),'--snapshot-source',str(target/'copied-source.json')],check=True)
 if json.loads((target/'copied-source.json').read_text())!=before:raise ValueError('Copied inputs do not match captured source hashes')
 print('Source snapshot: '+str(target),flush=True)
 if a.build:
  subprocess.run(['bash',str(snapshot_eval/'build.sh')],check=True)
  built=json.loads((target/'build/workshop-eval/build-receipt.json').read_text())
  atomic(ROOT/'build/workshop-eval/prepared-snapshot.json',json.dumps({'root':str(target),'source_sha256':built['source']['sha256']}))
 print('Evaluate with: python3 '+str(snapshot_eval/'run.py')+' --workers 4',flush=True)
if __name__=='__main__':main()
