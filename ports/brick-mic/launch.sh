#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ "${DEVICE:-}" != brick ] && [ "${TRIMUI_MODEL:-}" != 'Trimui Brick' ]; then
    echo 'Brick Mic requires NextUI on TrimUI Brick.' >&2
    exit 1
fi
SYSTEM_ROOT="${SYSTEM_PATH:-/mnt/SDCARD/.system/tg5040}"
export PATH="$SYSTEM_ROOT/bin:/usr/bin:/bin:$PATH"
export LD_LIBRARY_PATH="$SYSTEM_ROOT/lib:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export BRICK_MIC_FONT="${RES_PATH:-/mnt/SDCARD/.system/res}/font1.ttf"
export BRICK_MIC_SETTINGS="${SHARED_USERDATA_PATH:-/mnt/SDCARD/.userdata/shared}/minuisettings.txt"
DATA="${SHARED_USERDATA_PATH:-/mnt/SDCARD/.userdata/shared}/brick-mic"
mkdir -p "$DATA"
export POCKETJS_DATA="$DATA/font-cache"
export BRICK_MIC_HOSTS="$DATA/receivers.json"
export BRICK_MIC_RESUME="$DATA/resume.json"
export BRICK_MIC_RECEIVER_RECORD=/tmp/brick-mic-receiver
exec >>"$DATA/app.log" 2>&1
HOOK_DIR="${USERDATA_PATH:-/mnt/SDCARD/.userdata/tg5040}/.hooks/boot.d"
mkdir -p "$HOOK_DIR"
if ! cmp -s ./resume.sync.sh "$HOOK_DIR/60-brick-mic.sync.sh"; then
    cp ./resume.sync.sh "$HOOK_DIR/60-brick-mic.sync.sh.new"
    chmod +x "$HOOK_DIR/60-brick-mic.sync.sh.new"
    mv "$HOOK_DIR/60-brick-mic.sync.sh.new" "$HOOK_DIR/60-brick-mic.sync.sh"
fi
BT_WAS_RUNNING=0
system_bluetooth_enabled() {
    grep -q '^bluetooth=1$' "$BRICK_MIC_SETTINGS" 2>/dev/null
}
if system_bluetooth_enabled || pidof bluetoothd >/dev/null 2>&1; then BT_WAS_RUNNING=1; fi
UI_PID=""
cleanup() {
    trap - EXIT INT TERM
    if [ -n "$UI_PID" ]; then kill "$UI_PID" 2>/dev/null || true; wait "$UI_PID" 2>/dev/null || true; fi
    if [ "$BT_WAS_RUNNING" = 0 ] && ! system_bluetooth_enabled; then "$SYSTEM_ROOT/etc/bluetooth/bt_init.sh" stop || true; fi
}
trap cleanup EXIT INT TERM
# The UI presents Loading before its background worker starts Bluetooth.
STATUS=0
./pocketjs-mic.elf "$@" &
UI_PID=$!
wait "$UI_PID" || STATUS=$?
if [ "$STATUS" = 42 ]; then
    cleanup
    touch /tmp/poweroff
    sync
    exec poweroff_next
fi
exit "$STATUS"
