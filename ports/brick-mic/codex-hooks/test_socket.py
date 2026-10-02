#!/usr/bin/env python3
"""Real localhost transport with a standalone Swift test double, never Codex UI."""
import importlib.util, json, stat, subprocess, tempfile, time, unittest
from contextlib import contextmanager
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('hook_bridge',ROOT/'codex-hooks/notify.py')
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)

class SocketTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory();cls.binary=Path(cls.build.name)/'control-harness'
        subprocess.run(['swiftc','-swift-version','5','-framework','AppKit','-framework','ApplicationServices',
                    str(ROOT/'mac/NotificationDelivery.swift'),str(ROOT/'mac/DesktopTaskMetadata.swift'),str(ROOT/'mac/Control.swift'),str(ROOT/'mac/tests/ControlHarness.swift'),'-o',str(cls.binary)],check=True)

    @classmethod
    def tearDownClass(cls):cls.build.cleanup()

    @contextmanager
    def server(self,scenario):
        with tempfile.TemporaryDirectory() as folder:
            process=subprocess.Popen([str(self.binary),folder,scenario],stdout=subprocess.DEVNULL)
            try:
                endpoint=Path(folder)/'hook-endpoint.json'
                for _ in range(100):
                    if endpoint.exists():break
                    time.sleep(.02)
                self.assertTrue(endpoint.exists())
                self.assertEqual(stat.S_IMODE(endpoint.stat().st_mode),0o600)
                yield endpoint
            finally:process.terminate();process.wait(timeout=2)

    def event(self,name='PermissionRequest',**extra):
        return {'session_id':'00000000-0000-4000-8000-000000000042','turn_id':'fixture-turn','hook_event_name':name,
                'tool_name':'Bash','tool_input':{'command':'printf fixture'},**extra}

    def test_swift_registry_fixtures(self):
        with tempfile.TemporaryDirectory() as folder:
            subprocess.run([str(self.binary),folder,'unit'],check=True,stdout=subprocess.DEVNULL)

    def test_real_socket_explicit_allow_deny_and_disconnect(self):
        for scenario in ('allow','deny','disconnect'):
            with self.subTest(scenario=scenario),self.server(scenario) as endpoint:
                result=bridge.relay(self.event(),endpoint=endpoint,wait=2)
                if scenario=='disconnect':self.assertEqual(result,{})
                else:self.assertEqual(result['hookSpecificOutput']['decision']['behavior'],scenario)

    def test_long_utf8_reply_is_saved_without_transcript_reads(self):
        with self.server('stop') as endpoint:
            text='回复😀'*5000
            self.assertEqual(bridge.relay(self.event('Stop',last_assistant_message=text),endpoint=endpoint),{})
            state=endpoint.parent/'control-state.json';reply=None
            for _ in range(100):
                if state.exists():
                    reply=json.loads(state.read_text()).get('replies',{}).get(self.event()['session_id'])
                    if reply:break
                time.sleep(.02)
            self.assertIsNotNone(reply)
            self.assertEqual(reply['text'],text[:12000]);self.assertTrue(reply['truncated'])
            self.assertEqual(stat.S_IMODE(state.stat().st_mode),0o600)

if __name__=='__main__':unittest.main()
