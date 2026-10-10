#!/usr/bin/env python3
"""Exercise the real bounded interpreter; never invokes user scripts in a host shell."""
import argparse,json,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--binary',required=True);p.add_argument('--ssh');p.add_argument('--output',type=Path);args=p.parse_args()
command=['ssh',args.ssh,args.binary] if args.ssh else [args.binary]
cases=[('snapshot','print(readText("game.lua"));writeText("test.json","ok");print(listFiles().join(","))',0,'test.json'),('system-snapshot','print(readText("system/meminfo"))',0,'MemTotal:'),('capabilities','print(typeof process,typeof require,typeof fetch,typeof std,typeof os,typeof WebAssembly)',0,'undefined undefined undefined undefined undefined undefined'),('traversal','readText("../../config.json")',1,'Error:'),('system','readText("/mnt/SDCARD/.userdata/shared/ai-keys.txt")',1,'Error:'),('network','fetch("https://example.com")',1,'not defined'),('process','require("child_process")',1,'not defined'),('loop','while(true){}',1,'interrupted'),('heap','const a=[];while(true)a.push(new Array(100000).fill(1))',1,''),('output','for(let i=0;i<20000;i++) print("abcdef")',0,'已截断')]
results=[]
for name,script,code,needle in cases:
 start=time.monotonic();r=subprocess.run(command,input=json.dumps({'lua':'function _init() end','script':script,'files':{'system/meminfo':'MemTotal: 998332 kB'}}).encode(),capture_output=True,timeout=8)
 text=r.stdout.decode();assert r.returncode==code,(name,r.returncode,text,r.stderr);assert needle in text,(name,text);assert len(r.stdout)<16600
 results.append({'case':name,'exit':r.returncode,'ms':round((time.monotonic()-start)*1000),'bytes':len(r.stdout)})
if args.output:args.output.write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2));print('PASS interpreter capabilities, snapshot access, denied host files/network/processes, CPU/heap/output budgets.')
