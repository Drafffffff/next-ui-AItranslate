#!/bin/sh
set -eu
TASK_DATA=${1:?PICO-8 data folder required}
TASK_ROMS=${2:?ROM folder required}
TASK_FAV="$TASK_DATA/favourites.txt"
if [ ! -f "$TASK_FAV" ]; then TASK_FAV="$TASK_DATA/favorites.txt"; fi
if [ ! -s "$TASK_FAV" ]; then
    echo 'No Splore favourites yet; nothing to sync.'
    exit 0
fi
TASK_LOCK="$TASK_DATA/.favourites-sync"
if ! mkdir "$TASK_LOCK" 2>/dev/null; then exit 0; fi
trap 'rm -rf "$TASK_LOCK"' EXIT HUP INT TERM
# 0.2.7 online favourites store a cart ID in field 2, without a path. Older
# and local records store a path. Never interpret titles or authors as IDs.
awk -F '|' '
    function trim(text) {sub(/^[[:space:]]+/, "", text); sub(/[[:space:]]+$/, "", text); return text}
    {
        sub(/\r$/, "")
        value=""
        id=trim($2)
        if (NF>1 && trim($1)=="" && id ~ /^[A-Za-z0-9_-]+$/) value=id
        else if (NF==1 && trim($1) ~ /^[A-Za-z0-9_-]+$/) value=trim($1)
        else {
            for (i=1; i<=NF; i++) {
                token=trim($i)
                if (token ~ /\.p8\.png$/) {value=token; break}
            }
        }
        if (value!="") {
            sub(/^.*\//, "", value)
            if (value !~ /\.p8\.png$/) value=value ".p8.png"
            if (value !~ /^temp-/ && !seen[value]++) print value
        }
    }
' "$TASK_FAV" > "$TASK_LOCK/wanted"
mkdir -p "$TASK_ROMS"
TASK_COUNT=0
while IFS= read -r TASK_NAME; do
    TASK_DEST="$TASK_ROMS/$TASK_NAME"
    # Do not overwrite user files or remove previously copied games.
    [ ! -e "$TASK_DEST" ] || continue
    for TASK_CART in "$TASK_DATA/bbs/$TASK_NAME" "$TASK_DATA/bbs"/*/"$TASK_NAME"; do
        [ -f "$TASK_CART" ] || continue
        # Ignore missing and incomplete prefetch downloads. Require PNG magic
        # and the final IEND chunk before publishing a cartridge to NextUI.
        TASK_HEAD=$(head -c 8 "$TASK_CART" | hexdump -v -e '1/1 "%02x"')
        TASK_TAIL=$(tail -c 12 "$TASK_CART" | hexdump -v -e '1/1 "%02x"')
        [ "$TASK_HEAD" = 89504e470d0a1a0a ] || continue
        [ "$TASK_TAIL" = 0000000049454e44ae426082 ] || continue
        TASK_TEMP="$TASK_ROMS/.splore-sync-$TASK_NAME"
        cp "$TASK_CART" "$TASK_TEMP"
        mv "$TASK_TEMP" "$TASK_DEST"
        mkdir -p "$TASK_ROMS/.media"
        TASK_MEDIA="$TASK_ROMS/.media/${TASK_NAME%.*}.png"
        if [ ! -e "$TASK_MEDIA" ]; then cp "$TASK_CART" "$TASK_MEDIA"; fi
        TASK_COUNT=$((TASK_COUNT+1))
        break
    done
done < "$TASK_LOCK/wanted"
echo "Synced $TASK_COUNT new Splore favourites to $TASK_ROMS"
