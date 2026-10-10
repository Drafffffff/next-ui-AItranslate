#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../../../.." && pwd -P)
SOURCE="$TASK_ROOT/build/workshop-fake08"
OUTPUT="$TASK_ROOT/build/workshop-eval"
mkdir -p "$OUTPUT"
node "$TASK_ROOT/ports/pocketjs-brick/workshop/build-skill.mjs"
SNAPSHOT="$OUTPUT/source-before-mac.json"
if [ "${1:-}" = --arm ];then SNAPSHOT="$OUTPUT/source-before-arm.json";fi
python3 "$TASK_ROOT/ports/pocketjs-brick/workshop/eval/run.py" --snapshot-source "$SNAPSHOT"
export POCKETJS_APP_SOURCE="$TASK_ROOT/ports/pocketjs-brick/workshop/eval/entry.tsx"
if [ "${1:-}" = --arm ]; then
 export POCKETJS_BUNDLE_DIR=eval-bundle POCKETJS_PACKAGE_NAME='Workshop Eval.pak'
 "$TASK_ROOT/ports/pocketjs-brick/build-app.sh"
 POCKETJS_CHECK_CXX=aarch64-nextui-linux-gnu-g++
 source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
 docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" /bin/bash -lc 'POCKETJS_CHECK_CXX=aarch64-nextui-linux-gnu-g++ /bin/bash /work/ports/pocketjs-brick/workshop/build-check.sh "/work/build/pocketjs-port/Workshop Eval.pak/workshop-check"'
 python3 "$TASK_ROOT/ports/pocketjs-brick/workshop/eval/run.py" --expect-source "$SNAPSHOT"
 exit 0
fi
export POCKETJS_MAC_DIR=mac-evaluation
"$TASK_ROOT/ports/pocketjs-brick/build-mac.sh"
/bin/bash "$TASK_ROOT/ports/pocketjs-brick/workshop/build-check.sh" "$TASK_ROOT/build/pocketjs-port/mac-evaluation/workshop-check"
test "$(git -C "$SOURCE" rev-parse HEAD)" = 814991a2571ad3970e386cef48f3b148aa1c27b9
test "$(git -C "$SOURCE/libs/z8lua" rev-parse HEAD)" = e6928578d46b61fd5ea30cfcf547e855a30a0553
FILES=();for file in "$SOURCE/source/"*.cpp; do
 case "$(basename "$file")" in main.cpp|logger.cpp|hostCommonFunctions.cpp) ;; *) FILES+=("$file") ;; esac
done
CORE=(lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lctype eris lauxlib lbaselib lcorolib ldblib ltablib lstrlib lpico8lib linit)
for name in "${CORE[@]}";do FILES+=("$SOURCE/libs/z8lua/$name.c");done
clang++ -std=c++17 -O2 -Wno-deprecated -Wno-deprecated-declarations -DVER_STR='"workshop-evaluation"' \
 -I"$SOURCE/source" -I"$SOURCE/test" -I"$SOURCE/libs/z8lua" -I"$SOURCE/libs/lodepng" -I"$SOURCE/libs/utf8-util" -I"$SOURCE/libs/miniz" -I"$SOURCE/libs/simpleini" \
 "$TASK_ROOT/ports/pocketjs-brick/workshop/eval/runtime.cpp" "$SOURCE/test/stubhost.cpp" "$SOURCE/test/nooplogger.cpp" "$SOURCE/libs/lodepng/lodepng.cpp" "${FILES[@]}" -o "$OUTPUT/fake08-replay"
python3 "$TASK_ROOT/ports/pocketjs-brick/workshop/eval/run.py" --record-build --expect-source "$SNAPSHOT"
echo "Ready: $OUTPUT and build/pocketjs-port/mac-evaluation"
