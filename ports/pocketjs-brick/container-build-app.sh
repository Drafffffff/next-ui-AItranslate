#!/bin/bash
set -euo pipefail
cd /work
source ports/pocketjs-brick/pins.env
OUTPUT=/work/build/pocketjs-port
UPSTREAM="$OUTPUT/upstream"
QJS="$OUTPUT/quickjs/libquickjs-sys/embed/quickjs"
PKG="$OUTPUT/${POCKETJS_PACKAGE_NAME:-PocketJS App.pak}"
BUNDLE="$OUTPUT/${POCKETJS_BUNDLE_DIR:-app-bundle}"
mkdir -p "$PKG" "$OUTPUT/quickjs-objects"
export RUSTUP_HOME="$OUTPUT/rustup" CARGO_HOME="$OUTPUT/cargo"
export PATH="$CARGO_HOME/bin:$PATH"
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-nextui-linux-gnu-gcc
export RUSTFLAGS='-C target-cpu=cortex-a53'
cargo +"$RUST_CHANNEL" build --release --locked --no-default-features \
    --manifest-path "$UPSTREAM/engine/ui-cabi/Cargo.toml" --features bare-platform,software-only \
    --target aarch64-unknown-linux-gnu --target-dir "$OUTPUT/rust-app-target"
for source in quickjs cutils libregexp libunicode dtoa; do
    aarch64-nextui-linux-gnu-gcc -std=gnu11 -O2 -mcpu=cortex-a53 -D_GNU_SOURCE \
        -DCONFIG_VERSION=\""$QUICKJS_VERSION"\" -I"$QJS" \
        -c "$QJS/$source.c" -o "$OUTPUT/quickjs-objects/$source.o"
done
# A local experimental host contract; no registered upstream Brick build plan exists.
# The bundle uses the upstream plan-less custom-host pipeline at density 1 / 60 Hz.
aarch64-nextui-linux-gnu-gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -mcpu=cortex-a53 \
    -I"$UPSTREAM/engine/ui-cabi/include" -I"$UPSTREAM/engine/quickjs-c" \
    -I"$UPSTREAM/hosts/nokia-e7/runtime" -I"$UPSTREAM/contracts/generated" -I"$QJS" $(pkg-config --cflags sdl2) \
    -DPOCKET_RUNTIME_EXTENSION -DPOCKET_RUNTIME_HARNESS -DPOCKETJS_TARGET_ID=\"brick-experimental\" -DPOCKETJS_HOST_ABI=1 \
    -DPOCKETJS_REV=\""$POCKETJS_REV"\" \
    ports/pocketjs-brick/app-host.c ports/pocketjs-brick/brick-services.c ports/pocketjs-brick/brick-hardware.c ports/pocketjs-brick/compat.c \
    "$UPSTREAM/engine/quickjs-c/pocket_runtime.c" "$UPSTREAM/engine/quickjs-c/rust_eh_personality.c" \
    "$OUTPUT/quickjs-objects/"*.o \
    "$OUTPUT/rust-app-target/aarch64-unknown-linux-gnu/release/libpocketjs_symbian_core.a" \
    $(pkg-config --libs sdl2) -lSDL2_ttf -lm -ldl -lpthread -o "$PKG/pocketjs-app.elf"
cp "$OUTPUT/ca-bundle.crt" "$PKG/ca-bundle.crt"
cp "$BUNDLE/brick-app.js" "$BUNDLE/brick-app.pak" "$PKG/"
cp ports/pocketjs-brick/launch-app.sh "$PKG/launch.sh"
cp ports/pocketjs-brick/APP.md "$PKG/README.md"
cp LICENSE "$PKG/NEXTUI-PORT-LICENSE.txt"
cp fonts/NotoSansSC-Regular.otf fonts/NotoSansSC-OFL.txt "$PKG/"
cp "$UPSTREAM/LICENSE" "$PKG/POCKETJS-LICENSE.txt"
cp "$QJS/LICENSE" "$PKG/QUICKJS-LICENSE.txt"
cp "$UPSTREAM/node_modules/solid-js/LICENSE" "$PKG/SOLID-LICENSE.txt"
chmod +x "$PKG/launch.sh" "$PKG/pocketjs-app.elf"
{
    printf 'PocketJS=%s\nQuickJS=%s\nBun=%s\nToolchain=%s\nHost=brick-experimental ABI=1 density=1 hz=60\n' "$POCKETJS_REV" "$QUICKJS_REV" "$BUN_VERSION" "$TOOLCHAIN_IMAGE"
    sha256sum "$PKG/pocketjs-app.elf" "$PKG/brick-app.js" "$PKG/brick-app.pak" "$PKG/ca-bundle.crt" "$PKG/NotoSansSC-Regular.otf"
    aarch64-nextui-linux-gnu-readelf -d "$PKG/pocketjs-app.elf" | sed -n '/NEEDED/p'
    aarch64-nextui-linux-gnu-readelf --version-info "$PKG/pocketjs-app.elf"
} > "$OUTPUT/app-build-receipt.txt"
echo "构建完成：$PKG"
