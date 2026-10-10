#!/bin/bash
# The supplied paid runtime is mounted read-only for local verification only.
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
TASK_RUNTIME=${1:?Provide the locally installed lawful Raspberry Pi 64-bit PICO-8 directory}
TASK_RUNTIME=$(cd "$TASK_RUNTIME" && pwd -P)
test -x "$TASK_RUNTIME/pico8_64"
test -r "$TASK_RUNTIME/pico8.dat"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work:ro" -v "$TASK_RUNTIME:/pico:ro" \
    -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy \
    nextui-pocketjs-verify:local /bin/bash -euc '
    export LD_LIBRARY_PATH=/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu
    mkdir -p /tmp/pico-check
    printf "blit_method 1\nwindowed 1\n" > /tmp/pico-check/config.txt
    printf "pico-8 cartridge // http://www.pico-8.com\nversion 42\n__lua__\nfunction _draw() cls(1) print(\"launcher check\",8,8,7) end\n" > /tmp/pico-check/fixture.p8
    check_quit() {
        env LD_PRELOAD=/work/build/native-consoles/PICO-8.pak/sdl-nosensor.so \
            /work/build/native-consoles/PICO-8.pak/menu-exit.elf /pico/pico8_64 \
            -home /tmp/pico-check "$@" > /tmp/pico-check/quit.log 2>&1 &
        task_pid=$!
        sleep 2
        kill -USR1 "$task_pid"
        wait "$task_pid"
        cat /tmp/pico-check/quit.log
        grep -q "MENU exit: child status 0" /tmp/pico-check/quit.log
        if grep -q "app unresponsive" /tmp/pico-check/quit.log; then exit 1; fi
    }
    check_quit
    check_quit -run /tmp/pico-check/fixture.p8
    echo "PASS: official PICO-8 console and local cartridge exit normally through SDL; no forced termination"
    '
