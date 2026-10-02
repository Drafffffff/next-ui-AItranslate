#!/usr/bin/env python3
"""Check NextUI boot integration in a temporary card tree; never powers off."""
import os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='brick-mic-resume-') as temp:
    card=Path(temp);shared=card/'.userdata/shared';app=card/'Tools/tg5040/Brick Mic.pak';marker=shared/'brick-mic/resume.json';record=card/'calls.txt'
    app.mkdir(parents=True);marker.parent.mkdir(parents=True)
    launcher=app/'launch.sh';launcher.write_text('#!/bin/sh\nprintf "opened\\n" >> "$TEST_RECORD"\nexit "${TEST_EXIT:-0}"\n');launcher.chmod(0o755)
    env={**os.environ,'DEVICE':'brick','SDCARD_PATH':str(card),'SHARED_USERDATA_PATH':str(shared),'TEST_RECORD':str(record)}
    hook=['sh',str(ROOT/'ports/brick-mic/resume.sync.sh')]
    subprocess.run(hook,env=env,check=True);assert not record.exists()
    marker.write_text('{"version":1,"app":"brick-mic"}')
    subprocess.run(hook,env=env,check=True);assert not marker.exists();assert record.read_text()=='opened\n'
    subprocess.run(hook,env=env,check=True);assert record.read_text()=='opened\n'
    marker.write_text('{"version":1,"app":"brick-mic"}')
    assert subprocess.run(hook,env={**env,'TEST_EXIT':'1'}).returncode==1
    assert not marker.exists();subprocess.run(hook,env=env,check=True)
    assert record.read_text()=='opened\nopened\n'
    marker.write_text('{"version":1,"app":"brick-mic"}')
    subprocess.run(hook,env={**env,'DEVICE':'smartpro'},check=True);assert marker.exists()
    print('PASS: one-time Brick app restore, no launch without marker, failure falls back to NextUI, device guard')
