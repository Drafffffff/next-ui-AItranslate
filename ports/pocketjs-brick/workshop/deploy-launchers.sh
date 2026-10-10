#!/bin/bash
# Refresh game launchers without replacing a running workshop or any project.
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../../.." && pwd -P)
TASK_HOST=${1:-nextui-brick}
[[ "$TASK_HOST" =~ ^[a-zA-Z0-9_.@-]+$ ]] || { echo 'Use an SSH host alias or user@hostname.' >&2; exit 2; }
TASK_NATIVE="$TASK_ROOT/build/native-consoles/PICO-8.pak"
TASK_CORE="$TASK_ROOT/build/pocketjs-port/P8.pak"
test -x "$TASK_NATIVE/menu-exit.elf"
test -r "$TASK_NATIVE/sdl-nosensor.so"
grep -q '^# NEXTUI_PICO8_UNIFIED_RUNNER_V1$' "$TASK_NATIVE/launch.sh" || {
    echo 'Build the current launcher first: bash ports/native-consoles/build-pico8.sh' >&2
    exit 1
}
test -r "$TASK_CORE/fake08_libretro.so"
TASK_STAGE=$(mktemp -d "$TASK_ROOT/build/.p8-launchers-XXXXXX")
trap 'rm -rf "$TASK_STAGE"' EXIT
mkdir -p "$TASK_STAGE/P8.pak"
cp -R "$TASK_CORE/." "$TASK_STAGE/P8.pak/"
cp "$TASK_ROOT/skeleton/EXTRAS/Emus/tg5040/P8.pak/launch.sh" "$TASK_STAGE/P8.pak/launch.sh"
cp -R "$TASK_NATIVE" "$TASK_STAGE/PICO-8.pak"
# Shell changes do not require recompiling the native helpers.
cp "$TASK_ROOT/ports/native-consoles/pico8-launch.sh" "$TASK_STAGE/PICO-8.pak/launch.sh"
cp "$TASK_ROOT/ports/native-consoles/sync-pico8-favourites.sh" "$TASK_STAGE/PICO-8.pak/sync-pico8-favourites.sh"
cp "$TASK_ROOT/ports/native-consoles/pico8-emu-launch.sh" "$TASK_STAGE/pico8-emu-launch.sh"
cp "$TASK_ROOT/ports/pocketjs-brick/workshop/launch.sh" "$TASK_STAGE/workshop-launch.sh"
TASK_STAMP=$(date +%Y%m%d-%H%M%S)
TASK_REMOTE="/mnt/SDCARD/.userdata/shared/p8-launchers-$TASK_STAMP.tar.gz"
tar -czf "$TASK_STAGE/payload.tar.gz" -C "$TASK_STAGE" P8.pak PICO-8.pak pico8-emu-launch.sh workshop-launch.sh
ssh -T -o BatchMode=yes -o ConnectTimeout=8 "$TASK_HOST" "cat > '$TASK_REMOTE'" < "$TASK_STAGE/payload.tar.gz"
ssh -T -o BatchMode=yes -o ConnectTimeout=8 "$TASK_HOST" sh -s -- "$TASK_STAMP" "$TASK_REMOTE" <<'REMOTE'
set -eu
TASK_STAMP=$1
TASK_ARCHIVE=$2
TASK_SD=/mnt/SDCARD
TASK_SHARED="$TASK_SD/.userdata/shared"
TASK_STAGE="$TASK_SHARED/p8-launchers-stage-$TASK_STAMP"
TASK_BACKUP="$TASK_SHARED/ai-game-workshop-backups/$TASK_STAMP-launchers"
mkdir -p "$TASK_STAGE" "$TASK_BACKUP" "$TASK_SD/Emus/tg5040"
tar -xzf "$TASK_ARCHIVE" -C "$TASK_STAGE"
TASK_P8="$TASK_SD/Emus/tg5040/P8.pak"
# Match NextUI's package selection. A system-only installation must retain
# its core/options rather than be shadowed by a new SD package.
if [ ! -e "$TASK_P8/launch.sh" ] &&
   [ -x "$TASK_SD/.system/tg5040/paks/Emus/P8.pak/launch.sh" ] &&
   [ -r "$TASK_SD/.system/tg5040/paks/Emus/P8.pak/fake08_libretro.so" ]; then
    TASK_P8="$TASK_SD/.system/tg5040/paks/Emus/P8.pak"
