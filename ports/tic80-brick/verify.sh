#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
# Prepare the desktop SDL reference image using the existing verification Dockerfile.
docker build --platform linux/arm64 -f "$TASK_ROOT/ports/pocketjs-brick/verify.Dockerfile" -t nextui-pocketjs-verify:local "$TASK_ROOT/ports/pocketjs-brick"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" \
    -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy \
    -e TIC80_TEST_FRAMES=120 -e TIC80_TEST_DUMP=/work/build/creative-lab/tic80.bmp \
    nextui-pocketjs-verify:local /bin/bash -c '
        set -eu
        cd "/work/build/creative-lab/TIC-80 Lab.pak"
        mkdir -p /tmp/tic80-check
        # Use the desktop loader normally: an explicit loader invocation makes
        # SDL_GetBasePath point to /lib and TIC-80 scan unrelated shared objects.
        LD_LIBRARY_PATH=/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu \
            ./tic80.elf "$PWD/sandbox.lua" --skip --soft --fs /tmp/tic80-check
        python3 - <<'\''PY'\''
import struct
from pathlib import Path
p=Path("/work/build/creative-lab/tic80.bmp").read_bytes()
assert p[:2]==b"BM"
offset=struct.unpack_from("<I",p,10)[0]
w,h=struct.unpack_from("<ii",p,18)
assert (w,h)==(256,144)
assert len({p[i:i+4] for i in range(offset,len(p)-3,4)})>=5
print("PASS: Lua sandbox produced a nonblank core frame with sand and water")
PY
'
