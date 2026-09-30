#!/bin/sh
set -eu
TASK_PAK=$(cd "$(dirname "$0")/.." && pwd)
TASK_EMU=/mnt/SDCARD/Emus/tg5040/PICO.pak
# Scope the community pak's download libraries to wget, keeping the SDL used
# by the running console unchanged. Verify HTTPS with our bundled CA roots.
export LD_LIBRARY_PATH="$TASK_EMU/lib/tg5040:$TASK_EMU/lib/arm64:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
unset LD_PRELOAD
exec "$TASK_EMU/bin/tg5040/wget-original" --ca-certificate="$TASK_PAK/certs/cacert.pem" --timeout=15 --tries=1 "$@"
