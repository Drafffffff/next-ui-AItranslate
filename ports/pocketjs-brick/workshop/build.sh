#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../../.." && pwd -P)
npm ci --ignore-scripts --prefix "$TASK_ROOT/ports/pocketjs-brick/workshop"
export POCKETJS_APP_SOURCE="$TASK_ROOT/ports/pocketjs-brick/workshop/app.tsx"
export POCKETJS_BUNDLE_DIR=workshop-bundle
export POCKETJS_PACKAGE_NAME='AI Game Workshop.pak'
"$TASK_ROOT/ports/pocketjs-brick/build-app.sh"
PKG="$TASK_ROOT/build/pocketjs-port/$POCKETJS_PACKAGE_NAME"
(cd "$TASK_ROOT/ports/pocketjs-brick/workshop/voice" && GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' -o "$PKG/workshop-voice" .)
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/third-party/LICENSE" "$PKG/LODEPNG-LICENSE.txt"
GO_MOD_CACHE=$(go env GOMODCACHE)
cp "$GO_MOD_CACHE/github.com/gorilla/websocket@v1.5.3/LICENSE" "$PKG/.gorilla-license.tmp"
chmod 644 "$PKG/.gorilla-license.tmp"
mv -f "$PKG/.gorilla-license.tmp" "$PKG/GORILLA-WEBSOCKET-LICENSE.txt"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/launch.sh" "$PKG/launch.sh"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/README.md" "$PKG/README.md"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/icons/LICENSE.txt" "$PKG/LUCIDE-LICENSE.txt"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/PI-LICENSE.txt" "$PKG/PI-LICENSE.txt"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/third-party/bloub/LICENSE" "$PKG/BLOUB-LICENSE.txt"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/third-party/bloub/SOURCE.md" "$PKG/BLOUB-SOURCE.md"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/node_modules/@noble/hashes/LICENSE" "$PKG/NOBLE-HASHES-LICENSE.txt"
# qrcode-generator 1.4.4 ships its license attribution in the source header.
sed -n '1,16p' "$TASK_ROOT/ports/pocketjs-brick/workshop/node_modules/qrcode-generator/qrcode.js" > "$PKG/QRCODE-LICENSE.txt"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/node_modules/diff/LICENSE" "$PKG/JSDIFF-LICENSE.txt"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/node_modules/typebox/license" "$PKG/TYPEBOX-LICENSE.txt"
# Replace the generated resource copy so moved modules do not leave stale guides.
rm -rf "$PKG/skills/pico8-game-dev"
mkdir -p "$PKG/skills/pico8-game-dev"
cp -R "$TASK_ROOT/skills/pico8-game-dev/." "$PKG/skills/pico8-game-dev/"
cp "$TASK_ROOT/LICENSE" "$PKG/SKILL-LICENSE.txt"
chmod +x "$PKG/launch.sh"
"$TASK_ROOT/ports/pocketjs-brick/workshop/build-runtime.sh"
"$TASK_ROOT/ports/native-consoles/build-pico8.sh"
STAGING=$(mktemp -d "$TASK_ROOT/build/pocketjs-port/workshop-install-XXXXXX")
mkdir -p "$STAGING/Tools/tg5040" "$STAGING/Emus/tg5040"
cp -R "$PKG" "$STAGING/Tools/tg5040/"
cp -R "$TASK_ROOT/build/pocketjs-port/P8.pak" "$STAGING/Emus/tg5040/"
cp -R "$TASK_ROOT/build/native-consoles/PICO-8.pak" "$STAGING/Tools/tg5040/"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/README.md" "$STAGING/AI-GAME-WORKSHOP.md"
ARCHIVE="$TASK_ROOT/build/pocketjs-port/AI-Game-Workshop-Brick.zip"
# Create a fresh archive and atomically replace the generated output.
ARCHIVE_TEMP=$(mktemp "$TASK_ROOT/build/pocketjs-port/.workshop-archive-XXXXXX")
(cd "$STAGING" && zip -qr - Tools Emus AI-GAME-WORKSHOP.md) > "$ARCHIVE_TEMP"
mv "$ARCHIVE_TEMP" "$ARCHIVE"
echo "Ready: $PKG"
echo "Installer: $ARCHIVE"
