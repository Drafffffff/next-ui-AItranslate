#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../../.." && pwd -P)
POCKETJS_MAC_DIR=creative-mac POCKETJS_APP_SOURCE="$TASK_ROOT/ports/pocketjs-brick/creative/app.tsx" "$TASK_ROOT/ports/pocketjs-brick/build-mac.sh"
MAC="$TASK_ROOT/build/pocketjs-port/creative-mac"
mkdir -p "$MAC/data"
export POCKETJS_DATA="$MAC/data" POCKETJS_FONT="$TASK_ROOT/fonts/font1.ttf" POCKETJS_HARDWARE=0
cd "$MAC"
exec "$MAC/PocketJS Preview.app/Contents/MacOS/pocketjs-app"
