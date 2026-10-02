#!/usr/bin/env python3
"""Drop native IPC during rendering and verify recovery without UI relaunch."""
import os
import subprocess
import tempfile
import time
from pathlib import Path
from preview import Mock, PACKAGE, ROOT

with tempfile.TemporaryDirectory(prefix='mic-recovery-check-') as folder:
    data=Path(folder)
    mock=Mock(data/'mic.sock')
    env={**os.environ,'BRICK_MIC_PREVIEW':'1','BRICK_MIC_SOCKET':str(data/'mic.sock'),
         'POCKETJS_DATA':str(data/'cache'),'BRICK_MIC_FONT':str(ROOT/'fonts/font1.ttf'),
         'BRICK_MIC_SETTINGS':str(ROOT/'build/brick-mic/theme-preview.txt')}
    process=subprocess.Popen([str(PACKAGE/'pocketjs-mic'),'--headless','--frames','250'],
                             cwd=PACKAGE,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        time.sleep(1)
        mock.offline_until=time.monotonic()+1
        output,error=process.communicate(timeout=10)
        assert process.returncode==0,(output,error)
        assert 'microphone_backend_restart resumed=0 attempt=1' in output,output
        assert mock.recovered_pings>0
        assert mock.commands==[],mock.commands
        print('PASS: native IPC outage recovers automatically without restarting the UI or recording')
    finally:
        if process.poll() is None:process.kill();process.wait()
        mock.close()
