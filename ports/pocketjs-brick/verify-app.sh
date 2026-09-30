#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" /bin/bash -lc '
    set -euo pipefail
    OUTPUT=/work/build/pocketjs-port
    cd "$OUTPUT/PocketJS App.pak"
    SYSROOT=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
    mkdir -p "$OUTPUT/validation"
    "$SYSROOT/lib/ld-linux-aarch64.so.1" --library-path "$SYSROOT/lib:$SYSROOT/usr/lib" \
        ./pocketjs-app.elf --self-test --dump "$OUTPUT/validation/app-scrolled.ppm"
    set +e
    "$SYSROOT/lib/ld-linux-aarch64.so.1" --library-path "$SYSROOT/lib:$SYSROOT/usr/lib" ./pocketjs-app.elf --frames 0
    result=$?
    set -e
    test "$result" -eq 2
'
docker build --platform linux/arm64 -f "$TASK_ROOT/ports/pocketjs-brick/verify.Dockerfile" \
    -t nextui-pocketjs-verify:local "$TASK_ROOT/ports/pocketjs-brick"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" nextui-pocketjs-verify:local /bin/bash -lc '
    set -euo pipefail
    cd "/work/build/pocketjs-port/PocketJS App.pak"
    /lib/ld-linux-aarch64.so.1 --library-path /lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu \
        ./pocketjs-app.elf --dummy-video --frames 4 --dump /work/build/pocketjs-port/validation/app-initial.ppm
'
