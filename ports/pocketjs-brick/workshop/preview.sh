#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../../.." && pwd -P)
OUTPUT="$TASK_ROOT/build/pocketjs-port/workshop-mac"
if [ ! -x "$OUTPUT/pocketjs-app" ]; then
    POCKETJS_APP_SOURCE="$TASK_ROOT/ports/pocketjs-brick/workshop/app.tsx" POCKETJS_MAC_DIR=workshop-mac "$TASK_ROOT/ports/pocketjs-brick/build-mac.sh"
fi
SD="${WORKSHOP_PREVIEW_SD:-$OUTPUT/sdcard}"
mkdir -p "$SD/.userdata/shared/ai-game-workshop" "$SD/Roms/AI生成游戏 (P8)"
export SHARED_USERDATA_PATH="$SD/.userdata/shared" POCKETJS_WORKSHOP=1
export POCKETJS_DATA="$SHARED_USERDATA_PATH/ai-game-workshop" POCKETJS_ROMS="$SD/Roms/AI生成游戏 (P8)"
export POCKETJS_FONT="$TASK_ROOT/fonts/font1.ttf" POCKETJS_CA="$TASK_ROOT/build/pocketjs-port/ca-bundle.crt"
cd "$OUTPUT"
exec ./pocketjs-app
