#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
OUTPUT="$TASK_ROOT/build/brick-mic"
PKG="$OUTPUT/Brick Mic.pak"
mkdir -p "$PKG"
(cd "$TASK_ROOT/ports/brick-mic/brick" && GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' -o "$PKG/brick-micd" .)
"$TASK_ROOT/ports/brick-mic/build-ui.sh" brick
cp "$TASK_ROOT/ports/brick-mic/launch.sh" "$PKG/launch.sh"
cp "$TASK_ROOT/ports/brick-mic/start-services.sh" "$PKG/start-services.sh"
cp "$TASK_ROOT/ports/brick-mic/resume.sync.sh" "$PKG/resume.sync.sh"
cp "$TASK_ROOT/LICENSE" "$PKG/NEXTUI-LICENSE.txt"
cp "$TASK_ROOT/ports/brick-mic/README.md" "$PKG/README.md"
rm -f "$PKG/brick-mic.elf"
chmod +x "$PKG/launch.sh" "$PKG/start-services.sh" "$PKG/resume.sync.sh" "$PKG/pocketjs-mic.elf" "$PKG/brick-micd"
printf 'Built: %s\n' "$PKG"
