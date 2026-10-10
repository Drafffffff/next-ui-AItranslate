#!/bin/sh
# NEXTUI_P8_UNIFIED_RUNNER_V1
# Both NextUI's P8 menu and the workshop enter through this launcher.
set -u
TASK_PAK=$(cd "$(dirname "$0")" && pwd) || exit 1
export PLATFORM="${PLATFORM:-tg5040}"
export SDCARD_PATH="${SDCARD_PATH:-/mnt/SDCARD}"
export SYSTEM_PATH="${SYSTEM_PATH:-$SDCARD_PATH/.system/$PLATFORM}"
export USERDATA_PATH="${USERDATA_PATH:-$SDCARD_PATH/.userdata/$PLATFORM}"
export SHARED_USERDATA_PATH="${SHARED_USERDATA_PATH:-$SDCARD_PATH/.userdata/shared}"
export BIOS_PATH="${BIOS_PATH:-$SDCARD_PATH/Bios}"
export SAVES_PATH="${SAVES_PATH:-$SDCARD_PATH/Saves}"
export CHEATS_PATH="${CHEATS_PATH:-$SDCARD_PATH/Cheats}"
export LOGS_PATH="${LOGS_PATH:-$USERDATA_PATH/logs}"
export PATH="$SYSTEM_PATH/bin:/usr/trimui/bin:$PATH"
TASK_NATIVE="$SDCARD_PATH/Tools/$PLATFORM/PICO-8.pak/launch.sh"
TASK_RUNNER=
if [ -r "$TASK_PAK/fake08_libretro.so" ] && command -v minarch.elf >/dev/null 2>&1; then
    TASK_RUNNER=fake08
elif [ -x "$TASK_NATIVE" ] && grep -q NEXTUI_PICO8_UNIFIED_RUNNER_V1 "$TASK_NATIVE" && "$TASK_NATIVE" --check >/dev/null 2>&1; then
    TASK_RUNNER=pico8
fi

# Readiness does not create directories, start SDL, or touch resume state.
if [ "${1:-}" = --check ]; then [ -n "$TASK_RUNNER" ]; exit $?; fi
TASK_ROM=${1:-}
if [ -z "$TASK_RUNNER" ] || [ ! -f "$TASK_ROM" ]; then
    printf '%s\n' 'P8: runtime or cartridge missing' >&2
    exit 1
fi

# A generated cartridge changes in place. A fake-08 savestate also contains
# the previous Lua VM, so resuming it would silently undo the latest edit.
# Keep ordinary P8 games' resume behavior and all their save files intact.
case "$TASK_ROM" in
    "$SDCARD_PATH/Roms/AI生成游戏 (P8)/"*) rm -f /tmp/resume_slot.txt ;;
esac
mkdir -p "$LOGS_PATH" || exit 1
printf 'runtime=%s\ncart=%s\n' "$TASK_RUNNER" "$TASK_ROM" > "$LOGS_PATH/P8-launch.txt"
if [ "$TASK_RUNNER" = pico8 ]; then
    # Do not retry a broken cart in another engine; preserve the real error.
    exec "$TASK_NATIVE" "$TASK_ROM"
fi

TASK_TAG=$(basename "$TASK_PAK" .pak)
mkdir -p "$BIOS_PATH/$TASK_TAG" "$SAVES_PATH/$TASK_TAG" "$CHEATS_PATH/$TASK_TAG" "$USERDATA_PATH" || exit 1
cd "$USERDATA_PATH" || exit 1
exec minarch.elf "$TASK_PAK/fake08_libretro.so" "$TASK_ROM" > "$LOGS_PATH/$TASK_TAG.txt" 2>&1
