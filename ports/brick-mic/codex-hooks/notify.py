#!/usr/bin/env python3
"""Reviewed localhost hook bridge; no transcript reads, implicit approvals or logs."""
import json, os, re, socket, sys, time, uuid
from pathlib import Path

EVENTS={'SessionStart','UserPromptSubmit','Stop','PermissionRequest','Interrupt','SessionEnd'}
SENSITIVE=re.compile(r'(api.?key|token|password|secret|authorization|credential|access.?key|cookie)',re.I)
ASSIGNMENT=re.compile(r'(?i)(?:api[_-]?key|token|password|secret|authorization|access[_-]?key)\s*[=:]')
SECRET_FLAG=re.compile(r'(?i)--(?:api[_-]?key|token|password|secret|authorization|access[_-]?key|cookie)\b(?:\s|=)')
BEARER=re.compile(r'(?i)\bBearer\s+[A-Za-z0-9._~+/-]+=*')
CONTROL=re.compile(r'[\x00-\x08\x0b\x0c\x0e-\x1f]')

def safe_text(value):
    original=str(value);text=CONTROL.sub('',original)
    # Shell quoting/concatenation can conceal where a secret ends. Hide the whole
    # string rather than leaking an unmatched suffix or pretending to show scope.
    if ASSIGNMENT.search(text) or SECRET_FLAG.search(text) or BEARER.search(text) or re.search(r'\bsk-[A-Za-z0-9_-]{16,}\b',text):
        return '〔含敏感参数，内容已隐藏〕',True
    return text,text!=original

def scrub(value):
    if isinstance(value,dict):
        result={};hidden=False
        for key,item in value.items():
            if SENSITIVE.search(str(key)):
                result[str(key)]='[已隐藏]';hidden=True
            else:
                result[str(key)],changed=scrub(item);hidden|=changed
        return result,hidden
    if isinstance(value,list):
        result=[];hidden=False
        for item in value:
            safe,changed=scrub(item);result.append(safe);hidden|=changed
        return result,hidden
    if isinstance(value,str):return safe_text(value)
    return value,False

def bounded(text,characters,byte_limit):
    result=text[:characters]
    while len(result.encode('utf8'))>byte_limit:result=result[:-1]
    return result

def approval_details(event):
    name=str(event.get('tool_name') or '未知操作')
    tool=bounded(name,80,128)
    original=event.get('tool_input')
    safe,hidden=scrub(original)
    complete=True
    if name in ('Bash','apply_patch') and isinstance(safe,dict):
        command=safe.get('command')
        description=safe.get('description')
        if not isinstance(command,str) or not command.strip():complete=False;command='请在 Mac 查看完整操作'
        summary=(str(description)+'\n' if description else '')+command
        remainder={key:value for key,value in safe.items() if key not in ('command','description')}
        if remainder:summary+='\n'+json.dumps(remainder,ensure_ascii=False,sort_keys=True,separators=(',',':'))
    else:
        summary=json.dumps(safe,ensure_ascii=False,sort_keys=True,separators=(',',':'))
        if original is None:complete=False;summary='请在 Mac 查看完整操作'
    if isinstance(event.get('cwd'),str):
        cwd,changed=safe_text(event['cwd']);hidden|=changed
        summary='目录: '+cwd+'\n'+summary
    clipped=len(summary)>1100 or len(summary.encode('utf8'))>3800 or name!=tool
    if clipped:summary=bounded(summary,1000,3600)+'\n〔内容过长，请在 Mac 查看完整操作〕'
    if hidden:summary+='\n〔敏感内容已隐藏，请在 Mac 查看完整操作〕'
    return {'tool':tool,'summary':summary,'allowAvailable':complete and not hidden and not clipped}

def relay(event,endpoint=None,wait=90):
    if not isinstance(event,dict) or event.get('hook_event_name') not in EVENTS:return {}
    # A subagent completion must never complete the parent task.
    if event.get('agent_id') or event.get('agent_type'):return {}
    allowed=('session_id','turn_id','hook_event_name','cwd','stop_hook_active')
    payload={key:event[key] for key in allowed if key in event}
    if not isinstance(payload.get('session_id'),str):return {}
    endpoint=endpoint or Path.home()/'Library/Application Support/Brick Mic/hook-endpoint.json'
    info=endpoint.stat()
    if info.st_uid!=os.getuid() or info.st_mode&0o077:return {}
    destination=json.loads(endpoint.read_text())
    port=destination['port'];token=destination['token']
    if not isinstance(port,int) or not 1024<=port<=65535 or not isinstance(token,str) or len(token)!=36:return {}
    payload['token']=token;payload['at']=time.time()
    permission=event['hook_event_name']=='PermissionRequest'
    request_id=''
    if permission:
        request_id=str(uuid.uuid4());payload['request_id']=request_id
        payload['permission']=approval_details(event)
    if event['hook_event_name']=='Stop' and isinstance(event.get('last_assistant_message'),str):
        text=CONTROL.sub('',event['last_assistant_message'])
        payload['last_assistant_message']=text[:12000]
        payload['reply_truncated']=len(text)>12000
    packet=json.dumps(payload,ensure_ascii=False,separators=(',',':')).encode()+b'\n'
    if len(packet)>65536:return {}
    with socket.create_connection(('127.0.0.1',port),timeout=.25) as connection:
        connection.sendall(packet)
        if not permission:return {}
        connection.settimeout(wait)
        response=b''
        while b'\n' not in response and len(response)<=4096:
            part=connection.recv(4096-len(response)+1)
            if not part:return {}
            response+=part
        if len(response)>4096 or b'\n' not in response:return {}
        decision=json.loads(response.split(b'\n',1)[0])
        if not isinstance(decision,dict) or decision.get('request_id')!=request_id:return {}
        choice=decision.get('decision')
        if choice not in ('allow','deny') or choice=='allow' and not payload['permission']['allowAvailable']:return {}
        answer={'behavior':choice}
        if choice=='deny':answer['message']='用户在 Brick Mic 上拒绝此操作。'
        return {'hookSpecificOutput':{'hookEventName':'PermissionRequest','decision':answer}}

def main():
    answer={}
    try:
        raw=sys.stdin.buffer.read(1024*1024+1)
        if len(raw)<=1024*1024:answer=relay(json.loads(raw))
    except (OSError,ValueError,KeyError,TypeError):pass
    # {} declines to decide; Codex continues its original Mac approval flow.
    print(json.dumps(answer,ensure_ascii=False,separators=(',',':')))

if __name__=='__main__':main()
