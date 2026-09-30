#!/bin/sh
# Launch from NextUI Tools on TrimUI Brick.
set -eu
cd "$(dirname "$0")"
if [ "${DEVICE:-}" != brick ] && [ "${TRIMUI_MODEL:-}" != 'Trimui Brick' ]; then
    echo 'PocketJS App: only NextUI on TrimUI Brick is supported.' >&2
    exit 1
fi
SYSTEM_ROOT="${SYSTEM_PATH:-/mnt/SDCARD/.system/tg5040}"
export LD_LIBRARY_PATH="$SYSTEM_ROOT/lib:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
LOG_DIR="${SHARED_USERDATA_PATH:-/mnt/SDCARD/.userdata/shared}/pocketjs-brick"
mkdir -p "$LOG_DIR"
exec ./pocketjs-app.elf > "$LOG_DIR/app.log" 2>&1
