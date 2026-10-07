#!/usr/bin/env python3
"""Package only ARM64 binaries plus reproducible source, licenses and SD installer."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/gba-link'
FILES={'.system/tg5040/bin/minarch.elf':OUT/'brick/minarch.elf',
       '.system/tg5040/cores/gpsp_libretro.so':OUT/'brick/gpsp_libretro.so',
       '.system/tg5040/bin/nextui-gba-bluetooth.elf':OUT/'brick/nextui-gba-bluetooth.elf'}
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
if subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,text=True).strip():
    raise SystemExit('Commit the source before packaging; refuse an unrecorded source tree')
# Source is exported exclusively with git archive below. Untracked work owned by
# other tasks is excluded; it must neither block this package nor enter it.
package=OUT/('nextui-gba-link-'+revision[:8])
if package.exists():shutil.rmtree(package)
package.mkdir();checksums={}
for name,source in FILES.items():
    raw=source.read_bytes()
    if raw[:5]!=b'\x7fELF\x02' or int.from_bytes(raw[18:20],'little')!=183:
        raise SystemExit('Refusing a non-Linux ARM64 binary: '+str(source))
    target=package/'payload'/name;target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target);checksums[name]=hashlib.sha256(raw).hexdigest()
manifest={'schema':1,'experimental':True,'platform':'tg5040','revision':revision,
          'versions':json.loads((ROOT/'ports/gba-link/versions.json').read_text()),'files':checksums}
(package/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
shutil.copy2(ROOT/'ports/gba-link/install.py',package/'install.py')
shutil.copy2(ROOT/'ports/gba-link/README.md',package/'README.md')
shutil.copy2(ROOT/'ports/gba-link/compatibility.md',package/'compatibility.md')
shutil.copy2(ROOT/'ports/gba-link/GAME-VALIDATION.md',package/'GAME-VALIDATION.md')
for name in ('LOCAL-TESTING.md', 'HARDWARE-VALIDATION.md', 'RFU-EXCHANGE-FIX.md', 'RFU-TIMING-FIX.md', 'RFU-FLOWER-INVESTIGATION.md'):
    shutil.copy2(ROOT/'ports/gba-link'/name, package/name)
source_dir=package/'source';source_dir.mkdir()
(source_dir/'REVISION.txt').write_text(revision+'\n')
(source_dir/'BUILD.md').write_text('Extract nextui-source.tar.gz into a fresh directory. Dependencies are fetched at pinned commits by build.py.\nRun python3 ports/gba-link/build.py brick --revision '+revision+'\nFor Mac: python3 ports/gba-link/build.py mac. Docker SDK, Apple clang, Homebrew OpenSSL/SDL2 and Python are required.\nThe included dependency archives supply the source of linked libraries; the normal script uses pinned Git checkouts.\n')
shutil.copy2(OUT/'gpsp-source.tar.gz',source_dir/'gpsp-source.tar.gz')
subprocess.run(['git','archive','--format=tar.gz','-o',str(source_dir/'nextui-source.tar.gz'),revision],cwd=ROOT,check=True)
for name,folder in [('libretro-common','libretro-common'),('libchdr','libchdr'),('rcheevos','rcheevos/src')]:
    subprocess.run(['git','archive','--format=tar.gz','-o',str(source_dir/(name+'-source.tar.gz')),'HEAD'],
                   cwd=ROOT/'workspace/all/minarch'/folder,check=True)
licenses=package/'licenses';licenses.mkdir()
for candidate in ('LICENSE','LICENSE.md','LICENSE.txt','COPYING'):
    if (ROOT/candidate).is_file():shutil.copy2(ROOT/candidate,licenses/('NextUI-'+candidate))
gpsp=next((OUT/'brick-source').glob('gpsp-*'))
for candidate in ('COPYING','LICENSE','LICENSE.txt'):
    if (gpsp/candidate).is_file():shutil.copy2(gpsp/candidate,licenses/('gpSP-'+candidate))
# The shipped helper statically links Go and its pinned BSD dependencies.
goroot=Path(subprocess.check_output(['go','env','GOROOT'],text=True).strip())
go_license=next((path for path in (goroot/'LICENSE',goroot.parent/'LICENSE') if path.is_file()),None)
if go_license is None:raise SystemExit('Go runtime LICENSE is missing; refuse incomplete distribution')
shutil.copy2(go_license,licenses/'Go-LICENSE')
for module,label in [('github.com/godbus/dbus/v5','godbus'),('golang.org/x/sys','x-sys')]:
    info=json.loads(subprocess.check_output(['go','list','-m','-json',module],cwd=ROOT/'ports/gba-link/bluetooth',text=True))
    shutil.copy2(Path(info['Dir'])/'LICENSE',licenses/(label+'-LICENSE'))
tests=package/'tests';tests.mkdir()
shutil.copy2(OUT/'link-probe.gba',tests/'link-probe.gba')
if (OUT/'ime-probe.gba').is_file():shutil.copy2(OUT/'ime-probe.gba',tests/'ime-probe.gba')
records=package/'validation';records.mkdir()
for name in ('test-results.json','regression.log','arm-host.log','arm-join.log','arm-test.log','netem-test.log','netem-host.log','netem-join.log','game-validation.json','install-test.log','save-guard-test.log','tennis-wireless-host.log','tennis-wireless-join.log','tennis-wireless-host-input.txt','tennis-wireless-join-input.txt',
             'rfu-queue-before.log', 'rfu-queue-after.log', 'rfu-queue-regression.log',
             'rfu-local-validation.json', 'rfu-local-host.log', 'rfu-local-join.log',
             'rfu-local-proxy.log', 'local-launcher-smoke.log',
             'rfu-exchange-validation.json', 'rfu-exchange-regression.log', 'rfu-exchange-adapter.log',
             'rfu-timing-validation.json', 'rfu-timing-regression.log', 'rfu-timing-adapter.log',
             'rfu-flower-validation.json', 'rfu-flower-regression.log',
             'rfu-flower-adapter.log', 'rfu-flower-arm-probe.log',
             'ime-before-mac.json', 'ime-after-mac.json', 'ime-before-arm.json', 'ime-after-arm.json',
             'rfu-ime-validation.json', 'rfu-ime-regression.log', 'rfu-ime-adapter.log', 'rfu-ime-arm-probe.log',
             'rfu-latency-validation.json', 'rfu-latency-regression.log', 'rfu-latency-transport.log', 'rfu-auto-validation.json', 'rfu-auto-regression.log', 'rfu-auto-transport.log'):
    path=OUT/name
    if path.is_file():shutil.copy2(path,records/name)
for name in ('bluetooth-plan.json','bluetooth-validation.json'):
    shutil.copy2(ROOT/'ports/gba-link'/name,records/name)
for name in ('core-bridge-integration.log','frontend-tcp-regression.log','worker-lifecycle.log','install-helper-regression.log','native-tests-current.elf.log','native-worker-lifecycle-results.json','worker-lifecycle-arm.elf.log','worker-lifecycle-arm-v1-failed.log'):
    path=OUT/'bluetooth'/name
    if path.is_file():shutil.copy2(path,records/name)
(package/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(package))+'\n'
                                      for p in sorted(package.rglob('*')) if p.is_file()))
archive=package.with_suffix('.zip')
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as z:
    for path in package.rglob('*'):
        if path.is_file():z.write(path,str(path.relative_to(package.parent)))
archive.with_suffix('.zip.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
print(archive)
