#!/usr/bin/env python3
"""Exercise real NextUI savestate persistence inside the pinned disposable SDK."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
LOCK = json.loads((ROOT / 'ports/gba-link/versions.json').read_text())
SCRIPT = r'''
set -e
cd /work
${CROSS_COMPILE}gcc -std=gnu99 -ffunction-sections -fdata-sections -DUSE_SDL2 -DUSE_GLES -DPLATFORM=\"tg5040\" -Iworkspace/all/minarch -Iworkspace/all/common -Iworkspace/tg5040/platform -Iworkspace/all/minarch/libretro-common/include -I/opt/nextui/include ports/gba-link/tests/save-guard.c workspace/all/minarch/ma_saves.c -Wl,--gc-sections -o build/gba-link/save-guard.elf
sdk=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
"$sdk/lib/ld-2.28.so" --library-path "$sdk/lib:$sdk/usr/lib" build/gba-link/save-guard.elf
'''
output = ROOT / 'build/gba-link/save-guard-test.log'
output.parent.mkdir(parents=True, exist_ok=True)
with output.open('w') as log:
    result = subprocess.run(['docker', 'run', '--rm', '--platform', 'linux/arm64',
        '-v', str(ROOT) + ':/work', LOCK['toolchain'], 'bash', '-lc', SCRIPT],
        stdout=log, stderr=subprocess.STDOUT)
print(output.read_text())
raise SystemExit(result.returncode)
