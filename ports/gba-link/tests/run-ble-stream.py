#!/usr/bin/env python3
"""Bounded synthetic NextUI wire benchmark over production private-FD BLE.
No ROM/save or user game controls. Refuses competing production/probe links.
"""
import argparse,hashlib,json,re,shlex,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
REMOTE='/tmp/nextui-gba-bluetooth/stream-test'

def run(args):
    args.output.mkdir(parents=True,exist_ok=True)
    binary=ROOT/'build/gba-link/bluetooth/stream.elf'
    digest=hashlib.sha256(binary.read_bytes()).hexdigest()
    base=['ssh','-C','-o','BatchMode=yes','-o','ConnectTimeout=8','-o','ServerAliveInterval=5','-o','ServerAliveCountMax=2','-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile='+str(args.known_hosts),'-i',str(args.identity)]
    peers=[base+[args.host],base+[args.join]]
    record={'passed':False,'scope':'Synthetic NextUI wire through production Bluetooth helpers over physical BLE, not commercial game acceptance','seconds':args.seconds,'payload':args.payload,'pattern':args.pattern,'helper_path':args.helper,'fixture_sha256':digest,'fixture_source_sha256':hashlib.sha256((ROOT/'ports/gba-link/tests/bluetooth-stream.c').read_bytes()).hexdigest(),'uncommitted_test_source':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip()),'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()}
    children=[];logs=[]
    try:
        for peer in peers:
            active=subprocess.check_output(peer+['pidof nextui-gba-bluetooth.elf stream.elf core-runner.elf ble-probe.elf probe.elf || true'],text=True,timeout=20).strip()
            if active:raise RuntimeError('Another Bluetooth game/probe is active; refuse test')
            sockets=subprocess.check_output(peer+['cat /proc/net/tcp /proc/net/tcp6'],text=True,timeout=20)
            for line in sockets.splitlines():
                fields=line.split()
                if len(fields)>3 and fields[3] in ('01','0A') and any(int(v.rsplit(':',1)[1],16)==55435 for v in fields[1:3]):raise RuntimeError('Production Wi-Fi link active; refuse test')
            record.setdefault('game_pids',[]).append(subprocess.check_output(peer+['pidof minarch.elf || true'],text=True,timeout=20).strip())
            record.setdefault('helper_sha256',[]).append(subprocess.check_output(peer+['sha256sum '+shlex.quote(args.helper)],text=True,timeout=20).split()[0])
            subprocess.run(peer+['mkdir -p '+REMOTE],check=True,timeout=20)
            cached=subprocess.run(peer+['sha256sum '+REMOTE+'/stream.elf'],capture_output=True,text=True,timeout=20)
            if cached.returncode or not cached.stdout.startswith(digest):
                subprocess.run(peer+['cat > '+REMOTE+'/stream.elf; chmod 700 '+REMOTE+'/stream.elf'],input=binary.read_bytes(),check=True,timeout=30)
            if subprocess.check_output(peer+['sha256sum '+REMOTE+'/stream.elf'],text=True,timeout=20).split()[0]!=digest:raise RuntimeError('Fixture checksum mismatch')
        if len(set(record['helper_sha256']))!=1:raise RuntimeError('Helpers differ across consoles')
        for index,peer in enumerate(peers):
            role='host' if index==0 else 'join'
            values=[REMOTE+'/stream.elf',args.helper,role,'' if index==0 else args.peer_bt,str(args.seconds),str(args.payload),args.pattern]
            command=' '.join(map(shlex.quote,values))+' & child=$!; trap \'kill -TERM "$child" 2>/dev/null || true\' EXIT HUP INT TERM; IFS= read -r stop || true; kill -TERM "$child" 2>/dev/null || true; wait "$child"'
            log=(args.output/(role+'.log')).open('w');logs.append(log)
            children.append(subprocess.Popen(peer+[command],stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT))
            if index==0:time.sleep(.5)
        deadline=time.monotonic()+args.seconds+90
        while time.monotonic()<deadline:
            contents=[(args.output/(role+'.log')).read_text() for role in ('host','join')]
            if any('FIXTURE_FAILED' in text for text in contents):raise RuntimeError('Fixture failed before complete measurement')
            if all('RESULT ' in text for text in contents):break
            if any(child.poll() is not None for child in children):raise RuntimeError('SSH owner exited before result')
            time.sleep(.2)
        else:raise RuntimeError('Bounded measurement deadline exceeded')
        # Fixtures already finished measuring; their SSH owner waits on stdin.
        # Let their normal targeted helper cleanup finish before ending owners.
        time.sleep(3.2)
        for child in children:child.stdin.write(b'STOP\n');child.stdin.flush();child.stdin.close()
        codes=[child.wait(timeout=15) for child in children];record['return_codes']=codes
        contents=[(args.output/(role+'.log')).read_text() for role in ('host','join')]
        record['results']=[{k:int(v) if v.isdigit() else v for k,v in re.findall(r'(\w+)=([^\s]+)',next(line for line in text.splitlines() if line.startswith('RESULT ')))} for text in contents]
        record['passed']=codes==[0,0] and all(r['passed']==1 and r['data_errors']==0 and r['final_outstanding']==0 for r in record['results']) and record['results'][0]['elapsed_ms']>=args.seconds*1000
    except (OSError,RuntimeError,subprocess.SubprocessError,StopIteration) as error:record['error']=str(error)
    finally:
        for child in children:
            if child.poll() is None:
                if child.stdin and not child.stdin.closed:
                    try:child.stdin.write(b'STOP\n');child.stdin.flush();child.stdin.close()
                    except BrokenPipeError:pass
                try:child.wait(timeout=15)
                except subprocess.TimeoutExpired:child.terminate();child.wait(timeout=10)
        for log in logs:log.close()
        record['completed_at']=int(time.time());(args.output/'results.json').write_text(json.dumps(record,indent=2)+'\n')
        print(json.dumps(record,indent=2),flush=True)
    return 0 if record['passed'] else 1

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('host','join','peer-bt'):p.add_argument('--'+name,required=True)
    for name in ('known-hosts','identity','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--helper',default='/mnt/SDCARD/.system/tg5040/bin/nextui-gba-bluetooth.elf')
    p.add_argument('--seconds',type=int,default=60);p.add_argument('--payload',type=int,default=104);p.add_argument('--pattern',choices=['padded','dense'],default='padded')
    args=p.parse_args()
    if not 1<=args.seconds<=900 or not 8<=args.payload<=65536:p.error('Invalid measurement bounds')
    if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}',args.peer_bt):p.error('Invalid peer address')
    if not args.helper.startswith('/') or '\n' in args.helper:p.error('Helper must be an absolute path')
    raise SystemExit(run(args))
