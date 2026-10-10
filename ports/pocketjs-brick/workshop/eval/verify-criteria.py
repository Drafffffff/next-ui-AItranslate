"""Negative controls for objective assessment; no model calls or game execution."""
import copy
import json
from pathlib import Path
from criteria import additional,validate_expect
from run import assess

for name in ['cases.json','iteration-cases.json']:
 for case in json.loads(Path(__file__).with_name(name).read_text())['cases']:validate_expect(case)
for step in [{'frames':30,'buttons':0,'name':'scene'},{'frames':30,'name':'scene'},{'frames':1,'mask':64,'name':'scene'},{'frames':True,'mask':0,'name':'scene'}]:
 try:validate_expect({'replay':[step]})
 except ValueError:pass
 else:raise AssertionError('Invalid replay must fail before model execution')

def rule(**expect):return {'turns':['explicit fixture'], 'expect':expect}
def verdict(c,result,project,initial):return {x['name']:x['ok'] for x in assess(c,result,project,None,initial)}
initial={'revision':1,'lua':'music(6,200,3) sfx(0,3)','gfx':'pixels','audio':{}}
project=dict(initial)
result={'ok':True,'revision':1,'events':[{'type':'tool_execution_end','tool':'read_game','error':False}]}
r=verdict(rule(operation='read_only'),result,project,initial)
assert r['讨论未改写或发布'] and r['没有尝试修改工具'] and '发布了新的游戏版本' not in r
bad=dict(result,events=result['events']+[{'type':'tool_execution_start','tool':'write_game'}])
assert not verdict(rule(operation='read_only'),bad,project,initial)['没有尝试修改工具']
assert not verdict(rule(operation='read_only'),result,dict(project,lua='changed'),initial)['讨论未改写或发布']
try:validate_expect(rule(unimplemented_requirement=True))
except ValueError:pass
else:raise AssertionError('Unknown criteria must not silently pass')
assert not additional(rule(design_required=True),{}, {'design':{}},initial)[0]['ok']
versions=[{'revision':1,'gfx':'a','audio':'a'},{'revision':2,'gfx':'a','audio':'a'}]
case=rule(preserve_between_turns=['gfx','audio']);case['turns']=['one','two']
r=additional(case,{'turns':[{'before':0,'revision':1},{'before':1,'revision':2}]},{'versions':versions},initial);assert all(x['ok'] for x in r)
wrong=copy.deepcopy(versions);wrong[1]['audio']='wrong';r=additional(case,{'turns':[{}, {'before':1,'revision':2}]},{'versions':wrong},initial);assert not r[1]['ok']
sound={'id':14,'speed':16,'notes':[[24,0,3,0],[24,0,2,5]],'loop_start':0,'loop_end':0}
pattern={'id':6,'channels':[14,-1,-1,-1],'loop_start':True,'loop_end':True,'stop':False}
audio={'sfx':[sound],'music':[pattern]};initial=dict(initial,audio=audio)
project=dict(initial,lua='music(8,200,3) sfx(0,3)',audio={'sfx':[sound,dict(sound,id=16,speed=20)],'music':[pattern,dict(pattern,id=8,channels=[16,-1,-1,-1])]})
c=rule(only_music_calls_changed=True,music_speed_clone={'original_pattern':6,'speed':20});assert all(x['ok'] for x in additional(c,{},project,initial))
wrong=copy.deepcopy(project);wrong['audio']['sfx'][1]['notes'][0][1]=3;assert not additional(c,{},wrong,initial)[1]['ok']
wrong=copy.deepcopy(project);wrong['lua']+=' score=100';assert not additional(c,{},wrong,initial)[0]['ok']
wrong=copy.deepcopy(project);wrong['audio']['music']=wrong['audio']['music'][1:];assert not additional(c,{},wrong,initial)[-1]['ok']
assert additional(rule(final_reply_max_chars=10),{},dict(project,messages=[{'role':'assistant','content':[{'type':'text','text':'123'}]}]),initial)[0]['ok']
assert not additional(rule(final_reply_max_chars=10),{},dict(project,messages=[{'role':'assistant','content':[{'type':'text','text':'x'*11}]}]),initial)[0]['ok']
h={'kind':'synthetic-stress-history','count':40,'count_after':42,'sha256_before':'abc','sha256_after':'abc'}
assert additional(rule(history_preserved=True),{'history_fixture':h},project,initial)[0]['ok']
assert not additional(rule(history_preserved=True),{'history_fixture':dict(h,sha256_after='different')},project,initial)[0]['ok']
assert not additional(rule(compaction_required=True),{},project,initial)[0]['ok']
print('PASS objective negative controls: readonly authorization, unknown criteria, design completeness, real-version preservation, exact music timing/notes/assets and bounded source edits')
