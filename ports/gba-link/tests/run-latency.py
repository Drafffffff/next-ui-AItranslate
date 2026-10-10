#!/usr/bin/env python3
"""Actual transport wakeups, ordered reply batching and bounded RTT telemetry."""
import json
from pathlib import Path
import re
import subprocess
import os
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/gba-link/latency-fix'
OUT.mkdir(parents=True,exist_ok=True)
binary=OUT/'netplay-latency-test'
subprocess.run(['clang','-std=c99','-O1','-Wall','-Wextra','-Werror',
 '-fsanitize=address,undefined','-I'+str(ROOT/'workspace/all/minarch'),
 '-I'+str(ROOT/'workspace/all/minarch/libretro-common/include'),
 str(ROOT/'ports/gba-link/tests/netplay-latency.c'),'-o',str(binary)],check=True)
results=[]
for mode in ('sleep','network-wait'):
 env=dict(os.environ);env.pop('NP_BASELINE_WAIT',None)
 if mode=='sleep':env['NP_BASELINE_WAIT']='1'
 proc=subprocess.run([str(binary)],env=env,capture_output=True,text=True)
 log=proc.stdout+proc.stderr;(OUT/('transport-'+mode+'.log')).write_text(log)
 print(log,end='')
 assert proc.returncode==0,log
 latency=int(re.search(r'pacing reply latency_us=(\d+)',log)[1])
 results.append({'mode':mode,'reply_latency_us':latency,'checks_passed':log.count('PASS:')})
# This is the simulated frontend pacing wait, not a Wi-Fi/handheld benchmark.
assert results[1]['reply_latency_us']<results[0]['reply_latency_us'],results
(OUT/'transport-results.json').write_text(json.dumps({'sanitizers':'ASan/UBSan','results':results},indent=2)+'\n')
