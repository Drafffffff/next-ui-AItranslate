#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
FAKE08_REV=814991a2571ad3970e386cef48f3b148aa1c27b9
SOURCE="$TASK_ROOT/build/workshop-fake08"
if [ ! -d "$SOURCE/.git" ]; then
    git clone --no-checkout --depth 1 https://github.com/jtothebell/fake-08.git "$SOURCE"
    git -C "$SOURCE" fetch --depth 1 origin "$FAKE08_REV"
    git -C "$SOURCE" checkout --detach "$FAKE08_REV"
fi
test "$(git -C "$SOURCE" rev-parse HEAD)" = "$FAKE08_REV"
PATCH="$TASK_ROOT/workspace/tg5040/cores/patches/fake-08.patch"
if git -C "$SOURCE" apply --check "$PATCH" 2>/dev/null; then
    git -C "$SOURCE" apply "$PATCH"
else
    git -C "$SOURCE" apply --reverse --check "$PATCH"
fi
git -C "$SOURCE" submodule update --init --recursive
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" \
    /bin/bash -lc 'cd /work/build/workshop-fake08/platform/libretro && make platform=tg5040 CROSS_COMPILE=aarch64-nextui-linux-gnu- -j4'
PKG="$TASK_ROOT/build/pocketjs-port/P8.pak"
mkdir -p "$PKG"
cp "$TASK_ROOT/skeleton/EXTRAS/Emus/tg5040/P8.pak/launch.sh" "$TASK_ROOT/skeleton/EXTRAS/Emus/tg5040/P8.pak/default.cfg" "$PKG/"
cp "$SOURCE/platform/libretro/fake08_libretro.so" "$PKG/"
cp "$SOURCE/LICENSE.MD" "$PKG/FAKE08-LICENSE.md"
cp "$SOURCE/libs/miniz/LICENSE" "$PKG/MINIZ-LICENSE.txt"
cp "$SOURCE/libs/lodepng/LICENSE" "$PKG/LODEPNG-LICENSE.txt"
cp "$TASK_ROOT/LICENSE" "$PKG/NEXTUI-PORT-LICENSE.txt"
chmod +x "$PKG/launch.sh"
# The syntax worker shares the emulator's pinned parser dialect.
mkdir -p "$TASK_ROOT/build/pocketjs-port/AI Game Workshop.pak"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" \
    /bin/bash -lc 'POCKETJS_CHECK_CXX=aarch64-nextui-linux-gnu-g++ /bin/bash /work/ports/pocketjs-brick/workshop/build-check.sh "/work/build/pocketjs-port/AI Game Workshop.pak/workshop-check"'
cp "$SOURCE/libs/z8lua/lua.h" "$TASK_ROOT/build/pocketjs-port/AI Game Workshop.pak/Z8LUA-LICENSE.txt"
head -n 22 "$SOURCE/libs/z8lua/eris.c" > "$TASK_ROOT/build/pocketjs-port/AI Game Workshop.pak/ERIS-LICENSE.txt"
{
    printf 'fake08=%s\nz8lua=%s\nToolchain=%s\n' "$FAKE08_REV" "$(git -C "$SOURCE/libs/z8lua" rev-parse HEAD)" "$TOOLCHAIN_IMAGE"
    shasum -a 256 "$PKG/fake08_libretro.so" "$PATCH"
} > "$TASK_ROOT/build/pocketjs-port/p8-build-receipt.txt"
