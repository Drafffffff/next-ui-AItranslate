import asyncio
import json
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch,AsyncMock
from protocol import Assembler,decode
from asr import Recognition,ASRError,DEFAULT_ENDPOINT,MODEL
from receiver import Receiver
from input import Keyboard

class KeyboardTests(unittest.IsolatedAsyncioTestCase):
    async def test_selection_uses_active_x11_window_and_wayland_uses_device(self):
        k=Keyboard();sent=[]
        async def xkeys(*keys):sent.append(('x11',keys))
        async def device(*keys):sent.append(('device',keys))
        k.xchord=xkeys;k.chord=device
        with patch('input.session_environment',return_value={'DISPLAY':':0'}),patch.object(k,'focus',return_value=(':0',123)):
            await k.command('left','select')
            await k.command('delete')
        with patch('input.session_environment',return_value={'WAYLAND_DISPLAY':'wayland-0'}),patch.object(k,'focus',return_value=None):
            await k.command('enter')
        self.assertEqual(sent,[('x11',('LEFTSHIFT','LEFT')),('x11',('BACKSPACE',)),('device',('ENTER',))])
    async def test_cancel_releases_modifier_and_closes_x11_connection(self):
        events=[];pressed=asyncio.Event()
        d=SimpleNamespace(keysym_to_keycode=lambda name: {'Shift_L':50,'Left':113}[name],flush=lambda:pressed.set(),sync=lambda:None,close=lambda:events.append('closed'))
        fake=SimpleNamespace(fake_input=lambda display,kind,code:events.append((kind,code)))
        modules={'Xlib':SimpleNamespace(X=SimpleNamespace(KeyPress=2,KeyRelease=3),XK=SimpleNamespace(string_to_keysym=lambda s:s),display=SimpleNamespace(Display=lambda _:d)), 'Xlib.ext':SimpleNamespace(xtest=fake)}
        with patch.dict(sys.modules,modules),patch('input.session_environment',return_value={'DISPLAY':':0'}):
            task=asyncio.create_task(Keyboard().xchord('LEFTSHIFT','LEFT'))
            await pressed.wait();task.cancel()
            with self.assertRaises(asyncio.CancelledError):await task
        self.assertEqual(events,[(2,50),(2,113),(3,113),(3,50),'closed'])

class ProtocolTests(unittest.TestCase):
    def test_independent_frames_and_bounds(self):
        encoded=struct.pack('<HhB',5,100,0)+bytes([0x10,0x98])
        self.assertEqual(struct.unpack('<5h',decode(encoded)),(100,100,101,101,100))
        for bad in (b'',encoded[:-1],struct.pack('<HhB',321,0,0),struct.pack('<HhB',1,0,89)):
            with self.assertRaises(ValueError):decode(bad)
    def test_fragment_order(self):
        a=Assembler();header=lambda off:struct.pack('<BHHHH',2,5,9,off,6)
        self.assertIsNone(a.append(header(0)+b'abc'))
        self.assertEqual(a.append(header(3)+b'def'),(2,5,9,b'abcdef'))
        a.append(header(0)+b'abc')
        with self.assertRaises(ValueError):a.append(header(4)+b'ef')

class ASRTests(unittest.IsolatedAsyncioTestCase):
    async def test_final_result_is_available_before_slow_socket_cleanup(self):
        a=Recognition('test-key',DEFAULT_ENDPOINT);close_allowed=asyncio.Event();closing=asyncio.Event()
        def event(kind,**payload):return json.dumps({'header':{'task_id':a.id,'event':kind},'payload':payload})
        events=[event('result-generated',output={'sentence':{'sentence_id':1,'sentence_end':True,'text':'完整结果'}}),event('task-finished')]
        class WS:
            async def __aenter__(self):return self
            async def __aexit__(self,*args):closing.set();await close_allowed.wait()
            async def send(self,value):pass
            async def recv(self):return event('task-started')
            def __aiter__(self):return self
            async def __anext__(self):
                await a.finished.wait()
                if not events:raise StopAsyncIteration
                return events.pop(0)
        with patch.dict(sys.modules,{'websockets.asyncio.client':SimpleNamespace(connect=lambda *args,**kwargs:WS())}):
            a.append(bytes(640));a.end();task=asyncio.create_task(a.run())
            try:
                self.assertEqual(await asyncio.wait_for(a.wait_final(),1),'完整结果')
                await asyncio.wait_for(closing.wait(),1)
                self.assertFalse(task.done(),'Final output must not wait for connection cleanup')
            finally:
                close_allowed.set()
                await task
        self.assertEqual(a.final_text,'完整结果')
    async def test_failed_start_wakes_final_waiter_and_preserves_error(self):
        a=Recognition('test-key',DEFAULT_ENDPOINT)
        class WS:
            async def __aenter__(self):return self
            async def __aexit__(self,*args):pass
            async def send(self,value):pass
            async def recv(self):return json.dumps({'header':{'task_id':a.id,'event':'task-failed'}})
        with patch.dict(sys.modules,{'websockets.asyncio.client':SimpleNamespace(connect=lambda *args,**kwargs:WS())}):
            task=asyncio.create_task(a.run())
            with self.assertRaises(ASRError):await asyncio.wait_for(a.wait_final(),1)
            with self.assertRaises(ASRError):await task
    async def test_upload_finishes_after_all_audio(self):
        a=Recognition('test-key',DEFAULT_ENDPOINT);sent=[]
        class WS:
            async def send(self,value):sent.append(value)
        a.append(b'\0\0'*320);a.append(b'\1\0'*320);a.end()
        await a.upload(WS())
        self.assertEqual(len(sent),3);self.assertIsInstance(sent[0],bytes)
        self.assertEqual(json.loads(sent[-1])['header']['action'],'finish-task')
        self.assertEqual(a.queued,0);self.assertTrue(a.finished.is_set())
    async def test_final_only_sorted_sentence_updates(self):
        a=Recognition('test-key',DEFAULT_ENDPOINT);a.finished.set()
        def event(event,**payload):return json.dumps({'header':{'task_id':a.id,'event':event},'payload':payload})
        events=[event('result-generated',output={'sentence':{'sentence_id':2,'sentence_end':True,'text':'乙'}}),
                event('result-generated',output={'sentence':{'sentence_id':1,'sentence_end':False,'text':'草稿'}}),
                event('result-generated',output={'sentence':{'sentence_id':1,'sentence_end':True,'text':'甲'}}),event('task-finished')]
        class WS:
            def __aiter__(self):return self
            async def __anext__(self):
                if not events:raise StopAsyncIteration
                return events.pop(0)
        self.assertEqual(await a.receive(WS()),'甲乙')
    def test_endpoint_identity_queue_bounds(self):
        with self.assertRaises(ASRError):Recognition('test','ws://localhost/api-ws/v1/inference')
        a=Recognition('test',DEFAULT_ENDPOINT)
        with self.assertRaises(ASRError):a.event({'header':{'task_id':'other','event':'task-started'}})
        a.append(bytes(96000))
        with self.assertRaises(ASRError):a.append(bytes(2))

