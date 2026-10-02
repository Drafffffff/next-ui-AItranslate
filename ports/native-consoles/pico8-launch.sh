#!/bin/sh
set -eu
cd "$(dirname "$0")"
TASK_DATA="${SHARED_USERDATA_PATH:-/mnt/SDCARD/.userdata/shared}/pico8-studio"
TASK_RUNTIME=/mnt/SDCARD/Emus/tg5040/PICO.pak/pico8
if [ ! -f "$TASK_RUNTIME/pico8_64" ]; then TASK_RUNTIME=/mnt/SDCARD/Bios/PICO; fi
mkdir -p "$TASK_DATA/cdata" /mnt/SDCARD/Screenshots
if [ ! -f "$TASK_DATA/config.txt" ]; then cp ./config.txt "$TASK_DATA/config.txt"; fi
sed 's/^use_wget .*/use_wget 1/' "$TASK_DATA/config.txt" > "$TASK_DATA/config.txt.tmp"
mv "$TASK_DATA/config.txt.tmp" "$TASK_DATA/config.txt"
cp ./sdl_controllers.txt "$TASK_DATA/sdl_controllers.txt"
export PATH="$PWD/bin:$PATH"
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/tg5040/lib:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_PRELOAD="$PWD/sdl-nosensor.so${LD_PRELOAD:+:$LD_PRELOAD}"
TASK_STATUS=0
./menu-exit.elf "$TASK_RUNTIME/pico8_64" -home "$TASK_DATA" -root_path "/mnt/SDCARD/Roms/Pico-8 (PICO)" -desktop /mnt/SDCARD/Screenshots -joystick 0 -splore > "$TASK_DATA/app.log" 2>&1 || TASK_STATUS=$?
unset LD_PRELOAD
sh ./sync-pico8-favourites.sh "$TASK_DATA" "/mnt/SDCARD/Roms/Pico-8 (PICO)/Favourites" >> "$TASK_DATA/favourites-sync.log" 2>&1 || true
exit "$TASK_STATUS"
