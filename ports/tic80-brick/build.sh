#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
TIC80_REV=4dba5bc2640d9cde650fb0b427c9be6aab598de9
OUTPUT="$TASK_ROOT/build/creative-lab"
SOURCE="$OUTPUT/tic80"
mkdir -p "$OUTPUT/sdl-cmake"
if [ ! -d "$SOURCE/.git" ]; then
    git clone --no-checkout https://github.com/nesbox/TIC-80.git "$SOURCE"
    git -C "$SOURCE" checkout "$TIC80_REV"
fi
[ "$(git -C "$SOURCE" rev-parse HEAD)" = "$TIC80_REV" ]
git -C "$SOURCE" submodule update --init --depth 1 vendor/blip-buf vendor/giflib vendor/lua vendor/zlib vendor/zip vendor/argparse vendor/libpng vendor/jsmn vendor/naett
PATCH="$TASK_ROOT/ports/tic80-brick/brick-input.patch"
if git -C "$SOURCE" apply --check "$PATCH" 2>/dev/null; then
    git -C "$SOURCE" apply "$PATCH"
else
    git -C "$SOURCE" apply --reverse --check "$PATCH"
fi
for TASK_PATCH in public-site; do
    if git -C "$SOURCE" apply --check "$TASK_ROOT/ports/tic80-brick/$TASK_PATCH.patch" 2>/dev/null; then
        git -C "$SOURCE" apply "$TASK_ROOT/ports/tic80-brick/$TASK_PATCH.patch"
    else
        git -C "$SOURCE" apply --reverse --check "$TASK_ROOT/ports/tic80-brick/$TASK_PATCH.patch"
    fi
done
if git -C "$SOURCE/vendor/naett" apply --check "$TASK_ROOT/ports/tic80-brick/naett-ca.patch" 2>/dev/null; then
    git -C "$SOURCE/vendor/naett" apply "$TASK_ROOT/ports/tic80-brick/naett-ca.patch"
else
    git -C "$SOURCE/vendor/naett" apply --reverse --check "$TASK_ROOT/ports/tic80-brick/naett-ca.patch"
fi
bash "$TASK_ROOT/ports/tic80-brick/prepare-network.sh"
cp "$TASK_ROOT/ports/tic80-brick/SDL2Config.cmake" "$OUTPUT/sdl-cmake/"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" /bin/bash -c '
    set -eu
    cmake -S /work/build/creative-lab/tic80 -B /work/build/creative-lab/tic80-brick \
        -DCMAKE_TOOLCHAIN_FILE=/work/ports/tic80-brick/toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_STANDARD_LIBRARIES="-lpthread -ldl -lm" \
        -DBUILD_STATIC=ON -DBUILD_PRO=ON -DPREFER_SYSTEM_SDL2=ON \
        -DSDL2_DIR=/work/build/creative-lab/sdl-cmake \
        -DBUILD_SDLGPU=OFF -DBUILD_SURF=ON -DBUILD_WITH_FENNEL=OFF \
        -DBUILD_WITH_MOON=OFF -DBUILD_WITH_YUE=OFF -DBUILD_WITH_JS=OFF \
        -DCMAKE_DISABLE_FIND_PACKAGE_CURL=FALSE \
        -DCURL_INCLUDE_DIR=/work/build/creative-lab/network/curl-7.54.1/include \
        -DCURL_LIBRARY_RELEASE=/work/build/creative-lab/network/lib/libcurl.so.4.4.0 \
        -DCMAKE_C_FLAGS=-DTIC80_BRICK_BUILD \
        -DCMAKE_SKIP_RPATH=ON \
        -DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath-link,/work/build/creative-lab/network/lib
    cmake --build /work/build/creative-lab/tic80-brick -j4
'
PKG="$OUTPUT/TIC-80 Lab.pak"
mkdir -p "$PKG"
cp "$OUTPUT/tic80-brick/bin/tic80" "$PKG/tic80.elf"
cp "$TASK_ROOT/ports/tic80-brick/launch.sh" "$TASK_ROOT/ports/tic80-brick/sandbox.lua" "$TASK_ROOT/ports/tic80-brick/README.md" "$PKG/"
cp "$SOURCE/LICENSE" "$PKG/TIC80-LICENSE.txt"
cp "$TASK_ROOT/LICENSE" "$PKG/NEXTUI-PORT-LICENSE.txt"
mkdir -p "$PKG/licenses"
python3 - "$SOURCE" "$PKG/licenses" <<'PY'
from pathlib import Path
import shutil,sys
src,out=map(Path,sys.argv[1:])
for vendor in ('blip-buf','giflib','lua','zlib','zip','argparse','libpng','jsmn','naett'):
    for p in (src/'vendor'/vendor).iterdir():
        if p.is_file() and (p.name.lower().startswith(('license','copying')) or p.name in ('README','README.md','lua.h','blip_buf.h','argparse.h','jsmn.h')):
            shutil.copyfile(p,out/(vendor+'-'+p.name))
# wave_writer is compiled into the studio; its source carries the MIT notice.
shutil.copyfile(src/'vendor/blip-buf/wave_writer.c',out/'wave-writer-notice.c')
PY
chmod +x "$PKG/launch.sh" "$PKG/tic80.elf"
printf 'TIC80=%s\nToolchain=%s\n' "$TIC80_REV" "$TOOLCHAIN_IMAGE" > "$PKG/BUILD.txt"
shasum -a 256 "$PKG/tic80.elf" "$PKG/sandbox.lua" >> "$PKG/BUILD.txt"
echo "Built: $PKG"
