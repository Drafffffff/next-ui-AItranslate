#!/usr/bin/env python3
"""Exercise native tools, replay, isolation and failure classification without API spend."""
import copy,json,os,signal,subprocess,sys,tempfile,time
from pathlib import Path
sys.dont_write_bytecode=True
from run import HERE,BUILD,execute,scrub,assess
from report import render_report

def main():
 with tempfile.TemporaryDirectory(prefix='workshop-eval-regressions-') as temp:
  temp=Path(temp)
  cases=json.loads((HERE/'cases.json').read_text())['cases']
  special=[]
  for mode in ['repair','question','crash','long-stream','json','disconnect']:
   c=copy.deepcopy(cases[0]);c.update(id='fixture-'+mode,fixture=mode,name='Fixture '+mode)
   special.append(c)
  history=copy.deepcopy(next(c for c in cases if c['id']=='speed-change'));history.update(id='fixture-history',name='Fixture immutable history',history_fixture={'pairs':20,'user_text':'保持源码与音乐，历史仅作记忆。','assistant_text':'没有修改。'});special.append(history)
  suite=temp/'suite.json';suite.write_text(json.dumps({'schema':1,'cases':cases+special+[dict(id='fixture-multiturn',name='Fixture saved conversation',turns=[cases[0]['turns'][0],cases[0]['turns'][0]],rubric=[])]},ensure_ascii=False))
  report=temp/'baseline'
  result=subprocess.run([sys.executable,str(HERE/'run.py'),'--suite',str(suite),'--workers','4','--output',str(report)],check=False)
  assert (report/'summary.json').exists(),'native evaluation setup failed; inspect build/source diagnostics above'
  assert result.returncode==1,'intentional runtime failure must fail the run'
  baseline=json.loads((report/'summary.json').read_text());records={c['id']:c for c in baseline['cases']}
  assert baseline['complete'] and baseline['observed_parallelism']>=2
  assert len(records['fixture-multiturn']['result']['turns'])==2 and records['fixture-multiturn']['usage']['totalTokens']>records['platform-create']['usage']['totalTokens']
  assert len({c['project_id'] for c in baseline['cases']})==len(records),'projects must remain isolated'
  for c in cases:
   s=records[c['id']];assert s['machine_pass'],(c['id'],s['checks'])
   assert s['usage']['totalTokens']>0,'history/usage must survive native save/reopen'
  h=records['fixture-history']['result']['history_fixture'];assert records['fixture-history']['machine_pass'] and h['count']==40 and h['sha256_before']==h['sha256_after'] and h['count_after']>40,'saved source history must survive real tool turns unchanged'
  repair=records['fixture-repair'];assert repair['machine_pass'] and repair['tool_errors']==1,'syntax failure must be repairable'
  question=records['fixture-question'];assert question['machine_pass'] and question['result']['questions'][0]['policy']=='skip-without-new-authorization' and question['result']['questions'][0]['answer']['skipped']
  crash=records['fixture-crash'];assert not crash['machine_pass'] and not crash['runtime']['ok'] and crash['native_exit']==0,'runtime failure must not be confused with native failure'
  long=records['fixture-long-stream'];assert long['machine_pass'],long['result'].get('error')
  metrics=long['service_metrics']
  assert metrics['httpReceivedBytes']>32*1024*1024,'long SSE must exceed the previous response cap'
  assert metrics['httpReclaimedBytes']>31*1024*1024,'delivered SSE must be reclaimed'
  assert metrics['httpPeakBufferBytes']<4*1024*1024,'long SSE should not allocate its entire wire history'
  assert 0<metrics['httpMaxProgressBytes']<=65536,'each native SSE delivery must stay within the frame budget'
  assert records['fixture-json']['machine_pass'],'stream=true JSON fallback must remain compatible'
  disconnected=records['fixture-disconnect'];assert not disconnected['machine_pass'] and '中断' in disconnected['result']['error']
  assert disconnected['result']['revision']==0 and 'write_game' not in disconnected['tool_names'],'disconnect must not execute even complete-looking arguments'
  speed=records['speed-change'];assert len([e for e in speed['runtime']['events'] if e['kind']=='assert' and e['ok']])==5
  rule=next(c for c in cases if c['id']=='speed-change');project=json.loads((report/'cases'/speed['slug']/'project.json').read_text());initial=project['versions'][0]
  equivalent=project['lua'].replace('player.x+dx*1','player.x+dx').replace('player.y+dy*1','player.y+dy')
  def precise(lua):return next(x['ok'] for x in assess(rule,speed['result'],dict(project,lua=lua),None,initial) if x['name']=='仅修改指定源码位置')
  assert precise(equivalent),'explicit identity expressions must not be a false failure'
  assert not precise(equivalent.replace('player.x+dx','player.x+dx*2')),'incorrect speed must still fail'
  assert not precise(equivalent.replace('left=30','left=45')),'unrelated code changes must still fail'
  assert any(e.get('audio_peak',0)>0 for e in speed['runtime']['events']),'must capture actual emulator audio'
  assert len({c['sha256'] for c in speed['runtime']['captures']})>1,'must capture changing game frames'
  # Native P8 assert stops without throwing. A false assertion must fail the
  # evaluation even if the script executes successfully, then restore its API.
  from run import replay
  negative=temp/'strict-assert';negative.mkdir()
  cart=report/'cases'/speed['slug']/'game.p8'
  probe=replay(cart,{'replay':[{'assert':'assert(false,"deliberate false rule")','name':'must-fail'},{'assert':'assert(true)','name':'after-failure'}]},negative)
  assert not probe['ok'],'a false P8 rule must not be a false-positive pass'
  rules={e['name']:e['ok'] for e in probe['events'] if e['kind']=='assert'}
  assert rules=={'must-fail':False,'after-failure':True},rules
  candidate=temp/'candidate';addition=temp/'append.txt';addition.write_text('Evaluation variant marker. Preserve existing tools.')
  subprocess.run([sys.executable,str(HERE/'run.py'),'--workers','2','--case','speed-change','--repeat','2','--prompt-append',str(addition),'--compare',str(report/'summary.json'),'--output',str(candidate)],check=True)
  current=json.loads((candidate/'summary.json').read_text())
  assert len(current['cases'])==2 and current['baseline']['cases'] and all(c['machine_pass'] for c in current['cases'])
  assert current['cases'][0]['prompt_hashes']!=speed['prompt_hashes'],'prompt changes must be visible'
  malicious=copy.deepcopy(baseline);payload='</script><script>globalThis.injected=1</script>'
  malicious['cases'][0]['name']=payload
  html=render_report(malicious);assert payload not in html and '\\u003c/script>' in html
  attack=temp/'escaped-report.html';attack.write_text(html)
  for page in [report/'report.html',candidate/'report.html',attack]:
   subprocess.run(['node',str(HERE/'verify-report.mjs'),str(page)],check=True)
  for c in baseline['cases']:
   directory=report/'cases'/c['slug']
   for name in c.get('artifacts',[]):assert (directory/name).is_file()
   if c['runtime'].get('captures'):assert (directory/'replay/audio.wav').is_file()
  assert scrub({'key':'secret-token','trace':'a secret-token b'},['secret-token'])=={'trace':'a [REDACTED] b'}
  pidfile=temp/'child.pid'
  code='import subprocess,time,pathlib; p=subprocess.Popen(["sleep","30"]); pathlib.Path('+repr(str(pidfile))+').write_text(str(p.pid)); time.sleep(30)'
  rc,_,_,timeout=execute([sys.executable,'-c',code],1)
  assert timeout and rc==-9 and pidfile.exists()
  child=int(pidfile.read_text());time.sleep(.1)
  state=subprocess.run(['ps','-o','stat=','-p',str(child)],capture_output=True,text=True)
  assert state.returncode!=0 or state.stdout.strip().startswith('Z'),'watchdog must kill native tool children too'
  print('PASS: native parallel isolation, >32 MiB SSE reclamation, JSON fallback, disconnected tool safety, precise edits/assets, syntax repair, questions, runtime failure, screenshots/audio, comparison, escaping/redaction, process-tree watchdog')
 return 0
if __name__=='__main__':sys.exit(main())
