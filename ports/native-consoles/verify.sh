#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
TASK_MOUNTS=(-v "$TASK_ROOT:/work")
if [ $# -gt 0 ]; then TASK_MOUNTS+=(-v "$1:/pico:ro"); fi
docker run --rm --platform linux/arm64 "${TASK_MOUNTS[@]}" \
    -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy \
    nextui-pocketjs-verify:local /bin/bash -c '
    set -eu
    export LD_LIBRARY_PATH=/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu
    export LD_PRELOAD=/work/build/native-consoles/TIC-80.pak/sdl-nosensor.so
    mkdir -p /tmp/tic-check /tmp/pico-check
    check_quit() {
        /work/build/native-consoles/TIC-80.pak/menu-exit.elf "$@" > /tmp/quit.log 2>&1 &
        task_pid=$!
        sleep 2
        kill -USR1 "$task_pid"
        wait "$task_pid"
        cat /tmp/quit.log
        grep -q "MENU exit: child status 0" /tmp/quit.log
        if grep -q "app unresponsive" /tmp/quit.log; then exit 1; fi
    }
    check_quit /work/build/native-consoles/TIC-80.pak/tic80.elf --skip --soft --fs /tmp/tic-check
    echo "PASS: TIC-80 console normal SDL exit without forced termination"
    if [ -f /pico/pico8_64 ]; then
        printf "blit_method 1\nwindowed 1\n" > /tmp/pico-check/config.txt
        check_quit /pico/pico8_64 -home /tmp/pico-check
        echo "PASS: PICO-8 console normal SDL exit without forced termination"
    fi
    '
