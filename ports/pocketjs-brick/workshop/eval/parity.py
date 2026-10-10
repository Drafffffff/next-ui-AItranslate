"""ARM/native parity probe in an isolated SD-card fixture. No production installs."""
import hashlib,json,os,re,shlex,subprocess,tarfile,tempfile,time
from pathlib import Path
from run import ARM,HERE,ROOT,SKILL,source_manifest,sha

def verify_parity(host,fixture,output,summary,case,options):
 if not re.fullmatch(r'[a-zA-Z0-9_.@-]+',host):raise ValueError('Invalid parity SSH alias')
 if summary['build']['source']!=source_manifest():raise ValueError('Source changed after Mac evaluation; rebuild and rerun before ARM comparison')
 receipt=ARM/'eval-source.json'
 if not receipt.exists() or json.loads(receipt.read_text())!=source_manifest():
  subprocess.run([str(HERE/'build.sh'),'--arm'],check=True)
  receipt.write_text(json.dumps(source_manifest()))
 bundle_key='build/pocketjs-port/mac-evaluation/brick-app.js'
 same_bundle=sha(ARM/'brick-app.js')==summary['build']['artifacts'][bundle_key]
 if not same_bundle:raise ValueError('Mac and ARM Agent JS bundles differ; do not claim environment parity')
 tag=time.strftime('%Y%m%d-%H%M%S')+'-'+os.urandom(3).hex()
 remote='/mnt/SDCARD/.userdata/shared/workshop-eval/'+tag
 # Stable request key makes the scripted-user/repair behaviour identical to the
 # corresponding Mac case. Port forwarding only transports fixture SSE.
 local=next(c for c in summary['cases'] if c['id']==case['id'])
 port=fixture.server.server_address[1]
 with tempfile.TemporaryDirectory(prefix='workshop-arm-parity-') as tmp:
  tmp=Path(tmp);data=tmp/'data';(data/'projects').mkdir(parents=True);(data/'project-info').mkdir()
  key='eval-'+local['slug']+('-'+case['fixture'] if case.get('fixture') else '')
  (data/'config.json').write_text(json.dumps({'provider':'deepseek','model':'deepseek-flash','key':key,'endpoint':fixture.endpoint,'timeoutMs':120000 if case.get('fixture')=='long-stream' else 10000}))
  spec={'turns':case['turns'],'answers':case.get('answers',[])}
  if options.prompt_file:spec['system_prompt']=Path(options.prompt_file).read_text()
  if options.prompt_append:spec['prompt_append']=Path(options.prompt_append).read_text()
  if case.get('seed'):spec['seed']={'lua':(SKILL/'assets/examples'/f'{case["seed"]}.lua').read_text(),'title':'Initial '+case['seed']}
  (data/'projects/game-eval-input.json').write_text(json.dumps(spec,ensure_ascii=False))
  package=tmp/'input.tar.gz'
  with tarfile.open(package,'w:gz') as tar:
   tar.add(ARM,arcname='pak');tar.add(data,arcname='data')
  try:
   # Restrict every remote mutation to this uniquely named fixture directory.
   with package.open('rb') as file:subprocess.run(['ssh','-T',host,'mkdir -p '+shlex.quote(remote)+' && cd '+shlex.quote(remote)+' && tar -xzf -'],stdin=file,check=True)
   env={'POCKETJS_WORKSHOP':'1','POCKETJS_HARDWARE':'0','POCKETJS_DATA':remote+'/data','POCKETJS_ROMS':remote+'/roms','SHARED_USERDATA_PATH':remote+'/shared','NEXTUI_AI_KEYS_FILE':remote+'/shared/ai-keys.txt','POCKETJS_FONT':'/mnt/SDCARD/.system/res/font1.ttf','LD_LIBRARY_PATH':'/mnt/SDCARD/.system/tg5040/lib:/usr/trimui/lib'}
   command='mkdir -p '+shlex.quote(remote+'/shared')+' '+shlex.quote(remote+'/roms')+' && cd '+shlex.quote(remote+'/pak')+' && env '+ ' '.join(k+'='+shlex.quote(v) for k,v in env.items())+' ./pocketjs-app.elf --workshop-eval'
   result=subprocess.run(['ssh','-T','-o','ExitOnForwardFailure=yes','-R',f'{port}:127.0.0.1:{port}',host,command],capture_output=True,text=True,timeout=180)
   (output/'arm-parity.log').write_text(result.stdout+'\n'+result.stderr)
   raw=subprocess.check_output(['ssh',host,'cat '+shlex.quote(remote+'/data/projects/game-eval-result.json')])
   device=json.loads(raw)
   mac=local['result']
   def tools(value):return [(e['tool'],bool(e.get('error'))) for e in value['events'] if e['type']=='tool_execution_end']
   cart=subprocess.check_output(['ssh',host,'cat '+shlex.quote(remote+'/roms/'+device['project_id']+'.p8')])
   digest=hashlib.sha256(cart).hexdigest()
   # Normalize run-local project/call IDs, but retain arguments, prompt and result
   # success paths. ROM bytes must match without any normalization.
   def args(value):
    return [(e['tool'],e.get('args',{})) for e in value['events'] if e['type']=='tool_execution_start']
   # Full prompt text is retained in the device trace; the local text is stored in
   # its per-turn snapshot by the runner.
   prompt=(output/'cases'/local['slug']/'prompt-1.txt').read_bytes()
   prompt_digest=hashlib.sha256(prompt).hexdigest()
   device_digest=hashlib.sha256(device['turns'][0]['system_prompt'].encode()).hexdigest()
   probe={'ok':result.returncode==0 and device['ok'] and tools(device)==tools(mac) and args(device)==args(mac) and digest==local['cart_sha256'] and prompt_digest==device_digest,'host':host,'case':case['id'],'remote_fixture':remote,'native_exit':result.returncode,'same_js_bundle':same_bundle,'same_tool_path':tools(device)==tools(mac),'same_tool_arguments':args(device)==args(mac),'same_system_prompt':prompt_digest==device_digest,'same_cartridge_bytes':digest==local['cart_sha256'],'cart_sha256':digest,'source_sha256':source_manifest()['sha256'],'scope':'Identical production native services, QuickJS, Pi, prompt and tools on Mac/ARM using scripted HTTP fixture; no claim of identical model sampling or CPU speed.'}
   (output/'arm-parity.json').write_text(json.dumps(probe,ensure_ascii=False,indent=2))
  finally:
   subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',host,'rm -rf '+shlex.quote(remote)],capture_output=True,timeout=15)
  return probe
