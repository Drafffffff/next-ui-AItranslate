#!/bin/sh
set -eu
cd "$(dirname "$0")"
SYSTEM_ROOT=${SYSTEM_PATH:-/mnt/SDCARD/.system/tg5040}
export LD_LIBRARY_PATH="$SYSTEM_ROOT/lib:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
LOG_DIR=${SHARED_USERDATA_PATH:-/mnt/SDCARD/.userdata/shared}/pocketjs-brick
mkdir -p "$LOG_DIR/creative"
export POCKETJS_DATA="$LOG_DIR/creative"
export POCKETJS_FONT="${RES_PATH:-/mnt/SDCARD/.system/res}/font1.ttf"
export POCKETJS_HARDWARE=1
exec ./pocketjs-app.elf > "$LOG_DIR/creative.log" 2>&1
