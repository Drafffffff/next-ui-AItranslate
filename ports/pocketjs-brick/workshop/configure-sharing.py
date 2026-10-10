#!/usr/bin/env python3
"""Merge only share endpoint/key into device settings; preserve other fields.
Secrets are passed through SSH stdin and never printed or stored in reports."""
import pathlib,subprocess,json,hashlib,datetime,shlex,argparse,urllib.parse
def share_endpoint(value):
 try:
  url=urllib.parse.urlsplit(value)
  valid=url.scheme=='https' and bool(url.hostname) and url.username is None and url.password is None and url.path in ('','/') and not url.query and not url.fragment and not any(c.isspace() for c in value)
  if url.port is not None and not 1<=url.port<=65535:valid=False
 except ValueError:valid=False
 if not valid:raise argparse.ArgumentTypeError('endpoint must be an HTTPS origin without credentials, path, query or fragment')
 return value.rstrip('/')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--endpoint',type=share_endpoint,default='https://games.dong-xi.cn')
args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parents[3];key=(root/'build/workshop-sharing-upload-key').read_text().strip();assert len(key)>=32
file='/mnt/SDCARD/.userdata/shared/ai-game-workshop/config.json';raw=subprocess.check_output(['ssh','-o','ConnectTimeout=8','nextui-brick','cat '+shlex.quote(file)]);config=json.loads(raw)
config.update(shareEndpoint=args.endpoint,shareKey=key);data=json.dumps(config,ensure_ascii=False).encode();expected=hashlib.sha256(raw).hexdigest()
stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S');backup='/mnt/SDCARD/.userdata/shared/ai-game-workshop-backups/'+stamp+'-sharing-config.json'
subprocess.run(['ssh','-T','nextui-brick','umask 077; cat > '+shlex.quote(file+'.sharing-new')],input=data,check=True)
script='\n'.join(['set -eu',f'test "$(sha256sum {shlex.quote(file)} | cut -d " " -f 1)" = {expected}',f'cp {shlex.quote(file)} {shlex.quote(backup)}','sync',f'mv -f {shlex.quote(file+".sharing-new")} {shlex.quote(file)}','sync'])
subprocess.run(['ssh','-T','nextui-brick','sh -s'],input=script.encode(),check=True)
check=subprocess.check_output(['ssh','nextui-brick','sha256sum '+shlex.quote(file)],text=True).split()[0];assert check==hashlib.sha256(data).hexdigest()
print('PASS sharing config merged and SHA verified; all model/ASR settings preserved; backup '+backup)