class ReceiverTests(unittest.IsolatedAsyncioTestCase):
    async def test_powered_off_recovers_but_permission_denials_do_not(self):
        r=Receiver({'name':'bazzite','key':'test','id':'linux-1234'})
        def error(reason):
            e=RuntimeError('test');e.reason=SimpleNamespace(name=reason);return e
        with patch('receiver.power_on_bluetooth',new_callable=AsyncMock,return_value=True) as power:
            self.assertTrue(await r.recover_bluetooth(error('POWERED_OFF')))
            self.assertEqual(r.status['bluetooth_recoveries'],1)
            for reason in ('DENIED_BY_USER','DENIED_BY_SYSTEM','DENIED_BY_UNKNOWN','NO_BLUETOOTH','NO_BLE_CENTRAL_ROLE'):
                self.assertFalse(await r.recover_bluetooth(error(reason)))
            self.assertFalse(await r.recover_bluetooth(RuntimeError('unrelated')))
            power.assert_awaited_once()
    async def test_failed_radio_power_on_does_not_claim_recovery(self):
        r=Receiver({'name':'bazzite','key':'test','id':'linux-1234'})
        e=RuntimeError('test');e.reason=SimpleNamespace(name='POWERED_OFF')
        with patch('receiver.power_on_bluetooth',new_callable=AsyncMock,return_value=False):
            self.assertFalse(await r.recover_bluetooth(e))
        self.assertEqual(r.status['bluetooth_recoveries'],0);self.assertEqual(r.status['connection'],'bluetooth-off')
    async def test_completed_recognizer_keeps_cleaning_up_after_input(self):
        r=Receiver({'name':'bazzite','key':'test','id':'linux-1234'});r.sid=4;r.connected=True;r.release=0
        close_allowed=asyncio.Event()
        async def cleanup():await close_allowed.wait()
        task=asyncio.create_task(cleanup());r.recognition=task;r.recognizers.add(task)
        a=Recognition('test-key',DEFAULT_ENDPOINT);a.final_text='你好';a.finalized.set();r.asr=a
        async def insert(*args):return True
        async def result(*args):pass
        async def control(*args):pass
        r.keyboard.insert=insert;r.result=result;r.control=control
        try:
            await r.finish(4)
            self.assertFalse(task.done());self.assertTrue(r.status['inserted']);self.assertEqual(r.sid,0)
        finally:close_allowed.set();await task
    async def test_input_failure_preserves_transcript_and_no_submit(self):
        r=Receiver({'name':'bazzite','key':'test','id':'linux-1234'});r.sid=4;r.connected=True;r.release=0
        r.recognition=asyncio.get_running_loop().create_future();r.recognition.set_result('你好')
        sent=[]
        async def result(sid,text):sent.append((sid,text))
        async def control(hint):sent.append(hint)
        async def insert(*args):raise PermissionError('test')
        r.result=result;r.control=control;r.keyboard.insert=insert
        await r.finish(4)
        self.assertEqual(sent[0],(4,'你好'));self.assertIn('文字保留',sent[1]);self.assertFalse(r.status['inserted'])
    async def test_new_recording_after_final_result_is_not_cancelled(self):
        r=Receiver({'name':'bazzite','key':'test','id':'linux-1234'});r.sid=4;r.connected=True;r.release=0
        r.recognition=asyncio.get_running_loop().create_future();r.recognition.set_result('你好')
        async def result(sid,text):r.sid=5;r.status['recording']=True
        async def control(hint):pass
        async def insert(*args):return True
        r.result=result;r.control=control;r.keyboard.insert=insert
        await r.finish(4)
        self.assertEqual(r.sid,5);self.assertTrue(r.status['recording'])
    async def test_control_token_rejects_wrong_peer(self):
        r=Receiver({'name':'bazzite','key':'test','id':'linux-1234'});r.token='good';called=[]
        async def command(*args):called.append(args)
        r.keyboard.command=command
        body=json.dumps({'op':'enter','token':'wrong'}).encode()
        r.messages.put_nowait(struct.pack('<BHHHH',6,0,0,0,len(body))+body)
        with self.assertRaises(asyncio.TimeoutError):await r.consume()
        self.assertEqual(called,[])

if __name__=='__main__':unittest.main()
