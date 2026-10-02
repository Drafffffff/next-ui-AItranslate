#!/usr/bin/env python3
"""Install the reviewed helper, merge hooks, preserve existing hooks and backup."""
import json, shutil, datetime
from pathlib import Path

root=Path(__file__).resolve().parent
support=Path.home()/'Library/Application Support/Brick Mic'
support.mkdir(parents=True,exist_ok=True);support.chmod(0o700)
helper=support/'codex-notify.py'
shutil.copyfile(root/'notify.py',helper);helper.chmod(0o600)
config=Path.home()/'.codex/hooks.json'
config.parent.mkdir(parents=True,exist_ok=True)
current=json.loads(config.read_text()) if config.exists() else {'hooks':{}}
# The command's path is shell-quoted, not interpolated through JSON escaping.
command='/usr/bin/python3 '+"'"+str(helper).replace("'","'\\''")+"'"
for event in ['SessionStart','UserPromptSubmit','Stop','PermissionRequest','Interrupt','SessionEnd']:
    groups=current.setdefault('hooks',{}).setdefault(event,[])
    existing=[h for g in groups for h in g.get('hooks',[]) if h.get('command')==command]
    handler={'type':'command','command':command,'timeout':95 if event=='PermissionRequest' else 1}
    if event not in ('PermissionRequest','Interrupt','SessionEnd'):handler['async']=True
    if event=='PermissionRequest':handler['statusMessage']='等待 Brick Mic 确认操作（最长 90 秒）'
    if existing:
        for entry in existing:
            entry.pop('async',None);entry.update(handler)
    else:
        groups.append({'hooks':[handler]})
content=json.dumps(current,ensure_ascii=False,indent=2)+'\n'
if not config.exists() or config.read_text()!=content:
    if config.exists():shutil.copy2(config,config.with_name('hooks.json.brick-mic-backup-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')))
    temp=config.with_suffix('.json.new');temp.write_text(content);temp.chmod(0o600);temp.replace(config)
print('Brick Mic hooks installed. Review and trust the updated definitions in Codex CLI /hooks; this installer does not grant trust.')
