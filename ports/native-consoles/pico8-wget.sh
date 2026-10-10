#!/bin/sh
set -eu
TASK_PAK=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
TASK_SD=${SDCARD_PATH:-/mnt/SDCARD}
TASK_PLATFORM=${PLATFORM:-tg5040}
TASK_EMU="$TASK_SD/Emus/$TASK_PLATFORM/PICO.pak"
TASK_WGET="$TASK_EMU/bin/$TASK_PLATFORM/wget-original"
if [ ! -x "$TASK_WGET" ]; then
    printf '%s\n' 'The installed PICO package HTTPS downloader is unavailable.' >&2
    exit 127
fi
# Keep community pak libraries scoped to its downloader. PICO-8 continues to
# use the device SDL, and HTTPS certificate verification stays enabled.
export LD_LIBRARY_PATH="$TASK_EMU/lib/$TASK_PLATFORM:$TASK_EMU/lib/arm64:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
unset LD_PRELOAD
exec "$TASK_WGET" --ca-certificate="$TASK_PAK/certs/cacert.pem" --timeout=15 --tries=1 "$@"
