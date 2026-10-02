#!/bin/sh
# Composable NextUI boot hook. The marker is consumed before launch so a broken
# package cannot trap the user in a boot loop; MENU exits back into NextUI.
set -eu
[ "${DEVICE:-}" = brick ] || exit 0
SHARED="${SHARED_USERDATA_PATH:-/mnt/SDCARD/.userdata/shared}"
MARKER="$SHARED/brick-mic/resume.json"
APP="${SDCARD_PATH:-/mnt/SDCARD}/Tools/tg5040/Brick Mic.pak/launch.sh"
[ -f "$MARKER" ] && [ -x "$APP" ] || exit 0
rm -f "$MARKER"
sync
"$APP"
