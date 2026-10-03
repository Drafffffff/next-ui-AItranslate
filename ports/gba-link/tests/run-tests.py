#!/usr/bin/env python3
"""Mac regression: real gpSP + ARM SIO ROM + actual TCP/relay, not mock packets."""
import json
import os
from pathlib import Path
import socket
import signal
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/gba-link'
COMMIT = json.loads((ROOT / 'ports/gba-link/versions.json').read_text())['gpsp_commit']
CORE = OUT / 'mac-source' / ('gpsp-' + COMMIT) / 'gpsp_libretro.dylib'
RUNNER = OUT / 'core-runner'
ROM = OUT / 'link-probe.gba'
RESULTS = []


def free_port():
    with socket.socket() as s:
        s.bind(('0.0.0.0', 0))
        return s.getsockname()[1]


def pair(name, relay=False, host_env=None, join_env=None, join_rom=ROM, join_mode='mul_poke', delay=0):
    port = free_port()
    environment = dict(os.environ)
    server = None
    if relay:
        environment['NP_ROOM'] = '731945'
        server = subprocess.Popen([sys.executable, str(ROOT / 'ports/gba-link/server/relay.py'),
            '--port', str(port), '--delay-ms', str(delay), '--chunk-bytes', '128'],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        time.sleep(.15)
    start = time.monotonic()
    files = [open(OUT / (name + '-' + role + '.log'), 'w') for role in ('host', 'join')]
    args = [str(RUNNER), str(CORE), str(ROM), 'host', '127.0.0.1', str(port), 'mul_poke', '240']
    a = subprocess.Popen(args, env=dict(environment, **(host_env or {})), stdout=files[0], stderr=subprocess.STDOUT)
    time.sleep(.2)
    args[2] = str(join_rom); args[3] = 'join'; args[6] = join_mode
    b = subprocess.Popen(args, env=dict(environment, **(join_env or {})), stdout=files[1], stderr=subprocess.STDOUT)
    try:
        codes = [a.wait(timeout=40), b.wait(timeout=40)]
    finally:
        for child in (a, b, server):
            if child and child.poll() is None:
                child.terminate()
                child.wait(timeout=5)
        for f in files: f.close()
    logs = [(OUT / (name + '-' + role + '.log')).read_text() for role in ('host', 'join')]
    return codes, logs, time.monotonic() - start


def record(name, ok, evidence):
    print(name, 'PASS' if ok else 'FAIL', flush=True)
    RESULTS.append(dict(name=name, passed=bool(ok), evidence=evidence))
    (OUT / 'test-results.json').write_text(json.dumps(RESULTS, ensure_ascii=False, indent=2) + '\n')
    if not ok: raise SystemExit('Failed: ' + name)


host_keys=OUT/'duel-host-input.txt';host_keys.write_text('100 106 256\n170 176 256\n')
join_keys=OUT/'duel-join-input.txt';join_keys.write_text('130 136 256\n')
codes,logs,elapsed=pair('duel-buttons',host_env={'NP_DUEL':'1','NP_SCRIPT':str(host_keys)},
                       join_env={'NP_DUEL':'1','NP_SCRIPT':str(join_keys)})
record('GBA key presses cross SIO and reduce peer health',codes==[0,0],logs)

codes, logs, elapsed = pair('direct')
record('two real cores over loopback TCP', codes == [0, 0], logs)
codes, logs, elapsed = pair('pause', host_env={'NP_PAUSE':'1'})
import re
paused = [int(re.search(r'paused_ticks=(\d+)', x)[1]) for x in logs]
record('both cores pause and resume', codes == [0, 0] and min(paused) >= 75 and elapsed >= 5, logs)
codes, logs, elapsed = pair('relay-delayed', relay=True, delay=25)
record('real cores through fragmented relay + 25ms forwarding delay', codes == [0, 0], logs)
changed = bytearray(ROM.read_bytes()); changed[-1] ^= 1
mismatch = OUT / 'mismatched-probe.gba'; mismatch.write_bytes(changed)
codes, logs, elapsed = pair('rom-mismatch', join_rom=mismatch)
record('ROM mismatch rejected before emulation', codes == [1, 1] and all('ROM mismatch' in x and 'frames=0' in x for x in logs), logs)
codes, logs, elapsed = pair('mode-mismatch', join_mode='rfu')
record('serial mode mismatch rejected', codes == [1, 1] and all('Link mode mismatch' in x for x in logs), logs)
codes, logs, elapsed = pair('version-mismatch', join_env={'NP_PROTOCOL':'different gpSP wire version'})
record('core protocol mismatch rejected', codes == [1, 1] and all('Core link version mismatch' in x for x in logs), logs)
codes, logs, elapsed = pair('core-build-mismatch', join_env={'NP_CORE_MISMATCH':'1'})
record('different core binary rejected', codes == [1, 1] and all('Core build mismatch' in x for x in logs), logs)


def wait_text(path, token, timeout=5):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        if path.exists() and token in path.read_text(): return
        time.sleep(.02)
    raise SystemExit('Did not observe '+token)


def broken_peer(stall=False):
    port=free_port(); prefix='peer-stall' if stall else 'peer-killed'
    paths=[OUT/(prefix+'-'+role+'.log') for role in ('host','join')]
    files=[open(p,'w') for p in paths]
    args=[str(RUNNER),str(CORE),str(ROM),'host','127.0.0.1',str(port),'mul_poke','2400']
    a=subprocess.Popen(args,stdout=files[0],stderr=subprocess.STDOUT)
    time.sleep(.15);args[3]='join'
    b=subprocess.Popen(args,stdout=files[1],stderr=subprocess.STDOUT)
    try:
        wait_text(paths[0],'state=4');time.sleep(1.5)
        if stall: b.send_signal(signal.SIGSTOP)
        else: b.kill();b.wait(timeout=5)
        a.wait(timeout=15)
    finally:
        if stall and b.poll() is None: b.send_signal(signal.SIGCONT)
        for child in (a,b):
            if child.poll() is None: child.terminate();child.wait(timeout=5)
        for f in files:f.close()
    logs=[p.read_text() for p in paths]
    expected='Peer timed out' if stall else 'Peer disconnected'
    return expected in logs[0] and 'state=5' in logs[0],logs

ok,logs=broken_peer()
record('abrupt peer termination exits session',ok,logs)
ok,logs=broken_peer(stall=True)
record('stalled peer triggers bounded heartbeat timeout',ok,logs)

port=free_port();path=OUT/'invalid-frame-host.log'
with open(path,'w') as output:
    child=subprocess.Popen([str(RUNNER),str(CORE),str(ROM),'host','127.0.0.1',str(port),'mul_poke','2400'],stdout=output,stderr=subprocess.STDOUT)
    try:
        wait_text(path,'state=1')
        with socket.create_connection(('127.0.0.1',port),timeout=3) as peer:
            hello=b''
            while len(hello)<160: hello+=peer.recv(160-len(hello))
            peer.sendall(hello[:-1]+b'\x00'+struct.pack('!II',1,65537))
            child.wait(timeout=5)
    finally:
        if child.poll() is None: child.terminate();child.wait(timeout=5)
log=path.read_text()
record('oversized frame rejected before core receive', 'Invalid link frame' in log and 'received=0' in log,[log])
def malformed_rfu(name, payload):
    port=free_port();path=OUT/(name+'-host.log')
    with open(path,'w') as output:
        child=subprocess.Popen([str(RUNNER),str(CORE),str(ROM),'host','127.0.0.1',str(port),'rfu','2400'],stdout=output,stderr=subprocess.STDOUT)
        try:
            wait_text(path,'state=1')
            with socket.create_connection(('127.0.0.1',port),timeout=3) as peer:
                hello=b''
                while len(hello)<160:hello+=peer.recv(160-len(hello))
                peer.sendall(hello[:-1]+b'\x00'+struct.pack('!II',1,len(payload))+payload)
                child.wait(timeout=5)
        finally:
            if child.poll() is None:child.terminate();child.wait(timeout=5)
    log=path.read_text()
    record(name,'Invalid gpSP RFU payload' in log and 'received=0' in log,[log])

malformed_rfu('truncated RFU broadcast blocked',b'RFU1'+struct.pack('!II',0,1))
malformed_rfu('RFU client buffer overflow blocked',b'RFU1'+struct.pack('!II',6,17<<24)+bytes(17))
malformed_rfu('RFU invalid assigned slot blocked',b'RFU1'+struct.pack('!II',2,4<<16)+bytes(4))
print('All completed checks passed; commercial gameplay, true packet loss and real WAN need separate evidence.')
