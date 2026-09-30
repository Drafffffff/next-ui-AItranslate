#!/bin/sh
set -eu
cd "$(dirname "$0")"
TASK_DATA="${SHARED_USERDATA_PATH:-/mnt/SDCARD/.userdata/shared}/tic80-studio"
mkdir -p "$TASK_DATA"
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/tg5040/lib:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export TIC80_BRICK=1
exec ./tic80.elf --skip --fullscreen --fs "$TASK_DATA" > "$TASK_DATA/app.log" 2>&1