fi
TASK_PICO="$TASK_SD/Emus/tg5040/PICO.pak"
TASK_TOOLS="$TASK_SD/Tools/tg5040/PICO-8.pak"
TASK_WORKSHOP="$TASK_SD/Tools/tg5040/AI Game Workshop.pak"
for TASK_ENTRY in "$TASK_P8/launch.sh" "$TASK_PICO/launch.sh" "$TASK_PICO/launch-vendor.sh" "$TASK_WORKSHOP/launch.sh"; do
    if [ -f "$TASK_ENTRY" ]; then
        TASK_REL=${TASK_ENTRY#"$TASK_SD/"}
        mkdir -p "$TASK_BACKUP/$(dirname "$TASK_REL")"
        cp "$TASK_ENTRY" "$TASK_BACKUP/$TASK_REL"
    fi
done
if [ -d "$TASK_TOOLS" ]; then tar -cf "$TASK_BACKUP/PICO-8-tools.tar" -C "$TASK_TOOLS" .; fi
install_file() {
    cp "$1" "$2.new"
    chmod "$3" "$2.new"
    mv -f "$2.new" "$2"
}
mkdir -p "$TASK_P8"
# Preserve the installed core and its options. Only supply missing files.
for TASK_FILE in "$TASK_STAGE/P8.pak/"*; do
    TASK_NAME=${TASK_FILE##*/}
    [ "$TASK_NAME" != launch.sh ] || continue
    if [ ! -e "$TASK_P8/$TASK_NAME" ]; then install_file "$TASK_FILE" "$TASK_P8/$TASK_NAME" 644; fi
done
install_file "$TASK_STAGE/P8.pak/launch.sh" "$TASK_P8/launch.sh" 755

mkdir -p "$TASK_TOOLS/bin" "$TASK_TOOLS/certs"
for TASK_FILE in "$TASK_STAGE/PICO-8.pak/"*; do
    [ -f "$TASK_FILE" ] || continue
    TASK_NAME=${TASK_FILE##*/}
    case "$TASK_NAME" in launch.sh|menu-exit.elf|sync-pico8-favourites.sh) TASK_MODE=755 ;; *) TASK_MODE=644 ;; esac
    install_file "$TASK_FILE" "$TASK_TOOLS/$TASK_NAME" "$TASK_MODE"
done
install_file "$TASK_STAGE/PICO-8.pak/bin/wget" "$TASK_TOOLS/bin/wget" 755
install_file "$TASK_STAGE/PICO-8.pak/certs/cacert.pem" "$TASK_TOOLS/certs/cacert.pem" 644
if [ -d "$TASK_PICO" ]; then
    # Keep the original vendor fallback, never back up this bridge as vendor.
    if [ ! -e "$TASK_PICO/launch-vendor.sh" ] && [ -f "$TASK_PICO/launch.sh" ] &&
       ! grep -q 'NEXTUI_PICO8_.*BRIDGE' "$TASK_PICO/launch.sh"; then
        install_file "$TASK_PICO/launch.sh" "$TASK_PICO/launch-vendor.sh" 755
    fi
    install_file "$TASK_STAGE/pico8-emu-launch.sh" "$TASK_PICO/launch.sh" 755
fi
if [ -d "$TASK_WORKSHOP" ]; then install_file "$TASK_STAGE/workshop-launch.sh" "$TASK_WORKSHOP/launch.sh" 755; fi
export SDCARD_PATH="$TASK_SD" PLATFORM=tg5040 SYSTEM_PATH="$TASK_SD/.system/tg5040"
export PATH="$SYSTEM_PATH/bin:/usr/trimui/bin:$PATH"
"$TASK_P8/launch.sh" --check
if "$TASK_TOOLS/launch.sh" --check; then
    printf '%s\n' 'Official PICO-8 Tools runtime ready.'
else
    printf '%s\n' 'Tools requires the separately installed official PICO-8 runtime; game menus use fake-08.'
fi
for TASK_ENTRY in "$TASK_P8/launch.sh" "$TASK_PICO/launch.sh" "$TASK_TOOLS/launch.sh" "$TASK_WORKSHOP/launch.sh"; do
    if [ -f "$TASK_ENTRY" ]; then sha256sum "$TASK_ENTRY"; fi
done
printf 'Launchers installed. Backup: %s\n' "$TASK_BACKUP"
rm -rf "$TASK_STAGE"
rm -f "$TASK_ARCHIVE"
REMOTE
