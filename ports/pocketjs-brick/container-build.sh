#!/bin/bash
set -euo pipefail
cd /work
source ports/pocketjs-brick/pins.env
export RUSTUP_HOME=/work/build/pocketjs-port/rustup
export CARGO_HOME=/work/build/pocketjs-port/cargo
export PATH="$CARGO_HOME/bin:$PATH"
if [ ! -x "$CARGO_HOME/bin/rustup" ]; then
    wget --https-only -q https://static.rust-lang.org/rustup/dist/aarch64-unknown-linux-gnu/rustup-init \
        -O /work/build/pocketjs-port/rustup-init
    chmod +x /work/build/pocketjs-port/rustup-init
    /work/build/pocketjs-port/rustup-init -y --no-modify-path --profile minimal --default-toolchain "$RUST_CHANNEL"
fi
rustup toolchain install "$RUST_CHANNEL" --profile minimal
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-nextui-linux-gnu-gcc
export RUSTFLAGS='-C target-cpu=cortex-a53'
UPSTREAM=/work/build/pocketjs-port/upstream
OUTPUT=/work/build/pocketjs-port
cargo +"$RUST_CHANNEL" build --release --locked \
    --manifest-path "$UPSTREAM/engine/ui-cabi/Cargo.toml" \
    --features bare-platform,software-only --target aarch64-unknown-linux-gnu \
    --target-dir "$OUTPUT/rust-target"

# Derive prop ids from the pinned core instead of maintaining another ABI table.
python3 - "$UPSTREAM/engine/core/src/spec.rs" "$OUTPUT/pocket_props.h" <<'PY'
import re,sys
from pathlib import Path
src=Path(sys.argv[1]).read_text().split('pub mod prop {',1)[1].split('\n}',1)[0]
props=re.findall(r'pub const (\w+): u8 = (\d+);',src)
assert props
Path(sys.argv[2]).write_text('/* Generated from pinned PocketJS spec.rs. */\n'+''.join(f'#define PROP_{name} {value}\n' for name,value in props))
PY
PKG="$OUTPUT/PocketJS Smoke.pak"
mkdir -p "$PKG"
aarch64-nextui-linux-gnu-gcc -std=gnu99 -O2 -Wall -Wextra -Werror -mcpu=cortex-a53 \
    -I"$UPSTREAM/engine/ui-cabi/include" -I"$OUTPUT" \
    $(pkg-config --cflags sdl2) -I"$PREFIX/include" \
    -DPOCKETJS_REV=\""$POCKETJS_REV"\" \
    ports/pocketjs-brick/smoke.c ports/pocketjs-brick/compat.c \
    "$OUTPUT/rust-target/aarch64-unknown-linux-gnu/release/libpocketjs_symbian_core.a" \
    "$UPSTREAM/engine/quickjs-c/rust_eh_personality.c" \
    -L"$PREFIX/lib" $(pkg-config --libs sdl2) -lSDL2_ttf -lm -ldl -lpthread \
    -o "$PKG/pocketjs-smoke.elf"
cp ports/pocketjs-brick/launch.sh "$PKG/launch.sh"
chmod +x "$PKG/launch.sh" "$PKG/pocketjs-smoke.elf"
cp ports/pocketjs-brick/README.md "$PKG/README.md"
cp "$UPSTREAM/LICENSE" "$PKG/POCKETJS-LICENSE.txt"
{
    printf 'PocketJS=%s\nToolchain=%s\nRust=%s\n' "$POCKETJS_REV" "$TOOLCHAIN_IMAGE" "$RUST_CHANNEL"
    sha256sum "$PKG/pocketjs-smoke.elf"
    aarch64-nextui-linux-gnu-readelf -d "$PKG/pocketjs-smoke.elf" | sed -n '/NEEDED/p'
    aarch64-nextui-linux-gnu-readelf --version-info "$PKG/pocketjs-smoke.elf"
} > "$OUTPUT/build-receipt.txt"
echo "构建完成：$PKG"
