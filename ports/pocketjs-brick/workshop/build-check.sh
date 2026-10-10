#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../../.." && pwd -P)
SOURCE="$TASK_ROOT/build/workshop-fake08/libs/z8lua"
test -f "$SOURCE/lparser.c"
COMPILER=${POCKETJS_CHECK_CXX:-clang++}
OUTPUT=${1:?Specify parser output file}
CORE=(lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lctype eris lauxlib lbaselib lcorolib ldblib ltablib lstrlib lpico8lib linit)
FILES=();for name in "${CORE[@]}";do FILES+=("$SOURCE/$name.c");done
"$COMPILER" -std=c++17 -O2 -Wno-deprecated -Wno-deprecated-declarations -I"$SOURCE" "$TASK_ROOT/ports/pocketjs-brick/workshop-check.cpp" "${FILES[@]}" -o "$OUTPUT"
