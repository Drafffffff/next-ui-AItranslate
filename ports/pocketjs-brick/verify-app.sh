#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
docker build --platform linux/arm64 -f "$TASK_ROOT/ports/pocketjs-brick/verify.Dockerfile" \
    -t nextui-pocketjs-verify:local "$TASK_ROOT/ports/pocketjs-brick"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" nextui-pocketjs-verify:local \
    python3 /work/ports/pocketjs-brick/verify-chat.py
# Device SDL has only mali. Dummy presentation uses desktop SDL, not device proof.
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" nextui-pocketjs-verify:local /bin/bash -lc '
    set -euo pipefail
    cd "/work/build/pocketjs-port/PocketJS App.pak"
    OUTPUT=/work/build/pocketjs-port/validation
    TEST_DATA=$(mktemp -d "$OUTPUT/presentation-XXXXXX")
    trap '\''rm -rf "$TEST_DATA"'\'' EXIT
    export POCKETJS_DATA="$TEST_DATA" POCKETJS_FONT=/work/fonts/font1.ttf
    /lib/ld-linux-aarch64.so.1 --library-path /lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu \
        ./pocketjs-app.elf --dummy-video --frames 8 --dump "$OUTPUT/chat-initial.ppm"
    test -s "$TEST_DATA/config.json"
'
