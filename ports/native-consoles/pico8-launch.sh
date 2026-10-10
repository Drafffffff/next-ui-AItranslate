#!/bin/sh
# NEXTUI_PICO8_UNIFIED_RUNNER_V1
# One official PICO-8 entry point for Tools, Emus and workshop cartridges.
set -eu

TASK_PAK=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
TASK_SD=${SDCARD_PATH:-/mnt/SDCARD}
TASK_PLATFORM=${PLATFORM:-tg5040}
TASK_SYSTEM=${SYSTEM_PATH:-$TASK_SD/.system/$TASK_PLATFORM}
TASK_BIOS=${BIOS_PATH:-$TASK_SD/Bios}
TASK_SHARED=${SHARED_USERDATA_PATH:-$TASK_SD/.userdata/shared}
TASK_DATA="$TASK_SHARED/pico8-studio"
TASK_RUNTIME=${PICO8_RUNTIME_PATH:-}

if [ -z "$TASK_RUNTIME" ]; then
    for TASK_CANDIDATE in "$TASK_SD/Emus/$TASK_PLATFORM/PICO.pak/pico8" "$TASK_BIOS/PICO"; do
        if [ -f "$TASK_CANDIDATE/pico8_64" ] && [ -x "$TASK_CANDIDATE/pico8_64" ] &&
           [ -f "$TASK_CANDIDATE/pico8.dat" ] && [ -r "$TASK_CANDIDATE/pico8.dat" ]; then
            TASK_RUNTIME=$TASK_CANDIDATE
            break
        fi
    done
fi

# This probe must not create data directories, change configuration or run the
# commercial binary. Callers can safely use it to choose the fallback runtime.
if [ -z "$TASK_RUNTIME" ] || [ ! -f "$TASK_RUNTIME/pico8_64" ] || [ ! -x "$TASK_RUNTIME/pico8_64" ] ||
   [ ! -f "$TASK_RUNTIME/pico8.dat" ] || [ ! -r "$TASK_RUNTIME/pico8.dat" ] ||
   [ ! -f "$TASK_PAK/menu-exit.elf" ] || [ ! -x "$TASK_PAK/menu-exit.elf" ] ||
   [ ! -f "$TASK_PAK/sdl-nosensor.so" ] || [ ! -r "$TASK_PAK/sdl-nosensor.so" ] ||
   [ ! -f "$TASK_PAK/config.txt" ] || [ ! -r "$TASK_PAK/config.txt" ] ||
   [ ! -f "$TASK_PAK/sdl_controllers.txt" ] || [ ! -r "$TASK_PAK/sdl_controllers.txt" ]; then
    [ "${1:-}" = --check ] || printf '%s\n' 'Official PICO-8 runtime or launcher support files are missing.' >&2
    exit 1
fi
if [ "${1:-}" = --check ]; then
    [ "$#" -eq 1 ] || exit 64
    exit 0
fi
[ "$#" -le 1 ] || exit 64
TASK_CART=${1:-}
if [ -n "$TASK_CART" ]; then
    case "$TASK_CART" in /*) ;; *) printf '%s\n' 'Expected an absolute cartridge path.' >&2; exit 64 ;; esac
    [ -f "$TASK_CART" ] || { printf '%s\n' 'Cartridge does not exist.' >&2; exit 66; }
fi

# Prefer the directory actually installed on this SD card. Never introduce a
# second Pico-8/PICO-8 folder merely because its spelling differs.
TASK_ROMS=${PICO8_ROMS_PATH:-}
if [ -z "$TASK_ROMS" ]; then
    for TASK_CANDIDATE in "$TASK_SD/Roms/"*"(PICO)"; do
        [ -d "$TASK_CANDIDATE" ] || continue
        case "${TASK_CANDIDATE##*/}" in
            'PICO-8 (PICO)'|'Pico-8 (PICO)') TASK_ROMS=$TASK_CANDIDATE; break ;;
        esac
        [ -n "$TASK_ROMS" ] || TASK_ROMS=$TASK_CANDIDATE
    done
fi
TASK_ROMS=${TASK_ROMS:-$TASK_SD/Roms/PICO-8 (PICO)}
TASK_SHOTS="$TASK_SD/Screenshots"
mkdir -p "$TASK_DATA/cdata" "$TASK_SHOTS" "$TASK_ROMS"

# Seed only on first launch. Preserve the user's existing settings, mappings,
# cdata_path, downloads and saves. PICO-8 itself may update its config on exit.
if [ ! -e "$TASK_DATA/config.txt" ] && [ ! -L "$TASK_DATA/config.txt" ]; then
    TASK_CONFIG_ROOT=$TASK_ROMS TASK_CONFIG_CDATA="$TASK_DATA/cdata" awk '
        /^root_path[[:space:]]/ { print "root_path " ENVIRON["TASK_CONFIG_ROOT"] "/"; next }
        /^cdata_path[[:space:]]/ { print "cdata_path " ENVIRON["TASK_CONFIG_CDATA"] "/"; next }
        { print }
    ' "$TASK_PAK/config.txt" > "$TASK_DATA/config.txt"
fi
if [ ! -e "$TASK_DATA/sdl_controllers.txt" ] && [ ! -L "$TASK_DATA/sdl_controllers.txt" ]; then
    cp "$TASK_PAK/sdl_controllers.txt" "$TASK_DATA/sdl_controllers.txt"
fi

export SDCARD_PATH="$TASK_SD" PLATFORM="$TASK_PLATFORM" SYSTEM_PATH="$TASK_SYSTEM"
export PATH="$TASK_PAK/bin:$PATH"
export LD_LIBRARY_PATH="$TASK_SYSTEM/lib:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
TASK_PRELOAD="$TASK_PAK/sdl-nosensor.so${LD_PRELOAD:+:$LD_PRELOAD}"
TASK_STATUS=0
if [ -n "$TASK_CART" ]; then
    env LD_PRELOAD="$TASK_PRELOAD" "$TASK_PAK/menu-exit.elf" "$TASK_RUNTIME/pico8_64" \
        -home "$TASK_DATA" -root_path "$TASK_ROMS" -desktop "$TASK_SHOTS" -joystick 0 \
        -run "$TASK_CART" > "$TASK_DATA/app.log" 2>&1 || TASK_STATUS=$?
else
    env LD_PRELOAD="$TASK_PRELOAD" "$TASK_PAK/menu-exit.elf" "$TASK_RUNTIME/pico8_64" \
        -home "$TASK_DATA" -root_path "$TASK_ROMS" -desktop "$TASK_SHOTS" -joystick 0 \
        -splore > "$TASK_DATA/app.log" 2>&1 || TASK_STATUS=$?
    # Downloads and favourites belong to Splore; ordinary local games do not
    # scan caches, access the network or mirror the entire PICO game library.
    sh "$TASK_PAK/sync-pico8-favourites.sh" "$TASK_DATA" "$TASK_ROMS/Favourites" \
        >> "$TASK_DATA/favourites-sync.log" 2>&1 || :
fi
exit "$TASK_STATUS"
