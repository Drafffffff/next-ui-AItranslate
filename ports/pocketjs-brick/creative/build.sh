#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
OUTPUT="$TASK_ROOT/build/pocketjs-port"
BUN="$OUTPUT/bun/bun-darwin-aarch64/bun"
"$BUN" "$OUTPUT/upstream/tools/build.ts" "$TASK_ROOT/ports/pocketjs-brick/creative/app.tsx" \
    --framework=solid --no-config --density=1 --hz=60 \
    --font-regular="$TASK_ROOT/fonts/font1.ttf" --font-bold="$TASK_ROOT/fonts/font1.ttf" \
    --project-root="$TASK_ROOT" --outdir="$OUTPUT/creative-bundle"
cp "$OUTPUT/creative-bundle/app.js" "$OUTPUT/creative-bundle/brick-app.js"
cp "$OUTPUT/creative-bundle/app.pak" "$OUTPUT/creative-bundle/brick-app.pak"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" \
    -e 'POCKETJS_PACKAGE_NAME=Brick Creative.pak' -e POCKETJS_BUNDLE_DIR=creative-bundle "$TOOLCHAIN_IMAGE" \
    /bin/bash /work/ports/pocketjs-brick/container-build-app.sh
cp "$TASK_ROOT/ports/pocketjs-brick/creative/launch.sh" "$OUTPUT/Brick Creative.pak/launch.sh"
cp "$TASK_ROOT/ports/pocketjs-brick/creative/README.md" "$OUTPUT/Brick Creative.pak/README.md"
