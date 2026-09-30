#!/usr/bin/env python3
"""Acceptance against the compiled app, in disposable data; never touches a key."""
import json, os, subprocess, sys, tempfile
from pathlib import Path
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[3]
package=root/'build/pocketjs-port'/('creative-mac' if sys.platform=='darwin' else 'Brick Creative.pak')
exe='./pocketjs-app' if sys.platform=='darwin' else './pocketjs-app.elf'
with tempfile.TemporaryDirectory(prefix='creative-acceptance-') as data:
    env={**os.environ,'POCKETJS_DATA':data,'POCKETJS_FONT':str(root/'fonts/font1.ttf'),'POCKETJS_HARDWARE':'0'}
    args=[exe,'--creative-test']
    if sys.platform!='darwin':
        sysroot='/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc'
        args=[sysroot+'/lib/ld-linux-aarch64.so.1','--library-path',sysroot+'/lib:'+sysroot+'/usr/lib']+args
    subprocess.run(args,cwd=package,env=env,check=True,timeout=30)
    project=json.loads((Path(data)/'pixel-project.json').read_text())
    assert project['version']==1 and project['frames'][0][0]=='3'
    svg=(Path(data)/'pixel-export.svg').read_text()
    assert ET.fromstring(svg).tag=='{http://www.w3.org/2000/svg}svg'
    assert len(svg.encode())<128*1024
    print('PASS: persisted project and exported SVG validated outside the guest')
