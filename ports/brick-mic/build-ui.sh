#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
MODE="${1:-brick}"
OUTPUT="$TASK_ROOT/build/brick-mic"
CORE="$TASK_ROOT/build/pocketjs-port"
UPSTREAM="$CORE/upstream"
QJS="$CORE/quickjs/libquickjs-sys/embed/quickjs"
BUN="${POCKETJS_BUN:-$CORE/bun/bun-darwin-aarch64/bun}"
if [ ! -f "$QJS/quickjs.c" ]; then "$TASK_ROOT/ports/pocketjs-brick/build-app.sh"; fi
[ "$(git -C "$UPSTREAM" rev-parse HEAD)" = "$POCKETJS_REV" ]
[ "$(git -C "$CORE/quickjs" rev-parse HEAD)" = "$QUICKJS_REV" ]
[ "$("$BUN" --version)" = "$BUN_VERSION" ]
if [ "$MODE" = mac ]; then
    PKG="$OUTPUT/pocketjs-preview"
else
    PKG="$OUTPUT/Brick Mic.pak"
fi
mkdir -p "$PKG"
"$BUN" "$UPSTREAM/tools/build.ts" "$TASK_ROOT/ports/brick-mic/ui/app.tsx" \
    --framework=solid --no-config --density=1 --hz=60 \
    --font-regular="$TASK_ROOT/fonts/font1.ttf" --font-bold="$TASK_ROOT/fonts/font1.ttf" \
    --project-root="$TASK_ROOT" --outdir="$OUTPUT/ui-bundle"
cp "$OUTPUT/ui-bundle/app.js" "$PKG/brick-app.js"
cp "$OUTPUT/ui-bundle/app.pak" "$PKG/brick-app.pak"
cp "$TASK_ROOT/fonts/NotoSansSC-Regular.otf" "$TASK_ROOT/fonts/NotoSansSC-OFL.txt" "$PKG/"
cp "$UPSTREAM/LICENSE" "$PKG/POCKETJS-LICENSE.txt"
cp "$QJS/LICENSE" "$PKG/QUICKJS-LICENSE.txt"
cp "$UPSTREAM/node_modules/solid-js/LICENSE" "$PKG/SOLID-LICENSE.txt"
if [ "$MODE" = mac ]; then
    STATIC="$CORE/mac-target/aarch64-apple-darwin/release/libpocketjs_symbian_core.a"
    if [ ! -f "$STATIC" ]; then "$TASK_ROOT/ports/pocketjs-brick/build-mac.sh"; fi
    clang -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
        -I"$UPSTREAM/engine/ui-cabi/include" -I"$UPSTREAM/engine/quickjs-c" \
        -I"$UPSTREAM/hosts/nokia-e7/runtime" -I"$UPSTREAM/contracts/generated" -I"$QJS" \
        -I"$TASK_ROOT/ports/pocketjs-brick" $(pkg-config --cflags sdl2 SDL2_ttf) \
        -DPOCKET_RUNTIME_EXTENSION -DPOCKETJS_CUSTOM_EXTENSION -DDYNAMIC_PX=40 \
        -DPOCKETJS_TARGET_ID=\"brick-experimental\" -DPOCKETJS_HOST_ABI=1 -DPOCKETJS_REV=\""$POCKETJS_REV"\" \
        "$TASK_ROOT/ports/brick-mic/brick/pocket-host.c" "$TASK_ROOT/ports/brick-mic/brick/mic-native.c" "$TASK_ROOT/ports/brick-mic/brick/mic-power.c" \
        "$TASK_ROOT/ports/pocketjs-brick/brick-services.c" "$TASK_ROOT/ports/pocketjs-brick/brick-hardware.c" \
        "$UPSTREAM/engine/quickjs-c/pocket_runtime.c" "$UPSTREAM/engine/quickjs-c/rust_eh_personality.c" \
        "$CORE/mac-preview/objects/"*.o "$STATIC" $(pkg-config --libs sdl2 SDL2_ttf) -lm -lpthread \
        -o "$PKG/pocketjs-mic"
    APP="$PKG/Brick Mic UI Preview.app"
    mkdir -p "$APP/Contents/MacOS"
    cp "$PKG/pocketjs-mic" "$APP/Contents/MacOS/pocketjs-mic"
    mkdir -p "$APP/Contents/Resources"
    cp "$PKG/brick-app.js" "$PKG/brick-app.pak" "$PKG/NotoSansSC-Regular.otf" "$APP/Contents/Resources/"
    cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>pocketjs-mic</string>
<key>CFBundleIdentifier</key><string>com.nextui.brickmic.preview</string>
<key>CFBundleName</key><string>Brick Mic UI Preview</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
    codesign --force --sign - "$APP"
    echo "Mac PocketJS preview built: $PKG"
else
    docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" \
        /bin/bash /work/ports/brick-mic/container-build-ui.sh
fi
