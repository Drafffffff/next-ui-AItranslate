#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
OUTPUT="$TASK_ROOT/build/pocketjs-port"
UPSTREAM="$OUTPUT/upstream"
mkdir -p "$OUTPUT"
if [ ! -d "$UPSTREAM/.git" ]; then
    git clone --no-checkout --depth 1 "$POCKETJS_URL" "$UPSTREAM"
    git -C "$UPSTREAM" fetch --depth 1 origin "$POCKETJS_REV"
    git -C "$UPSTREAM" checkout --detach "$POCKETJS_REV"
fi
if [ "$(git -C "$UPSTREAM" rev-parse HEAD 2>/dev/null || true)" != "$POCKETJS_REV" ]; then
    if [ -n "$(git -C "$UPSTREAM" status --porcelain)" ]; then
        echo '上游工作目录有修改，请保留修改后再切换固定版本。' >&2
        exit 1
    fi
    git -C "$UPSTREAM" fetch --depth 1 origin "$POCKETJS_REV"
    git -C "$UPSTREAM" checkout --detach "$POCKETJS_REV"
fi
if [ -n "$(git -C "$UPSTREAM" status --porcelain)" ]; then
    echo '上游源码含未提交修改，拒绝将其标记为固定提交的构建。' >&2
    exit 1
fi
docker run --rm --platform linux/arm64 \
    -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" \
    /bin/bash /work/ports/pocketjs-brick/container-build.sh
