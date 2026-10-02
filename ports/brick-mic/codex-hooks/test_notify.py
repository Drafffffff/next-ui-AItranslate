#!/usr/bin/env python3
"""Local fixtures only; never connects to the installed application or Codex."""
import importlib.util, json, os, socket, subprocess, tempfile, threading, unittest, uuid
from pathlib import Path

ROOT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('brick_hook',ROOT/'notify.py')
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)

class BridgeTests(unittest.TestCase):
    def event(self,name='PermissionRequest',**fields):
        return {'session_id':'00000000-0000-4000-8000-000000000001','turn_id':'turn-1','hook_event_name':name,
                'tool_name':'Bash','tool_input':{'command':'printf hello','description':'Print a fixture'},**fields}

    def exchange(self,event,answer=None,permissions=0o600):
        captured={}
        with tempfile.TemporaryDirectory() as temp,socket.socket() as server:
            server.bind(('127.0.0.1',0));server.listen(1);server.settimeout(1)
            endpoint=Path(temp)/'endpoint.json'
            endpoint.write_text(json.dumps({'port':server.getsockname()[1],'token':str(uuid.uuid4())}));endpoint.chmod(permissions)
            def receive():
                try:
                    connection,_=server.accept()
                    with connection:
                        data=b''
                        while b'\n' not in data:
                            part=connection.recv(4096)
                            if not part:break
                            data+=part
                        captured.update(json.loads(data.split(b'\n',1)[0]))
                        if answer is not None:
                            response=answer(captured)
                            connection.sendall(json.dumps(response).encode()+b'\n')
                except socket.timeout:pass
            worker=threading.Thread(target=receive);worker.start()
            result=bridge.relay(event,endpoint=endpoint,wait=.1)
            worker.join(1.5)
            return result,captured

    def test_allow_and_deny_are_explicit(self):
        for choice in ('allow','deny'):
            result,packet=self.exchange(self.event(),lambda p:{'request_id':p['request_id'],'decision':choice})
            self.assertEqual(result['hookSpecificOutput']['decision']['behavior'],choice)
            self.assertTrue(packet['permission']['allowAvailable'])

    def test_wrong_request_never_approves(self):
        result,_=self.exchange(self.event(),lambda p:{'request_id':str(uuid.uuid4()),'decision':'allow'})
        self.assertEqual(result,{})

    def test_closed_connection_returns_to_mac(self):
        result,_=self.exchange(self.event())
        self.assertEqual(result,{})

    def test_sensitive_command_is_not_approvable(self):
        fixture='fixture-password-that-must-not-leave-helper'
        result,packet=self.exchange(self.event(tool_input={'command':'TOKEN='+fixture+' printf hello'}),lambda p:{'request_id':p['request_id'],'decision':'allow'})
        self.assertEqual(result,{})
        self.assertNotIn(fixture,json.dumps(packet))
        self.assertFalse(packet['permission']['allowAvailable'])

    def test_quoted_and_flag_secrets_are_wholly_hidden(self):
        commands=["PASSWORD='fixture secret with spaces' printf hello",'tool --token fixture-secret-value',
                  'tool --api-key="fixture secret"',"PASSWORD='fixture'\"'\"'secret' printf hello"]
        for command in commands:
            with self.subTest(command=command):
                details=bridge.approval_details(self.event(tool_input={'command':command}))
                self.assertFalse(details['allowAvailable']);self.assertNotIn('fixture',details['summary'])

    def test_sensitive_mcp_fields_are_hidden(self):
        details=bridge.approval_details(self.event(tool_name='mcp__fixture__post',tool_input={'url':'https://example.test','api_key':'secret-fixture'}))
        self.assertFalse(details['allowAvailable']);self.assertNotIn('secret-fixture',details['summary'])

    def test_command_scope_and_all_extra_fields_are_visible(self):
        details=bridge.approval_details(self.event(cwd='/fixture/session',tool_input={'command':'rm notes.txt','cwd':'/fixture/tool','sandbox_permissions':'require_escalated','prefix_rule':['rm']}))
        self.assertTrue(details['allowAvailable'])
        for field in ('/fixture/session','/fixture/tool','require_escalated','prefix_rule'):
            self.assertIn(field,details['summary'])

    def test_large_command_and_missing_input_require_mac(self):
        for value in ({'command':'x'*1500},{},None):
            details=bridge.approval_details(self.event(tool_input=value))
            self.assertFalse(details['allowAvailable']);self.assertLessEqual(len(details['summary'].encode()),4000)

    def test_stop_only_relays_latest_official_reply(self):
        text='回复😀'*5000
        result,packet=self.exchange(self.event('Stop',last_assistant_message=text,transcript_path='/never/read/fixture',prompt='never-send-fixture'))
        self.assertEqual(result,{})
        self.assertEqual(packet['last_assistant_message'],text[:12000]);self.assertTrue(packet['reply_truncated'])
        self.assertNotIn('transcript_path',packet);self.assertNotIn('prompt',packet);self.assertNotIn('tool_input',packet)
        self.assertLess(len(json.dumps(packet,ensure_ascii=False).encode()),65536)

    def test_subagent_events_are_ignored(self):
        self.assertEqual(bridge.relay(self.event('Stop',agent_id='subagent-fixture')), {})

    def test_unprotected_endpoint_is_rejected(self):
        result,packet=self.exchange(self.event(),permissions=0o644)
        self.assertEqual(result,{});self.assertFalse(packet)

    def test_install_migrates_async_permission_preserving_other_hooks(self):
        with tempfile.TemporaryDirectory() as home:
            support=Path(home)/'Library/Application Support/Brick Mic'
            command="/usr/bin/python3 '"+str(support/'codex-notify.py')+"'"
            config=Path(home)/'.codex/hooks.json';config.parent.mkdir()
            config.write_text(json.dumps({'hooks':{'PermissionRequest':[{'hooks':[{'type':'command','command':command,'async':True,'timeout':1},{'type':'command','command':'echo unrelated','timeout':3}]}]}}))
            env={**os.environ,'HOME':home}
            subprocess.run(['python3',str(ROOT/'install.py')],env=env,check=True,stdout=subprocess.DEVNULL)
            first=config.read_text();subprocess.run(['python3',str(ROOT/'install.py')],env=env,check=True,stdout=subprocess.DEVNULL)
            self.assertEqual(first,config.read_text())
            handlers=json.loads(first)['hooks']['PermissionRequest'][0]['hooks']
            own=next(h for h in handlers if h['command']==command)
            self.assertNotIn('async',own);self.assertEqual(own['timeout'],95)
            self.assertIn({'type':'command','command':'echo unrelated','timeout':3},handlers)
            self.assertTrue(list(config.parent.glob('hooks.json.brick-mic-backup-*')))

if __name__=='__main__':unittest.main()
