#!/usr/bin/env python3
"""Finite owned native gpSP hotspot probe; consoles restore Wi-Fi autonomously.
No user game controls, saves, system supplicant restart or saved Wi-Fi changes.
"""
import argparse,hashlib,json,re,shlex,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];REMOTE='/tmp/nextui-gba-hotspot-test/core-test'

def run(args):
 args.output.mkdir(parents=True,exist_ok=True)
 base=['ssh','-C','-o','BatchMode=yes','-o','ConnectTimeout=5','-o','ServerAliveInterval=3','-o','ServerAliveCountMax=2','-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile='+str(args.known_hosts),'-i',str(args.identity)]
 peers=[args.host,args.join];roles=['host','join']
 artifacts={'core-runner.elf':ROOT/'build/gba-link/bluetooth/core-runner-hotspot-arm.elf','nextui-gba-bluetooth.elf':ROOT/'build/gba-link/bluetooth/hotspot-helper-arm.elf','gpsp_libretro.so':ROOT/'build/gba-link/brick/gpsp_libretro.so','probe.gba':ROOT/'build/gba-link/link-probe.gba'}
 if args.case=='rom-mismatch':
  changed=args.output/'probe-other.gba';raw=bytearray(artifacts['probe.gba'].read_bytes());raw[0xa0]^=1;raw[0xbd]=(-sum(raw[0xa0:0xbd])-0x19)&255;changed.write_bytes(raw);artifacts['probe-other.gba']=changed
 hashes={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in artifacts.items()}
 record={'passed':False,'scope':'Real ARM gpSP/core frontend over dedicated physical hotspot; open-source SIO probe, not Mario Tennis UI/game acceptance','case':args.case,'requested_frame_bound':args.frames,'hashes':hashes,'revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty_source':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip())}
 def call(peer,command,data=None,timeout=30):
  r=subprocess.run(base+[peer,command],input=data,capture_output=True,timeout=timeout)
  if r.returncode:raise RuntimeError(peer+': '+r.stderr.decode()+r.stdout.decode())
  return r.stdout.decode()
 try:
  for peer in peers:
   active=call(peer,'pidof nextui-gba-bluetooth.elf core-runner.elf hostapd stream.elf ble-probe.elf || true').strip()
   if active:raise RuntimeError('Existing game/probe radio is active; preserve it')
   sockets=call(peer,'cat /proc/net/tcp /proc/net/tcp6')
   for line in sockets.splitlines():
    fields=line.split()
    if len(fields)>3 and fields[3] in ('01','0A') and any(int(v.rsplit(':',1)[1],16)==55435 for v in fields[1:3]):raise RuntimeError('Production Wi-Fi game link is active')
   record.setdefault('config_before',[]).append(call(peer,'sha256sum /etc/wifi/wpa_supplicant.conf').split()[0])
   record.setdefault('offline_game_pids',[]).append(call(peer,'pidof minarch.elf || true').strip())
   call(peer,'mkdir -p '+REMOTE)
   for name,path in artifacts.items():
    target=REMOTE+'/'+name;current=call(peer,'if [ -f '+shlex.quote(target)+' ]; then sha256sum '+shlex.quote(target)+'; fi').split()
    if current and current[0]==hashes[name]:continue
    if name=='gpsp_libretro.so':
     installed='/mnt/SDCARD/.system/tg5040/cores/gpsp_libretro.so'
     if call(peer,'sha256sum '+installed).split()[0]==hashes[name]:call(peer,'cp '+installed+' '+target);continue
    call(peer,'cat > '+shlex.quote(target)+'; chmod 700 '+shlex.quote(target),path.read_bytes())
    if call(peer,'sha256sum '+shlex.quote(target)).split()[0]!=hashes[name]:raise RuntimeError('Staged artifact hash mismatch')
  # Stage everything before the first dedicated radio leaves the router.
  for peer,role in zip(peers,roles):
   log=REMOTE+'/'+args.case+'-'+role+'.log';rc=log+'.rc'
   keys=b'100 106 256\n170 176 256\n' if role=='host' else b'130 136 256\n'
   call(peer,'rm -f '+shlex.quote(log)+' '+shlex.quote(rc))
   call(peer,'cat > '+REMOTE+'/keys',keys)
   env={'NP_HP_PEER':'' if role=='host' else args.peer_bssid,'NP_DUEL':'1','NP_DYNAREC':'1','NP_SCRIPT':REMOTE+'/keys','NP_REALTIME':'1','NP_NETWORK_WAIT':'1'}
   if role=='join':env['NP_HP_DISCOVER']='1'
   if args.case=='core-mismatch' and role=='join':env['NP_CORE_MISMATCH']='1'
   rom=REMOTE+('/probe-other.gba' if args.case=='rom-mismatch' and role=='join' else '/probe.gba')
   serial='rfu' if args.case=='mode-mismatch' and role=='join' else 'mul_poke'
   command=' '.join(k+'='+shlex.quote(v) for k,v in env.items())+' '+REMOTE+'/core-runner.elf '+REMOTE+'/gpsp_libretro.so '+rom+' '+role+' 127.0.0.1 0 '+serial+' '+str(args.frames)
   # HUP is ignored only by our finite fixture. C frame/wait bounds plus the
   # helper's local FD heartbeat handle the intentional loss of SSH routing.
   shell='trap \'\' HUP; ( '+command+' > '+shlex.quote(log)+' 2>&1; code=$?; printf "%s\\n" "$code" > '+shlex.quote(rc)+' ) < /dev/null > /dev/null 2>&1 &'
   call(peer,shell,timeout=10)
   print(role,'finite core probe started',flush=True)
  deadline=time.monotonic()+args.frames/60+75
  complete={}
  while time.monotonic()<deadline:
   for peer,role in zip(peers,roles):
    if role in complete:continue
    log=REMOTE+'/'+args.case+'-'+role+'.log'
    try:
     r=subprocess.run(base+[peer,'if [ -f '+shlex.quote(log+'.rc')+' ]; then cat '+shlex.quote(log+'.rc')+'; cat '+shlex.quote(log)+'; fi'],capture_output=True,text=True,timeout=9)
     if r.returncode==0 and r.stdout:
      code,body=r.stdout.split('\n',1);complete[role]={'code':int(code),'log':body};(args.output/(role+'.log')).write_text(body)
    except (subprocess.SubprocessError,ValueError):pass
   if len(complete)==2:break
   time.sleep(1)
  record['roles']=complete
  if len(complete)!=2:raise RuntimeError('Bounded core result/reconnection deadline exceeded; inspect autonomous cleanup')
  for peer in peers:
   record.setdefault('config_after',[]).append(call(peer,'sha256sum /etc/wifi/wpa_supplicant.conf').split()[0])
   state=call(peer,'wpa_cli -p /etc/wifi/sockets -i wlan0 status; ip -4 addr show wlan1; pidof nextui-gba-bluetooth.elf hostapd || true');record.setdefault('restored',[]).append(state)
  if args.case=='normal':good=all(r['code']==0 and (lambda m: bool(m) and int(m.group(1)) >= args.frames)(re.search(r'RESULT frames=(\d+)',r['log'])) and 'DUEL local_hp=' in r['log'] and 'HOTSPOT_RESTORED session_resources=true' in r['log'] for r in complete.values())
  else:
   reason={'core-mismatch':'Core build mismatch','rom-mismatch':'ROM mismatch','mode-mismatch':'Link mode mismatch'}[args.case]
   good=all(r['code']==1 and reason in r['log'] and 'RESULT frames=0' in r['log'] and 'HOTSPOT_RESTORED session_resources=true' in r['log'] for r in complete.values())
  record['passed']=good and record['config_before']==record['config_after'] and all('wpa_state=COMPLETED' in s and 'inet 192.168.241.' not in s for s in record['restored'])
 except (OSError,RuntimeError,subprocess.SubprocessError) as error:record['error']=str(error)
 record['completed_at']=int(time.time());(args.output/'results.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2),flush=True);return 0 if record['passed'] else 1

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('host','join','peer-bssid'):p.add_argument('--'+n,required=True)
 for n in ('known-hosts','identity','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--frames',type=int,default=240)
 p.add_argument('--case',choices=['normal','core-mismatch','rom-mismatch','mode-mismatch'],default='normal')
 args=p.parse_args()
 if not 240<=args.frames<=36000:p.error('Frame count must be 240..36000')
 if args.case!='normal' and args.frames!=240:p.error('Mismatch checks run no emulated frames; use default frame bound')
 if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}',args.peer_bssid):p.error('Invalid BSSID')
 raise SystemExit(run(args))
