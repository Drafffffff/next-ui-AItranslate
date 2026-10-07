#!/usr/bin/env python3
"""Real Brick gpSP probe ROM over production frontend/private-FD BLE path.
Never opens a handheld game, alters saves, or replaces installed programs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import time

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/gba-link'
REMOTE='/tmp/nextui-gba-bluetooth/core-test'


def run(args):
    args.output.mkdir(parents=True,exist_ok=True)
    base=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8','-o','ServerAliveInterval=5','-o','ServerAliveCountMax=2','-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile='+str(args.known_hosts),'-i',str(args.identity)]
    peers=[base+[args.host],base+[args.join]]
    artifacts={'core-runner.elf':OUT/'bluetooth/core-runner-bluetooth-arm.elf',
               'nextui-gba-bluetooth.elf':OUT/'brick/nextui-gba-bluetooth.elf',
               'gpsp_libretro.so':OUT/'brick/gpsp_libretro.so','probe.gba':OUT/'link-probe.gba'}
    hashes={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in artifacts.items()}
    record={'passed':False,'scope':'Real ARM gpSP and production frontend BLE helper over physical radio; open-source SIO test ROM, not commercial game/UI acceptance','case':args.case,'hashes':hashes,'uncommitted_test_source':bool(subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,text=True).strip()),'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()}
    children=[];logs=[]
    try:
        for peer in peers:
            pids=subprocess.check_output(peer+['pidof minarch.elf || true'],text=True,timeout=20).strip()
            record.setdefault('game_pids',[]).append(pids)
            sockets=subprocess.check_output(peer+['cat /proc/net/tcp /proc/net/tcp6'],text=True,timeout=20)
            for line in sockets.splitlines():
                fields=line.split()
                if len(fields)>3 and fields[3] in ('01','0A') and any(int(v.rsplit(':',1)[1],16)==55435 for v in fields[1:3]):raise RuntimeError('Production link active; refuse radio test')
            active=subprocess.check_output(peer+['pidof core-runner.elf nextui-gba-bluetooth.elf ble-probe.elf probe.elf || true'],text=True,timeout=20).strip()
            if active:raise RuntimeError('Another radio/core helper is active; refuse test')
            subprocess.run(peer+['mkdir -p '+REMOTE+'; rm -f '+REMOTE+'/stop'],check=True,timeout=20)
            for name,path in artifacts.items():
                cached=subprocess.run(peer+['sha256sum '+REMOTE+'/'+name],capture_output=True,text=True,timeout=20)
                digest=cached.stdout.split()[0] if cached.returncode==0 and cached.stdout.split() else ''
                if digest!=hashes[name]:
                    with path.open('rb') as source:
                        subprocess.run(peer+['cat > '+REMOTE+'/'+name+'; chmod 700 '+REMOTE+'/'+name],stdin=source,check=True,timeout=45)
                    digest=subprocess.check_output(peer+['sha256sum '+REMOTE+'/'+name],text=True,timeout=20).split()[0]
                if digest!=hashes[name]:raise RuntimeError('Remote artifact checksum mismatch')
        for index,peer in enumerate(peers):
            role='host' if index==0 else 'join'
            keys='100 106 256\n170 176 256\n' if index==0 else '130 136 256\n'
            subprocess.run(peer+['cat > '+REMOTE+'/keys'],input=keys,text=True,check=True,timeout=20)
            serial='rfu' if index==1 and args.case=='mode-mismatch' else 'mul_poke'
            rom=REMOTE+'/probe.gba'
            if index==1 and args.case=='rom-mismatch':
                changed=bytearray(artifacts['probe.gba'].read_bytes());changed[-1]^=1
                subprocess.run(peer+['cat > '+REMOTE+'/different.gba'],input=changed,check=True,timeout=20);rom=REMOTE+'/different.gba'
            environment={'NP_BT_PEER':'' if index==0 else args.peer_bt,'NP_DYNAREC':'1','NP_DUEL':'1','NP_SCRIPT':REMOTE+'/keys','NP_STOP_FILE':REMOTE+'/stop'}
            if index==1:environment['NP_BT_DISCOVER']='1'
            if index==1 and args.case=='core-mismatch':environment['NP_CORE_MISMATCH']='1'
            command=' '.join(k+'='+shlex.quote(v) for k,v in environment.items())+' '+REMOTE+'/core-runner.elf '+REMOTE+'/gpsp_libretro.so '+rom+' '+role+' 127.0.0.1 0 '+serial+' 240'
            # This SSH stdin owns only this runner. Closing it cleans up this
            # child; closing the runner's private FD cancels its BLE worker.
            command+=' & child=$!; trap \'kill -TERM "$child" 2>/dev/null || true\' EXIT HUP INT TERM; IFS= read -r finish || true; touch '+REMOTE+'/stop; wait "$child"'
            log=(args.output/(role+'.log')).open('w');logs.append(log)
            children.append(subprocess.Popen(peer+[command],stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT))
            if index==0:time.sleep(.6)
        deadline=time.monotonic()+args.timeout
        while time.monotonic()<deadline:
            contents=[(args.output/(role+'.log')).read_text() for role in ('host','join')]
            if all('RESULT frames=' in content for content in contents):break
            if any(child.poll() is not None for child in children):raise RuntimeError('SSH owner exited before core result')
            time.sleep(.1)
        else:raise RuntimeError('Core BLE test exceeded bounded observation time')
        for child in children:
            child.stdin.write(b'STOP\n');child.stdin.flush();child.stdin.close()
        codes=[child.wait(timeout=15) for child in children]
        record['return_codes']=codes
        contents=[(args.output/(role+'.log')).read_text() for role in ('host','join')]
        if args.case=='normal':record['passed']=codes==[0,0] and all('state=4 Link connected' in c for c in contents) and 'DISCOVERED_ROOM ' in contents[1]
        else:
            expected={'rom-mismatch':'ROM mismatch','core-mismatch':'Core build mismatch','mode-mismatch':'Link mode mismatch'}[args.case]
            record['passed']=codes==[1,1] and all(expected in c and 'RESULT frames=0' in c for c in contents)
    except (OSError,RuntimeError,subprocess.SubprocessError) as error:
        record['error']=str(error)
    finally:
        for child in children:
            if child.poll() is None:
                if child.stdin and not child.stdin.closed:
                    try:child.stdin.write(b'STOP\n');child.stdin.flush();child.stdin.close()
                    except BrokenPipeError:pass
                try:child.wait(timeout=15)
                except subprocess.TimeoutExpired:child.terminate();child.wait(timeout=10)
        for log in logs:log.close()
        for role in ('host','join'):
            path=args.output/(role+'.log')
            if path.exists():record[role+'_log']=path.read_text()
        record['completed_at']=int(time.time())
        (args.output/'results.json').write_text(json.dumps(record,indent=2)+'\n')
        print(json.dumps(record,indent=2),flush=True)
    return 0 if record['passed'] else 1


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',required=True);p.add_argument('--join',required=True);p.add_argument('--peer-bt',required=True)
    p.add_argument('--known-hosts',type=Path,required=True);p.add_argument('--identity',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--case',choices=['normal','rom-mismatch','core-mismatch','mode-mismatch'],default='normal');p.add_argument('--timeout',type=int,default=90)
    args=p.parse_args()
    if not 30<=args.timeout<=180:p.error('timeout must be 30..180 seconds')
    import re
    if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}',args.peer_bt):p.error('Invalid peer address')
    raise SystemExit(run(args))
