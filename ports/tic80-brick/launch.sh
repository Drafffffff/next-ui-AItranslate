#!/bin/sh
set -eu
cd "$(dirname "$0")"
export LD_LIBRARY_PATH="${SYSTEM_PATH:-/mnt/SDCARD/.system/tg5040}/lib:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
TASK_DATA="${SHARED_USERDATA_PATH:-/mnt/SDCARD/.userdata/shared}/tic80-brick"
mkdir -p "$TASK_DATA"
export TIC80_BRICK=1
exec ./tic80.elf "$PWD/sandbox.lua" --skip --fullscreen --soft --fs "$TASK_DATA" > "$TASK_DATA/app.log" 2>&1
