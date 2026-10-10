"""Explicit objective criteria. Never substitute static criteria for gameplay."""
import re

EXPECTED_KEYS={'unchanged_sprites','unchanged_audio','precise_replace','precise_replace_alternatives','operation','preserve_between_turns','design_required','only_music_calls_changed','music_speed_clone','unchanged_lua','title','question_required','final_reply_max_chars','compaction_required','history_preserved'}
DESIGN_FIELDS=['core_action','goal','risk','controls','scenes','audiovisual']

def validate_expect(case):
 # Reject driver mistakes before loading credentials or paying for a model run.
 for step in case.get('replay',[]):
  if not isinstance(step,dict) or set(step)-{'assert','reload','frames','mask','name'}:raise ValueError('Unknown replay fields; frame input uses mask, not buttons')
  if 'assert' in step:
   if not isinstance(step['assert'],str) or not step['assert'].strip() or 'frames' in step or 'reload' in step:raise ValueError('Invalid replay assertion')
  elif step.get('reload'):
   if 'frames' in step or 'mask' in step:raise ValueError('Invalid replay reload')
  elif type(step.get('frames')) is not int or not 1<=step['frames']<=36000 or type(step.get('mask')) is not int or not 0<=step['mask']<=63:raise ValueError('Replay requires bounded frames and integer mask (0..63)')
  if not step.get('reload') and not re.fullmatch(r'[a-z0-9-_]{1,40}',step.get('name','')):raise ValueError('Invalid replay name')
 e=case.get('expect',{})
 if not isinstance(e,dict) or set(e)-EXPECTED_KEYS:raise ValueError('Unknown objective criteria: '+str(set(e)-EXPECTED_KEYS if isinstance(e,dict) else e))
 if e.get('operation','mutate') not in ['mutate','read_only','metadata']:raise ValueError('Unknown operation expectation')
 if e.get('preserve_between_turns') and (len(case['turns'])<2 or any(x not in ['gfx','audio'] for x in e['preserve_between_turns'])):raise ValueError('Invalid between-turn preservation rule')
 if 'final_reply_max_chars' in e and (type(e['final_reply_max_chars']) is not int or e['final_reply_max_chars']<1):raise ValueError('Invalid reply length criterion')
 if e.get('music_speed_clone'):
  m=e['music_speed_clone']
  if set(m)!={'original_pattern','speed'} or not all(type(x) is int for x in m.values()) or not 1<=m['speed']<=255:raise ValueError('Invalid music clone criterion')

def additional(case,result,project,initial):
 e=case.get('expect',{});checks=[]
 def add(name,ok,reason=''):checks.append({'name':name,'ok':bool(ok),'reason':reason})
 if e.get('final_reply_max_chars'):
  final=next((m for m in reversed(project.get('messages',[])) if m.get('role')=='assistant'),{})
  text=''.join(c.get('text','') for c in final.get('content',[]) if c.get('type')=='text')
  add('最终回复长度符合简短请求',bool(text.strip()) and len(text)<=e['final_reply_max_chars'],str(len(text))+' characters; limit '+str(e['final_reply_max_chars']))
 if e.get('compaction_required'):add('真实模型摘要已持久化',bool(project.get('compactions')) and all(r.get('summary','').strip() and r.get('firstKept',0)>0 for r in project['compactions']))
 if e.get('history_preserved'):
  h=result.get('history_fixture',{});add('压缩前原始历史逐字保留',h.get('kind')=='synthetic-stress-history' and bool(h.get('sha256_before')) and h.get('sha256_before')==h.get('sha256_after') and h.get('count_after',0)>=h.get('count',1))
 if e.get('unchanged_lua'):add('原Lua保持不变',project.get('lua')==initial.get('lua'))
 if 'title' in e:add('标题符合请求',project.get('title')==e['title'])
 if e.get('question_required'):add('等待实际提问回答',bool(result.get('questions')) and all(q.get('answer') for q in result['questions']))
 if e.get('design_required'):
  d=project.get('design',{}) or {}
  add('完整非空设计记录',all(isinstance(d.get(k),str) and d[k].strip() for k in DESIGN_FIELDS) and all(isinstance(d.get(k),list) for k in ['preserve','unresolved']))
 if e.get('preserve_between_turns'):
  turns=result.get('turns',[]);versions={v['revision']:v for v in project.get('versions',[])}
  for key in e['preserve_between_turns']:
   pairs=[(versions.get(t['before']),versions.get(t['revision'])) for t in turns[1:]]
   add('连续修改保留 '+key,bool(pairs) and all(a and b and a.get(key)==b.get(key) for a,b in pairs),'以实际每次发布版本比较；不以最终草稿自证保留')
 if e.get('only_music_calls_changed'):
  def normal(lua):return re.sub(r'\b(music\s*\(\s*)\d+(?=\s*[,\)])',r'\1<track>',lua)
  add('仅必要music编号变化',normal(project.get('lua',''))==normal(initial.get('lua','')))
 if e.get('music_speed_clone'):
  rule=e['music_speed_clone'];old=initial.get('audio',{});new=project.get('audio',{})
  os={s['id']:s for s in old.get('sfx',[])};ns={s['id']:s for s in new.get('sfx',[])}
  op={p['id']:p for p in old.get('music',[])};np={p['id']:p for p in new.get('music',[])}
  pattern=op.get(rule['original_pattern']);calls=set(int(n) for n in re.findall(r'\bmusic\s*\(\s*(\d+)',project.get('lua','')))
  expected=[os.get(i) for i in (pattern or {}).get('channels',[]) if i>=0]
  def clone_matches(pid):
   p=np.get(pid)
   if not p or not pattern or any(p.get(k)!=pattern.get(k) for k in ['loop_start','loop_end','stop']):return False
   actual=[ns.get(i) for i in p.get('channels',[]) if i>=0]
   return len(actual)==len(expected) and all(a and b and a.get('speed')==rule['speed'] and all(a.get(k)==b.get(k) for k in ['notes','loop_start','loop_end']) for a,b in zip(actual,expected))
  add('BGM音符/包络/循环保留且时长正确',bool(expected) and bool(calls) and all(clone_matches(i) for i in calls),'比较实际所有播放曲目的音符、音量、效果、有效长度和循环；不评价听感')
  add('原有音效和曲目完整保留',all(ns.get(i)==s for i,s in os.items()) and all(np.get(i)==p for i,p in op.items()))
 return checks
