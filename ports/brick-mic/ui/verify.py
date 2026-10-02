#!/usr/bin/env python3
from pathlib import Path
import json,re,subprocess,tempfile,sys
from preview import ROOT,run as snapshot_run
# Read receipts are telemetry, separate from the existing user-action assertions.
def run(*args,**kwargs):
    result=snapshot_run(*args,**kwargs)
    strip=lambda commands:[c for c in commands if not c.startswith('control:reply:seen:')]
    return (strip(result[0]),result[1]) if isinstance(result,tuple) else strip(result)

output=ROOT/'build/brick-mic/ui-validation';output.mkdir(parents=True,exist_ok=True)
if '--controls-only' not in sys.argv:
    for state in ['bluetooth','service','disconnected','ready','recording','processing','result','error']:
        run(state,frames=40,output=output/(state+'.ppm'))
        subprocess.run(['sips','-s','format','png',str(output/(state+'.ppm')),'--out',str(output/(state+'.png'))],check=True,stdout=subprocess.DEVNULL)
        pixels=(output/(state+'.ppm')).read_bytes().split(b'\n',3)[3]
        assert len(pixels)==1024*768*3
        assert pixels[:3]==bytes.fromhex('e9f2f5'),(state,'did not inherit the NextUI background')
        print('PASS: real PocketJS render, theme, font and native IPC: '+state)
    commands=run('ready',frames=200,input_test=True,output=output/'input.ppm')
    assert commands==['start','stop','control:delete'],commands
    print('PASS: quick A tap preserves start/stop ordering; B deletes through native IPC')
    commands=run('error',frames=200,input_test=True,output=output/'retry.ppm')
    assert commands==[],commands
    print('PASS: A retries a disconnected service without starting a recording')
    commands=run('recording',frames=135,power_test=True,output=output/'wake.ppm')
    assert commands==['sleep'],commands
    print('PASS: sleeping cancels microphone; wake retains the same PocketJS runtime')
    with tempfile.TemporaryDirectory(prefix='mic-theme-check-') as d:
        settings=Path(d)/'settings.txt';settings.write_text('font=1\ncolor1=0xFFFFFFFF\ncolor4=0xEAE7DDFF\ncolor5=0x151820FF\ncolor6=0xB1B9CFFF\ncolor7=0x151820\n')
        run('ready',frames=35,output=output/'dark.ppm',settings=settings)
        pixels=(output/'dark.ppm').read_bytes().split(b'\n',3)[3]
        assert pixels[:3]==bytes.fromhex('151820')
        print('PASS: dark theme and legacy RGB color format')

    commands=run('ready',frames=180,wake_record_test=True,output=output/'wake-record.ppm')
    assert commands==['sleep','start','stop'],commands
    print('PASS: screen-off A press wakes and begins recording; the same release stops it')

commands=run('ready',frames=270,control_input_test=True,output=output/'control-input.ppm')
expected=['control:mode:codex','control:tasks','control:choose:00000000-0000-4000-8000-000000000001','control:left:select','control:tasks','control:delete','control:submit','control:redo','control:mode:ordinary']
assert commands==expected,commands
print('PASS: mode switch, task selection by ID, shoulder selection, delete, explicit submit, edit menu, ordinary-mode return')

commands=run('ready',frames=235,review_input_test=True,output=output/'approval-screen.ppm')
assert commands==['control:tasks'],commands
commands=run('ready',frames=255,review_input_test=True,output=output/'approval-confirm.ppm')
assert commands==['control:tasks','control:approval:00000000-0000-4000-8000-000000000001:deny','control:tasks'],commands
print('PASS: opening approval sends nothing, default deny requires explicit A')
commands=run('ready',frames=290,reply_input_test=True,output=output/'reply-close.ppm')
assert commands==['control:reply:next','control:left:select','start','stop','control:submit'],commands
print('PASS: task main shows latest reply automatically; shoulder paging, selection, same-screen recording and physical X submit')

commands=run('ready',frames=200,input_test=True,remote_allowed=False,output=output/'dictation-only.ppm')
assert commands==['start','stop'],commands
print('PASS: voice-only profile records normally; B cannot edit or unexpectedly exit')

commands=run('ready',frames=50,task_main=True,output=output/'task-main.ppm')
assert commands==[],commands
print('PASS: restored selected task displays reply without menu/open commands')

commands,report=run('ready',frames=180,task_main=True,focus_churn=True,statistics=True,output=output/'detail-stable.ppm')
assert commands==[],commands
changed,unchanged=map(int,re.search(r'changed_frames=(\d+) unchanged_frames=(\d+)',report).groups())
assert changed<12 and unchanged>160,(changed,unchanged)
print('PASS: task detail stays visible through focus updates; unchanged frames are not presented')
commands=run('ready',frames=260,reply_input_test=True,output=output/'dictation-review.ppm')
assert commands==['control:reply:next','control:left:select','start','stop'],commands
print('PASS: dictation result remains on task main until explicit X; no automatic submission')

before=output/'ordinary-before-y.ppm';after=output/'ordinary-after-y.ppm'
assert run('ready',frames=50,navigation_input_test=True,output=before)==[]
assert run('ready',frames=70,navigation_input_test=True,output=after)==[]
assert before.read_bytes()==after.read_bytes()
print('PASS: ordinary Y leaves screen and command stream unchanged')

commands=run('ready',frames=430,navigation_input_test=True,output=output/'key-roles.ppm')
assert commands==['control:mode:codex','control:tasks','control:choose:00000000-0000-4000-8000-000000000001','control:tasks','control:mode:ordinary','control:all','control:mode:codex','control:tasks','control:choose:00000000-0000-4000-8000-000000000001','control:mode:ordinary'],commands
print('PASS: ST enters Codex on main; ordinary Y is inert, Codex Y opens task center; hidden ST/Y/SE do nothing in menus; B returns consistently')
commands=run('result',frames=275,read_input_test=True,output=output/'reading-keys.ppm')
assert commands==[],commands
print('PASS: reading keys are local; A/ST/Y/SE do not dismiss reading or change mode; B alone returns')

commands=run('ready',frames=300,aux_input_test=True,output=output/'aux-input.ppm')
assert commands==['control:space','control:enter','control:mode:codex','control:tasks','control:choose:00000000-0000-4000-8000-000000000001','control:space','control:enter','control:space','control:enter','start','cancel'],commands
print('PASS: physical L3/R3 insert space/enter once per press; input is blocked during task selection and recording; edit-menu shortcuts and cancellation remain correct')
assert run('ready',frames=120,aux_input_test=True,remote_allowed=False,output=output/'aux-disabled.ppm')==[]
print('PASS: L3/R3 respect disabled remote editing')
