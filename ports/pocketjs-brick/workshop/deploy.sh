#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../../.." && pwd -P)
OUTPUT="$TASK_ROOT/build/pocketjs-port"
HOST="${1:-nextui-brick}"
[[ "$HOST" =~ ^[a-zA-Z0-9_.@-]+$ ]] || { echo 'Use an SSH host alias or user@hostname.' >&2; exit 2; }
test -x "$OUTPUT/AI Game Workshop.pak/pocketjs-app.elf"
test -f "$OUTPUT/P8.pak/fake08_libretro.so"
cmp "$TASK_ROOT/ports/pocketjs-brick/workshop/launch.sh" "$OUTPUT/AI Game Workshop.pak/launch.sh" || {
    echo 'Workshop launcher is missing or stale; rebuild the workshop package before deploying.' >&2
    exit 1
}
STAMP=$(date +%Y%m%d-%H%M%S)
REMOTE_ARCHIVE="/mnt/SDCARD/.userdata/shared/ai-game-workshop-install-$STAMP.tar.gz"
ARCHIVE=$(mktemp "$OUTPUT/.workshop-deploy-XXXXXX")
tar -czf "$ARCHIVE" -C "$OUTPUT" 'AI Game Workshop.pak' P8.pak
ssh -o BatchMode=yes -o ConnectTimeout=8 "$HOST" 'test "$(uname -m)" = aarch64 && test -d /mnt/SDCARD/.system/tg5040 && test -d /mnt/SDCARD/.userdata/shared'
# TrimUI's Dropbear image has SSH but no remote scp/SFTP executable.
ssh -T -o BatchMode=yes -o ConnectTimeout=8 "$HOST" "cat > '$REMOTE_ARCHIVE'" < "$ARCHIVE"
ssh -o BatchMode=yes -o ConnectTimeout=8 "$HOST" sh -s -- "$STAMP" "$REMOTE_ARCHIVE" <<'REMOTE'
set -eu
stamp=$1
archive=$2
sd=/mnt/SDCARD
shared="$sd/.userdata/shared"
stage="$shared/ai-game-workshop-install-$stamp"
mkdir -p "$stage" "$shared/ai-game-workshop-backups" "$sd/Tools/tg5040" "$sd/Emus/tg5040"
tar -xzf "$archive" -C "$stage"
cp "$stage/P8.pak/launch.sh" "$stage/p8-launch.sh"
pak="$sd/Tools/tg5040/AI Game Workshop.pak"
if [ -e "$pak" ]; then mv "$pak" "$shared/ai-game-workshop-backups/$stamp.pak"; fi
mv "$stage/AI Game Workshop.pak" "$pak"
chmod +x "$pak/launch.sh" "$pak/pocketjs-app.elf"
p8="$sd/Emus/tg5040/P8.pak"
if [ -x "$p8/launch.sh" ] && [ -r "$p8/fake08_libretro.so" ]; then
    echo 'Using existing SD P8 core.'
elif [ ! -e "$p8/launch.sh" ] && [ -x "$sd/.system/tg5040/paks/Emus/P8.pak/launch.sh" ] && [ -r "$sd/.system/tg5040/paks/Emus/P8.pak/fake08_libretro.so" ]; then
    p8="$sd/.system/tg5040/paks/Emus/P8.pak"
    echo 'Using existing system P8 runtime.'
else
    if [ -e "$p8" ] && [ ! -d "$p8" ]; then mv "$p8" "$shared/ai-game-workshop-backups/$stamp-P8.pak"; fi
    mkdir -p "$p8"
    # Repair an incomplete package without discarding its custom bindings.
    for file in "$stage/P8.pak/"*; do
        name=${file##*/}
        if [ ! -e "$p8/$name" ]; then cp "$file" "$p8/$name"; fi
    done
    echo 'Installed missing P8 runtime files; preserved existing options.'
fi
# Keep an installed core/config, but refresh the shared router on every deploy.
# Otherwise menu previews can retain a launcher from an unrelated installation.
cp "$p8/launch.sh" "$shared/ai-game-workshop-backups/$stamp-P8-launch.sh"
cp "$stage/p8-launch.sh" "$p8/launch.sh.new"
chmod +x "$p8/launch.sh.new"
mv -f "$p8/launch.sh.new" "$p8/launch.sh"
export SHARED_USERDATA_PATH="$shared" POCKETJS_WORKSHOP=1 POCKETJS_HARDWARE=0 POCKETJS_VOICE=1
export POCKETJS_DATA="$shared/ai-game-workshop" POCKETJS_ROMS="$sd/Roms/AI生成游戏 (P8)"
export POCKETJS_FONT="$sd/.system/res/font1.ttf" POCKETJS_P8_LAUNCH="$p8/launch.sh"
export POCKETJS_P8_CORE="$p8/fake08_libretro.so"
export POCKETJS_CA="$pak/ca-bundle.crt"
export PATH="$sd/.system/tg5040/bin:/usr/trimui/bin:$PATH"
export LD_LIBRARY_PATH="$sd/.system/tg5040/lib:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$POCKETJS_DATA/projects" "$sd/Roms" "$POCKETJS_ROMS"
cd "$pak"
(
    unset POCKETJS_WORKSHOP POCKETJS_DATA POCKETJS_ROMS POCKETJS_FONT POCKETJS_CA POCKETJS_P8_LAUNCH POCKETJS_P8_CORE
    ./launch.sh --check
)
sha256sum pocketjs-app.elf brick-app.js brick-app.pak
printf 'Installed: %s\nGames: %s\n' "$pak" "$POCKETJS_ROMS"
REMOTE
if [ -x "$TASK_ROOT/build/native-consoles/PICO-8.pak/menu-exit.elf" ]; then
    "$TASK_ROOT/ports/pocketjs-brick/workshop/deploy-launchers.sh" "$HOST"
fi
