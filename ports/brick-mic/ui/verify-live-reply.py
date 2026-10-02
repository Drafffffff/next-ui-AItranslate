#!/usr/bin/env python3
"""Exercise actual PocketJS rendering, with a new Mac reply and no handheld submit."""
from preview import ROOT,run
output=ROOT/'build/brick-mic/ui-validation';output.mkdir(parents=True,exist_ok=True)
for identical in [False,True]:
    commands=run('ready',frames=360,reply_input_test=True,live_reply=True,identical_reply=identical,
                 output=output/('r14-live-identical.ppm' if identical else 'r14-live-reply.ppm'))
    assert 'control:submit' not in commands,commands
    assert commands.count('control:reply:seen:0')==1,commands
    assert commands.count('control:reply:seen:1')==1,commands
    assert [c for c in commands if not c.startswith('control:reply:seen:')]==['control:reply:next','control:left:select','start','stop'],commands
    print('PASS: dictation switches to fresh AI reply without X; same text='+str(identical),flush=True)
