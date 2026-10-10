#!/bin/sh
# NextUI waits for this wrapper. UI and emulator take turns owning SDL/input.
set -u
cd "$(dirname "$0")" || exit 1
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
export LD_LIBRARY_PATH="$SYSTEM_PATH/lib:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export POCKETJS_WORKSHOP=1
export POCKETJS_VOICE=1
export POCKETJS_DATA="$SHARED_USERDATA_PATH/ai-game-workshop"
export POCKETJS_ROMS="$SDCARD_PATH/Roms/AI生成游戏 (P8)"
export POCKETJS_FONT="$SDCARD_PATH/.system/res/font1.ttf"
export POCKETJS_CA="$PWD/ca-bundle.crt"
mkdir -p "$POCKETJS_DATA/projects" "$SDCARD_PATH/Roms" "$POCKETJS_ROMS" "$LOGS_PATH" || exit 1
find_runner() {
    p8_dir="$SDCARD_PATH/Emus/$PLATFORM/P8.pak"
    if [ ! -x "$p8_dir/launch.sh" ]; then p8_dir="$SYSTEM_PATH/paks/Emus/P8.pak"; fi
    unset POCKETJS_P8_LAUNCH
    unset POCKETJS_P8_CORE
    # The thumbnail renderer and the interactive game need separate readiness.
    # Official PICO-8 can run even when the fake-08 thumbnail core is missing.
    if [ -r "$p8_dir/fake08_libretro.so" ]; then
        export POCKETJS_P8_CORE="$p8_dir/fake08_libretro.so"
    elif [ -r "$SYSTEM_PATH/paks/Emus/P8.pak/fake08_libretro.so" ]; then
        export POCKETJS_P8_CORE="$SYSTEM_PATH/paks/Emus/P8.pak/fake08_libretro.so"
    fi
    if [ -x "$p8_dir/launch.sh" ]; then
        if grep -q NEXTUI_P8_UNIFIED_RUNNER_V1 "$p8_dir/launch.sh"; then
            if "$p8_dir/launch.sh" --check >/dev/null 2>&1; then export POCKETJS_P8_LAUNCH="$p8_dir/launch.sh"; fi
        elif [ -r "$p8_dir/fake08_libretro.so" ] && command -v minarch.elf >/dev/null 2>&1; then
            # Older installations have no --check; never launch them to probe.
            export POCKETJS_P8_LAUNCH="$p8_dir/launch.sh"
        fi
    fi
}
find_runner
if [ "${1:-}" = --check ]; then
    # Exercise the installed launcher environment, not one recreated by deploy.
    export POCKETJS_HARDWARE=0
    ./pocketjs-app.elf --dummy-video --workshop-ready --dump "$POCKETJS_DATA/launcher-check.ppm" > "$POCKETJS_DATA/launcher-check.log" 2>&1
    check_result=$?
    cat "$POCKETJS_DATA/launcher-check.log"
    exit "$check_result"
fi
while :; do
    # A request is consumed only after a clean UI exit. Never execute its content.
    rm -f "$POCKETJS_DATA/play-request.txt"
    ./pocketjs-app.elf >> "$POCKETJS_DATA/workshop.log" 2>&1
    ui_result=$?
    [ -e /tmp/poweroff ] || [ -e /tmp/reboot ] && break
    [ "$ui_result" -eq 0 ] || break
    [ -f "$POCKETJS_DATA/play-request.txt" ] || break
    project_id=$(cat "$POCKETJS_DATA/play-request.txt")
    rm -f "$POCKETJS_DATA/play-request.txt"
    case "$project_id" in game-*) ;; *) break ;; esac
    case "$project_id" in *[!a-zA-Z0-9-]*) break ;; esac
    rom="$POCKETJS_ROMS/$project_id.p8"
    find_runner
    if [ -z "${POCKETJS_P8_LAUNCH:-}" ] || [ ! -f "$rom" ]; then
        printf '%s\n' '游戏暂时无法启动，请检查 P8 运行器与游戏文件' > "$POCKETJS_DATA/launch-error.txt"
        continue
    fi
    # Workshop previews start the current code rather than an old resume state.
    rm -f /tmp/resume_slot.txt
    if command -v gametimectl.elf >/dev/null 2>&1; then gametimectl.elf start "$rom" >/dev/null 2>&1 || :; fi
    "$POCKETJS_P8_LAUNCH" "$rom"
    game_result=$?
    if command -v gametimectl.elf >/dev/null 2>&1; then gametimectl.elf stop "$rom" >/dev/null 2>&1 || :; fi
    [ -e /tmp/poweroff ] || [ -e /tmp/reboot ] && break
    if [ "$game_result" -ne 0 ]; then printf '%s\n' '游戏退出异常，可以继续修改后再试' > "$POCKETJS_DATA/launch-error.txt"; fi
    export POCKETJS_WORKSHOP_RESUME=1
done
exit "$ui_result"
