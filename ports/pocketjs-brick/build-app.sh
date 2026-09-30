#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
OUTPUT="$TASK_ROOT/build/pocketjs-port"
# Reuse the verified native core and its pinned upstream acquisition.
"$TASK_ROOT/ports/pocketjs-brick/build.sh"
QUICKJS="$OUTPUT/quickjs"
if [ ! -d "$QUICKJS/.git" ]; then
    git clone --no-checkout --depth 1 "$QUICKJS_URL" "$QUICKJS"
    git -C "$QUICKJS" fetch --depth 1 origin "$QUICKJS_REV"
    git -C "$QUICKJS" checkout --detach "$QUICKJS_REV"
fi
if [ "$(git -C "$QUICKJS" rev-parse HEAD)" != "$QUICKJS_REV" ] || [ -n "$(git -C "$QUICKJS" status --porcelain)" ]; then
    echo 'QuickJS checkout does not match the clean pinned revision.' >&2; exit 1
fi
test "$(cat "$QUICKJS/libquickjs-sys/embed/quickjs/VERSION")" = "$QUICKJS_VERSION"
if [ -n "${POCKETJS_BUN:-}" ]; then
    BUN="$POCKETJS_BUN"
else
    case "$(uname -s)-$(uname -m)" in
        Darwin-arm64) BUN_ASSET=bun-darwin-aarch64 ;;
        Linux-aarch64) BUN_ASSET=bun-linux-aarch64 ;;
        Linux-x86_64) BUN_ASSET=bun-linux-x64 ;;
        Darwin-x86_64) BUN_ASSET=bun-darwin-x64 ;;
        *) echo 'Set POCKETJS_BUN to Bun 1.4.2 for this host.' >&2; exit 1 ;;
    esac
    BUN="$OUTPUT/bun/$BUN_ASSET/bun"
    if [ ! -x "$BUN" ]; then
        mkdir -p "$OUTPUT/bun"
        if command -v gh >/dev/null; then
            gh release download "bun-v$BUN_VERSION" --repo oven-sh/bun --pattern "$BUN_ASSET.zip" --dir "$OUTPUT/bun" --skip-existing
        else
            curl --fail --location --retry 3 "https://github.com/oven-sh/bun/releases/download/bun-v$BUN_VERSION/$BUN_ASSET.zip" -o "$OUTPUT/bun/$BUN_ASSET.zip"
        fi
        unzip -qo "$OUTPUT/bun/$BUN_ASSET.zip" -d "$OUTPUT/bun"
    fi
fi
test "$("$BUN" --version)" = "$BUN_VERSION"
"$BUN" install --frozen-lockfile --cwd "$OUTPUT/upstream"
"$BUN" "$OUTPUT/upstream/tools/build.ts" "$TASK_ROOT/ports/pocketjs-brick/app/brick-app.tsx" \
    --framework=solid --no-config --density=1 --hz=60 \
    --font-regular="$TASK_ROOT/fonts/font1.ttf" --font-bold="$TASK_ROOT/fonts/font1.ttf" \
    --project-root="$TASK_ROOT" --outdir="$OUTPUT/app-bundle"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" \
    /bin/bash /work/ports/pocketjs-brick/container-build-app.sh
