#!/bin/sh
# NEXTUI_PICO8_SPLORE_BRIDGE
set -eu
TASK_PAK=$(cd "$(dirname "$0")" && pwd)
TASK_CART=${1:-}
case "${TASK_CART##*/}" in
    Splore.p8|Splore.p8.png|splore.p8|splore.p8.png)
        exec sh /mnt/SDCARD/Tools/tg5040/PICO-8.pak/launch.sh
        ;;
esac
# Keep the installed vendor launcher for ordinary cartridge files.
exec sh "$TASK_PAK/launch-vendor.sh" "$@"
