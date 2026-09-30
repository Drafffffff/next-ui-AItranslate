#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" /bin/bash -lc '
    set -euo pipefail
    OUTPUT=/work/build/pocketjs-port
    BIN="$OUTPUT/PocketJS Smoke.pak/pocketjs-smoke.elf"
    SYSROOT=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
    LOADER="$SYSROOT/lib/ld-linux-aarch64.so.1"
    LIBRARIES="$SYSROOT/lib:$SYSROOT/usr/lib"
    mkdir -p "$OUTPUT/validation"
    "$LOADER" --library-path "$LIBRARIES" "$BIN" --self-test --dump "$OUTPUT/validation/core.bmp"
    set +e
    "$LOADER" --library-path "$LIBRARIES" "$BIN" --frames 0
    result=$?
    set -e
    test "$result" -eq 2
'

# Device SDL has no dummy driver. Exercise the presentation code with an
# ARM64 desktop SDL library, not as evidence of the device display backend.
docker build --platform linux/arm64 -f "$TASK_ROOT/ports/pocketjs-brick/verify.Dockerfile" \
    -t nextui-pocketjs-verify:local "$TASK_ROOT/ports/pocketjs-brick"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" nextui-pocketjs-verify:local /bin/bash -lc '
    set -euo pipefail
    BIN="/work/build/pocketjs-port/PocketJS Smoke.pak/pocketjs-smoke.elf"
    DESKTOP_LIBRARIES=/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu
    POCKETJS_FONT=/work/fonts/font1.ttf /lib/ld-linux-aarch64.so.1 --library-path "$DESKTOP_LIBRARIES" \
        "$BIN" --dummy-video --frames 4
'
