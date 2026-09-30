#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
OUTPUT="$TASK_ROOT/build/pocketjs-port"
UPSTREAM="$OUTPUT/upstream"
QJS="$OUTPUT/quickjs/libquickjs-sys/embed/quickjs"
MAC="$OUTPUT/mac-preview"
[ "$(uname -s)" = Darwin ] || { echo 'This preview requires macOS.' >&2; exit 1; }
pkg-config --exists sdl2 SDL2_ttf || { echo 'Run: brew install sdl2_ttf pkgconf' >&2; exit 1; }
if [ ! -f "$QJS/quickjs.c" ] || [ ! -f "$UPSTREAM/tools/build.ts" ]; then
    echo 'Run build-app.sh once to acquire the pinned sources and Bun.' >&2; exit 1
fi
[ "$(git -C "$UPSTREAM" rev-parse HEAD)" = "$POCKETJS_REV" ]
[ "$(git -C "$OUTPUT/quickjs" rev-parse HEAD)" = "$QUICKJS_REV" ]
export RUSTUP_HOME="$OUTPUT/mac-toolchain/rustup" CARGO_HOME="$OUTPUT/mac-toolchain/cargo"
export PATH="$CARGO_HOME/bin:$PATH"
if [ ! -x "$CARGO_HOME/bin/cargo" ]; then
    mkdir -p "$OUTPUT/mac-toolchain"
    case "$(uname -m)" in arm64) RUST_HOST=aarch64-apple-darwin ;; x86_64) RUST_HOST=x86_64-apple-darwin ;; *) exit 1 ;; esac
    curl --fail --location "https://static.rust-lang.org/rustup/dist/$RUST_HOST/rustup-init" -o "$OUTPUT/mac-toolchain/rustup-init"
    chmod +x "$OUTPUT/mac-toolchain/rustup-init"
    "$OUTPUT/mac-toolchain/rustup-init" -y --no-modify-path --profile minimal --default-toolchain "$RUST_CHANNEL"
fi
TARGET=$(rustc +"$RUST_CHANNEL" -vV | sed -n 's/^host: //p')
mkdir -p "$MAC/objects"
cargo +"$RUST_CHANNEL" build --release --locked --no-default-features \
    --manifest-path "$UPSTREAM/engine/ui-cabi/Cargo.toml" --features bare-platform,software-only \
    --target "$TARGET" --target-dir "$OUTPUT/mac-target"
BUN="${POCKETJS_BUN:-$OUTPUT/bun/bun-darwin-$( [ "$(uname -m)" = arm64 ] && echo aarch64 || echo x64 )/bun}"
test "$("$BUN" --version)" = "$BUN_VERSION"
"$BUN" "$UPSTREAM/tools/build.ts" "$TASK_ROOT/ports/pocketjs-brick/app/brick-app.tsx" \
    --framework=solid --no-config --density=1 --hz=60 \
    --font-regular="$TASK_ROOT/fonts/font1.ttf" --font-bold="$TASK_ROOT/fonts/font1.ttf" \
    --project-root="$TASK_ROOT" --outdir="$MAC"
for source in quickjs cutils libregexp libunicode dtoa; do
    clang -std=gnu11 -O2 -D_GNU_SOURCE -DCONFIG_VERSION=\""$QUICKJS_VERSION"\" -I"$QJS" \
        -c "$QJS/$source.c" -o "$MAC/objects/$source.o"
done
clang -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
    -I"$UPSTREAM/engine/ui-cabi/include" -I"$UPSTREAM/engine/quickjs-c" \
    -I"$UPSTREAM/hosts/nokia-e7/runtime" -I"$UPSTREAM/contracts/generated" -I"$QJS" $(pkg-config --cflags sdl2 SDL2_ttf) \
    -DPOCKET_RUNTIME_EXTENSION -DPOCKET_RUNTIME_HARNESS -DPOCKETJS_TARGET_ID=\"brick-experimental\" -DPOCKETJS_HOST_ABI=1 \
    -DPOCKETJS_REV=\""$POCKETJS_REV"\" \
    "$TASK_ROOT/ports/pocketjs-brick/app-host.c" "$TASK_ROOT/ports/pocketjs-brick/brick-services.c" \
    "$UPSTREAM/engine/quickjs-c/pocket_runtime.c" "$UPSTREAM/engine/quickjs-c/rust_eh_personality.c" "$MAC/objects/"*.o \
    "$OUTPUT/mac-target/$TARGET/release/libpocketjs_symbian_core.a" \
    $(pkg-config --libs sdl2 SDL2_ttf) -lm -lpthread -o "$MAC/pocketjs-app"
cp "$TASK_ROOT/fonts/NotoSansSC-Regular.otf" "$MAC/"
mkdir -p "$MAC/PocketJS Preview.app/Contents/MacOS"
cp "$MAC/pocketjs-app" "$MAC/PocketJS Preview.app/Contents/MacOS/pocketjs-app"
cat > "$MAC/PocketJS Preview.app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>pocketjs-app</string>
<key>CFBundleIdentifier</key><string>local.nextui.pocketjs-preview</string>
<key>CFBundleName</key><string>PocketJS Preview</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
echo "Mac preview built: $MAC/pocketjs-app"
