#!/bin/sh
# NEXTUI_PICO8_UNIFIED_BRIDGE
set -eu
TASK_PAK=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
TASK_SD=${SDCARD_PATH:-/mnt/SDCARD}
TASK_PLATFORM=${PLATFORM:-tg5040}
TASK_TOOLS="$TASK_SD/Tools/$TASK_PLATFORM/PICO-8.pak/launch.sh"
TASK_P8="$TASK_SD/Emus/$TASK_PLATFORM/P8.pak/launch.sh"
if [ ! -x "$TASK_P8" ]; then
    TASK_P8="${SYSTEM_PATH:-$TASK_SD/.system/$TASK_PLATFORM}/paks/Emus/P8.pak/launch.sh"
fi
TASK_CART=${1:-}

# Splore is the official online browser. All local cartridges share the P8
# runner with the workshop, which prefers fake-08. Check markers first: older
# Tools launchers ignore arguments and would actually open Splore for --check.
TASK_SPLORE=0
case "${TASK_CART##*/}" in
    ''|Splore.p8|Splore.p8.png|splore.p8|splore.p8.png) TASK_SPLORE=1 ;;
esac
if [ "$TASK_SPLORE" -eq 0 ] && [ -x "$TASK_P8" ] &&
   grep -q '^# NEXTUI_P8_UNIFIED_RUNNER_V1$' "$TASK_P8" && "$TASK_P8" --check; then
    exec "$TASK_P8" "$@"
fi
if [ -x "$TASK_TOOLS" ] &&
   grep -q '^# NEXTUI_PICO8_UNIFIED_RUNNER_V1$' "$TASK_TOOLS" && "$TASK_TOOLS" --check; then
    if [ "$TASK_SPLORE" -eq 1 ]; then exec "$TASK_TOOLS"; fi
    exec "$TASK_TOOLS" "$@"
fi

# Deployment saves the vendor script once. Do not route the fallback back to
# this bridge or replace it on reinstall; it may contain user customizations.
if [ -x "$TASK_PAK/launch-vendor.sh" ] &&
   ! grep -q 'NEXTUI_PICO8_.*BRIDGE' "$TASK_PAK/launch-vendor.sh"; then
    exec "$TASK_PAK/launch-vendor.sh" "$@"
fi
printf '%s\n' 'Neither the official PICO-8 Tools launcher nor the original vendor launcher is available.' >&2
exit 1
