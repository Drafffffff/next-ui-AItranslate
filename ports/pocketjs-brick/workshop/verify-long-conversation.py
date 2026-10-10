#!/usr/bin/env python3
"""Actual QuickJS/PocketJS/native glyphs, isolated multi-megabyte transcript."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host',type=Path,required=True)
parser.add_argument('--project-fixture',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--markdown',action='store_true',help='Exercise native Markdown runs and a 22000-line fenced code block')
parser.add_argument('--messages',type=int,default=10000)
args=parser.parse_args();host=args.host.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
sd=output/'sd';assert not sd.exists(),'Use a fresh output'
shutil.copytree(next(args.project_fixture.glob('fixture-*/sd')),sd)
shared=sd/'.userdata/shared';data=shared/'ai-game-workshop';roms=sd/'Roms/AI生成游戏 (P8)'
path=next((data/'projects').glob('*.json'));p=json.loads(path.read_text());p['title']='长对话压力测试'
for v in p['versions']:v['title']=p['title']
def assistant(text):return dict(role='assistant',content=[dict(type='text',text=text)],stopReason='stop',timestamp=1)
p['messages']=[dict(role='user',content=f'BEGIN{i:05d} 历史需求：方向键移动，保留当前规则。',timestamp=1) if i%2==0 else assistant(f'第 {i} 次回复：已保存修改，按 SR 试玩。\n\n') for i in range(args.messages)]
p['messages'].extend([dict(role='assistant',content=[dict(type='toolCall',id='long-error',name='write_game',arguments={})],stopReason='toolUse'),dict(role='toolResult',toolCallId='long-error',toolName='write_game',isError=True,content=[dict(type='text',text='Validation failed for tool "write_game":\n - notes: fewer than 32 items\n\nReceived arguments:\n'+('  [0, 0, 0, 0],\n'*1800))])])
long_reply='LONG-BEGIN\n'+'已保存修改 line 1234567890\n'*22000+'LONG-END'
if args.markdown:long_reply='LONG-BEGIN\n```lua\n'+'-- line 1234567890 **literal**\n'*22000+'```\n## 玩法调整\n**手感优先**，按 `SR` 试玩。\n- [x] 已完成移动\n> 保留当前规则\n```lua\nprint("ready")\n```\n[开发参考](https://www.lexaloffle.com)\n**LONG-END**'
p['messages'].append(assistant(long_reply))
raw=json.dumps(p,ensure_ascii=False,separators=(',',':')).encode();assert 1024*1024<len(raw)<16*1024*1024
path.write_bytes(raw);(data/'last-project.txt').write_text(p['id']);shutil.rmtree(data/'project-info',ignore_errors=True)
config=dict(model='deepseek-flash',endpoint='http://127.0.0.1:1/v1',key='',reducedMotion=True);(data/'config.json').write_text(json.dumps(config));(shared/'ai-keys.txt').write_text('')
env=dict(os.environ,POCKETJS_WORKSHOP='1',POCKETJS_HARDWARE='0',POCKETJS_DATA=str(data),POCKETJS_ROMS=str(roms),POCKETJS_FONT=str(root/'fonts/font1.ttf'),SHARED_USERDATA_PATH=str(shared),NEXTUI_AI_KEYS_FILE=str(shared/'ai-keys.txt'))
for key in ['POCKETJS_VOICE','POCKETJS_VOICE_SOCKET','POCKETJS_WORKSHOP_RESUME','POCKETJS_TEST']:env.pop(key,None)
before=hashlib.sha256(raw).hexdigest();rom_before={f:hashlib.sha256(f.read_bytes()).hexdigest() for f in roms.glob('*.p8')}
result=subprocess.run([str(host),'--workshop-transcript-test','--dump',str(output/'chat')],cwd=host.parent,env=env,capture_output=True,text=True,timeout=100)
(output/'stdout.log').write_text(result.stdout);(output/'stderr.log').write_text(result.stderr);assert result.returncode==0,result.stdout+'\n'+result.stderr
assert hashlib.sha256(path.read_bytes()).hexdigest()==before,'Viewing changed saved history'
for f,digest in rom_before.items():assert hashlib.sha256(f.read_bytes()).hexdigest()==digest,'Viewing changed a game'
for kind in ['tail','error','details']:
 image=Image.open(output/f'chat.{kind}.ppm').convert('RGB');image.save(output/f'{kind}.png')
 # Conversation cannot overwrite the status/shortcut region.
 assert len(set(image.crop((56,620,680,628)).getdata()))==1,'Conversation overlaps footer'
print(result.stdout.strip());print(f'PASS: native {len(p["messages"])} messages / {len(raw):,} saved bytes / 22000-line single reply / 1800-line error; no history/ROM changes. {output}')
if args.markdown:
 image=Image.open(output/'tail.png').convert('RGB')
 assert len(set(image.crop((56,151,944,620)).getdata()))>4,'Markdown viewport did not render'
 print('PASS: native Markdown fixture, fenced long source, emphasis, inline code, list, quote and link; footer clear')
