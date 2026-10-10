"""Reviewer transport/schema regression; no API calls."""
import json
from unittest.mock import patch
from run import judge
class Response:
 def __init__(self,value):self.value=value
 def read(self):return json.dumps(self.value).encode()
requests=[]
responses=[{'choices':[{'message':{'content':'{"criteria":[{"id":9,"score":1,"reason":"bad"}],"suggestions":[]}'}}],'usage':{'prompt_tokens':10,'completion_tokens':2,'total_tokens':12}}, {'choices':[{'message':{'content':'{"criteria":[{"id":0,"score":4,"reason":"uses complete range","lines":[1]}],"suggestions":[]}'}}],'usage':{'prompt_tokens':20,'completion_tokens':3,'total_tokens':23}}]
def call(request,timeout):
 requests.append(json.loads(request.data));return Response(responses.pop(0))
project={'lua':'local u=(c-full_left)/full_width','versions':[{'lua':'local u=(c-clipped_left)/clipped_width'}]}
with patch('urllib.request.urlopen',call):
 out=judge({'turns':['fix clipping'],'rubric':['unclipped UV']},project,{'events':[]},{'ok':True},{'provider':'deepseek','model':'deepseek-flash','key':'test','endpoint':'https://fixture.invalid'},'')
assert out['ok'] and out['repair_attempted'] and len(out['attempts'])==1
assert out['usage']=={'prompt_tokens':30,'completion_tokens':5,'total_tokens':35}
payload=json.loads(requests[0]['messages'][1]['content']);assert payload['initial_lua']==project['versions'][0]['lua'] and '-local u=(c-clipped_left)' in payload['actual_diff'] and '+local u=(c-full_left)' in payload['actual_diff']
assert len(requests[1]['messages'])==4
print('PASS reviewer before/after evidence, bounded schema repair and all-attempt usage')
