#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
SOURCE="$TASK_ROOT/build/workshop-fake08"
OUTPUT="$TASK_ROOT/ports/workshop-share/public/runtime"
test "$(git -C "$SOURCE" rev-parse HEAD)" = 814991a2571ad3970e386cef48f3b148aa1c27b9
test "$(git -C "$SOURCE/libs/z8lua" rev-parse HEAD)" = e6928578d46b61fd5ea30cfcf547e855a30a0553
if [ "${1:-}" != --inside ];then
 docker run --rm --platform linux/amd64 -v "$TASK_ROOT:/work" emscripten/emsdk:4.0.15 /bin/bash /work/ports/workshop-share/build-runtime.sh --inside
 exit 0
fi
FILES=();for file in "$SOURCE/source/"*.cpp;do case "$(basename "$file")" in main.cpp|logger.cpp|hostCommonFunctions.cpp);;*)FILES+=("$file");;esac;done
CORE=(lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lctype eris lauxlib lbaselib lcorolib ldblib ltablib lstrlib lpico8lib linit)
for name in "${CORE[@]}";do FILES+=("$SOURCE/libs/z8lua/$name.c");done
mkdir -p "$OUTPUT"
FILES=("$TASK_ROOT/ports/workshop-share/runtime.cpp" "$SOURCE/test/stubhost.cpp" "$SOURCE/test/nooplogger.cpp" "$SOURCE/libs/lodepng/lodepng.cpp" "${FILES[@]}")
CACHE_KEY=$(sha256sum "$TASK_ROOT/ports/workshop-share/build-runtime.sh" "${FILES[@]}" | sha256sum | cut -c1-24)
export OBJECTS="$TASK_ROOT/build/workshop-web-objects/$CACHE_KEY" SOURCE
mkdir -p "$OBJECTS"
compile_one(){
 local file="$1" object="$OBJECTS/$(basename "$1").o"
 if [ -f "$object" ];then return;fi
 em++ -std=c++17 -O2 -Wno-deprecated -Wno-deprecated-declarations -DLUA_USE_LONGJMP -DVER_STR='"workshop-web"' \
  -I"$SOURCE/source" -I"$SOURCE/test" -I"$SOURCE/libs/z8lua" -I"$SOURCE/libs/lodepng" -I"$SOURCE/libs/utf8-util" -I"$SOURCE/libs/miniz" -I"$SOURCE/libs/simpleini" \
  -c "$file" -o "$object.tmp"
 mv "$object.tmp" "$object"
}
export -f compile_one
printf '%s\0' "${FILES[@]}" | xargs -0 -P 4 -I{} /bin/bash -c 'compile_one "$1"' _ '{}'
OBJECT_FILES=();for file in "${FILES[@]}";do OBJECT_FILES+=("$OBJECTS/$(basename "$file").o");done
em++ -O2 "${OBJECT_FILES[@]}" \
 -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=worker,node -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sMAXIMUM_MEMORY=134217728 \
 -sEXPORTED_FUNCTIONS='["_malloc","_free","_web_load","_web_step","_web_pixels","_web_audio","_web_error"]' \
 -sEXPORTED_RUNTIME_METHODS='["UTF8ToString","HEAPU8","HEAP16"]' --no-entry -o "$OUTPUT/fake08.js"
cp "$SOURCE/LICENSE.MD" "$OUTPUT/FAKE08-LICENSE.md"
cp "$SOURCE/libs/z8lua/lua.h" "$OUTPUT/Z8LUA-LICENSE.txt"
cp "$SOURCE/libs/lodepng/LICENSE" "$OUTPUT/LODEPNG-LICENSE.txt"
cp "$SOURCE/libs/miniz/LICENSE" "$OUTPUT/MINIZ-LICENSE.txt"
head -n 22 "$SOURCE/libs/z8lua/eris.c" > "$OUTPUT/ERIS-LICENSE.txt"
