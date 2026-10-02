#!/bin/bash
set -euo pipefail
cd /work
source ports/pocketjs-brick/pins.env
CORE=/work/build/pocketjs-port
UPSTREAM="$CORE/upstream"
QJS="$CORE/quickjs/libquickjs-sys/embed/quickjs"
PKG="/work/build/brick-mic/Brick Mic.pak"
STATIC="$CORE/rust-app-target/aarch64-unknown-linux-gnu/release/libpocketjs_symbian_core.a"
if [ ! -f "$STATIC" ]; then /bin/bash ports/pocketjs-brick/container-build-app.sh; fi
aarch64-nextui-linux-gnu-gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -mcpu=cortex-a53 \
    -I"$UPSTREAM/engine/ui-cabi/include" -I"$UPSTREAM/engine/quickjs-c" \
    -I"$UPSTREAM/hosts/nokia-e7/runtime" -I"$UPSTREAM/contracts/generated" -I"$QJS" \
    -I/work/ports/pocketjs-brick $(pkg-config --cflags sdl2) \
    -DPOCKET_RUNTIME_EXTENSION -DPOCKETJS_CUSTOM_EXTENSION -DDYNAMIC_PX=40 \
    -DPOCKETJS_TARGET_ID=\"brick-experimental\" -DPOCKETJS_HOST_ABI=1 -DPOCKETJS_REV=\""$POCKETJS_REV"\" \
    ports/brick-mic/brick/pocket-host.c ports/brick-mic/brick/mic-native.c ports/brick-mic/brick/mic-power.c \
    ports/pocketjs-brick/brick-services.c ports/pocketjs-brick/brick-hardware.c ports/pocketjs-brick/compat.c \
    "$UPSTREAM/engine/quickjs-c/pocket_runtime.c" "$UPSTREAM/engine/quickjs-c/rust_eh_personality.c" \
    "$CORE/quickjs-objects/"*.o "$STATIC" $(pkg-config --libs sdl2) -lSDL2_ttf -lm -ldl -lpthread \
    -o "$PKG/pocketjs-mic.elf"
echo "Brick PocketJS UI built: $PKG"
