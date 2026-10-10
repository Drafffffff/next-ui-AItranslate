"""Parse reviewer evidence without trusting reviewer-authored criterion labels."""
import json,re

def parse_review(message,rubric,length):
 message=re.sub(r'^```(?:json)?\s*|\s*```$','',message.strip())
 obj=json.loads(message)
 if not isinstance(obj,dict) or not isinstance(obj.get('criteria'),list) or len(obj['criteria'])!=len(rubric):raise ValueError('Expected exactly one review for each requested criterion')
 criteria=obj['criteria']
 ids=[c.get('id') if isinstance(c,dict) else None for c in criteria]
 if any(type(i) is not int for i in ids) or sorted(ids)!=list(range(len(rubric))):raise ValueError('Missing, repeated or unrequested criterion id')
 checked=[]
 for c in sorted(criteria,key=lambda c:c['id']):
  if type(c.get('score')) is not int or not 0<=c['score']<=4 or not isinstance(c.get('reason'),str) or not c['reason'].strip():raise ValueError('Invalid score/reason')
  if 'name' in c and c['name']!=rubric[c['id']]:raise ValueError('Reviewer changed the criterion label')
  lines=c.get('lines',[])
  if not isinstance(lines,list) or not all(type(n) is int and 1<=n<=length for n in lines):raise ValueError('Invalid source citations')
  checked.append({'id':c['id'],'name':rubric[c['id']],'score':c['score'],'reason':c['reason'],'cited_lines':lines,'lines':[min(lines),max(lines)] if lines else []})
 suggestions=obj.get('suggestions',[])
 if not isinstance(suggestions,list) or any(not isinstance(x,str) for x in suggestions):raise ValueError('Invalid suggestions')
 return checked,suggestions
