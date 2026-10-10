import json
from review import parse_review
rubric=['requested one','requested two']
obj={'criteria':[{'id':1,'score':2,'reason':'uncertain boundary','lines':[4,2]},{'id':0,'score':3,'reason':'implemented','lines':[1]}],'suggestions':['review boundary']}
c,s=parse_review(json.dumps(obj),rubric,5);assert [x['name'] for x in c]==rubric and c[1]['cited_lines']==[4,2] and c[1]['lines']==[2,4]
for raw in [json.dumps(obj)+' trailing',json.dumps({'criteria':obj['criteria']+[obj['criteria'][0]]}),json.dumps({'criteria':[dict(obj['criteria'][0],id=0),obj['criteria'][1]]}),json.dumps({'criteria':[dict(obj['criteria'][0],lines=[99]),obj['criteria'][1]]}),json.dumps({'criteria':[dict(obj['criteria'][0],score=True),obj['criteria'][1]]}),json.dumps({'criteria':[dict(obj['criteria'][0],name='invented criterion'),obj['criteria'][1]]})]:
 try:parse_review(raw,rubric,5)
 except ValueError:pass
 else:raise AssertionError('Invalid reviewer output accepted')
print('PASS reviewer schema: stable ids/order, exact original labels, preserved citations; extra/repeated criteria, trailing prose, invalid lines/scores rejected')
