"""Headless keyboard injection and an in-memory UTF-8 X11 clipboard.

Never focus an application or press Enter after dictated text. Freeze the X11
input-focus window at recording start; a changed destination keeps text on Brick.
"""
import asyncio
import os
from pathlib import Path
import threading
import subprocess

def session_environment():
    env=dict(os.environ)
    # A lingering service starts before login. Resolve session fields on demand;
    # never source shell profiles or copy unrelated environment / credentials.
    for p in Path('/proc').iterdir():
        if not p.name.isdigit():continue
        try:
            if p.stat().st_uid!=os.getuid():continue
            if (p/'comm').read_text().strip() not in ('steam','plasmashell'):continue
            fields=(p/'environ').read_bytes().split(b'\0')
            values=dict(x.split(b'=',1) for x in fields if b'=' in x)
            for name in ('DISPLAY','XAUTHORITY','WAYLAND_DISPLAY','XDG_RUNTIME_DIR','DBUS_SESSION_BUS_ADDRESS'):
                if name.encode() in values:env[name]=values[name.encode()].decode()
            if 'DISPLAY' in env:return env
        except (OSError,UnicodeError):pass
    return env

class Keyboard:
    def __init__(self):
        self.device=None;self.clipboard=None;self.clip_env=None;self.clip_text='';self.clip_lock=threading.Lock()
    def focus(self):
        from Xlib import display
        env=session_environment()
        if not env.get('DISPLAY'):return None
        try:
            d=display.Display(env['DISPLAY']);w=d.get_input_focus().focus
            wid=getattr(w,'id',0);root=d.screen().root.id;d.close()
            return (env['DISPLAY'],wid) if wid not in (0,1,root) else None
        except Exception:return None
    def ensure(self):
        if self.device is None or self.device.poll() is not None:
            self.device=subprocess.Popen([str(Path(__file__).with_name('brick-mic-keyboard'))],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
            if self.device.stdout.readline().strip()!='ready':
                self.device.terminate();raise RuntimeError('uinput unavailable')
    async def chord(self,*keys):
        self.ensure()
        codes={'LEFTCTRL':29,'LEFTSHIFT':42,'LEFT':105,'RIGHT':106,'UP':103,'DOWN':108,'BACKSPACE':14,'A':30,'C':46,'V':47,'Z':44,'SPACE':57,'ENTER':28}
        self.device.stdin.write(' '.join(str(codes[key]) for key in keys)+'\n');self.device.stdin.flush()
        if self.device.stdout.readline().strip()!='ok':raise RuntimeError('keyboard unavailable')
    def close(self):
        if self.device:
            self.device.stdin.close()
            try:self.device.wait(timeout=2)
            except subprocess.TimeoutExpired:self.device.kill();self.device.wait()
            self.device=None
    async def xchord(self,*keys):
        # Gamescope/HHD can intercept evdev keyboard events. Editing the active
        # X11 text window through XTEST avoids that overlay routing layer.
        from Xlib import X,XK,display
        from Xlib.ext import xtest
        env=session_environment();d=display.Display(env['DISPLAY'])
        symbols={'LEFTCTRL':'Control_L','LEFTSHIFT':'Shift_L','LEFT':'Left','RIGHT':'Right','UP':'Up','DOWN':'Down','BACKSPACE':'BackSpace','A':'a','C':'c','V':'v','Z':'z','SPACE':'space','ENTER':'Return'}
        codes=[d.keysym_to_keycode(XK.string_to_keysym(symbols[k])) for k in keys]
        if not all(codes):d.close();raise RuntimeError('keyboard mapping unavailable')
        try:
            for code in codes:xtest.fake_input(d,X.KeyPress,code)
            d.flush();await asyncio.sleep(.015)
        finally:
            for code in reversed(codes):xtest.fake_input(d,X.KeyRelease,code)
            d.sync();d.close()
    def set_clipboard(self,text,destination):
        from Xlib import X,display
        if self.clipboard is None or self.clip_env!=destination[0]:
            if self.clipboard:raise RuntimeError('desktop changed; reconnect receiver')
            d=display.Display(destination[0]);win=d.screen().root.create_window(-1,-1,1,1,0,d.screen().root_depth)
            self.clipboard=(d,win);self.clip_env=destination[0]
            threading.Thread(target=self.clipboard_events,daemon=True).start()
        with self.clip_lock:self.clip_text=text
        d,win=self.clipboard;win.set_selection_owner(d.intern_atom('CLIPBOARD'),X.CurrentTime);d.flush()
    def clipboard_events(self):
        from Xlib import X,protocol
        d,win=self.clipboard
        try:
            while True:
                ev=d.next_event()
                if ev.type!=X.SelectionRequest:continue
                target=d.get_atom_name(ev.target);prop=ev.property or ev.target
                with self.clip_lock:text=self.clip_text
                if target=='TARGETS':
                    ev.requestor.change_property(prop,d.intern_atom('ATOM'),32,[d.intern_atom(t) for t in ('TARGETS','UTF8_STRING','text/plain;charset=utf-8','STRING')])
                elif target in ('UTF8_STRING','text/plain;charset=utf-8','STRING'):
                    ev.requestor.change_property(prop,ev.target,8,text.encode('latin1','replace') if target=='STRING' else text.encode())
                else:prop=X.NONE
                ev.requestor.send_event(protocol.event.SelectionNotify(time=ev.time,requestor=ev.requestor,selection=ev.selection,target=ev.target,property=prop));d.flush()
        except Exception:pass
    async def insert(self,text,destination):
        if not text:return True
        if destination is None or self.focus()!=destination:return False
        self.ensure();self.set_clipboard(text,destination)
        # Check again immediately before posting keys; don't move focus ourselves.
        if self.focus()!=destination:return False
        await self.chord('LEFTCTRL','V');return True
    async def command(self,op,arg=''):
        if not any(session_environment().get(k) for k in ('DISPLAY','WAYLAND_DISPLAY')):raise RuntimeError('no desktop session')
        keys={'left':('LEFT',),'right':('RIGHT',),'up':('UP',),'down':('DOWN',),
              'delete':('BACKSPACE',),'all':('LEFTCTRL','A'),'copy':('LEFTCTRL','C'),
              'paste':('LEFTCTRL','V'),'undo':('LEFTCTRL','Z'),'redo':('LEFTCTRL','LEFTSHIFT','Z'),
              'space':('SPACE',),'enter':('ENTER',),'newline':('LEFTSHIFT','ENTER')}
        if op not in keys:raise ValueError('unsupported input command')
        chord=keys[op]
        if op in ('left','right','up','down') and arg=='select':chord=('LEFTSHIFT',)+chord
        if self.focus() is not None:await self.xchord(*chord)
        else:await self.chord(*chord)
