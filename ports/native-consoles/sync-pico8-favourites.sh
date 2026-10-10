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
if ! command -v flock >/dev/null 2>&1; then
    printf '%s\n' 'Splore favourites sync requires flock (included on NextUI Linux).' >&2
    exit 1
fi
# Kernel locks are released after SIGKILL too. Never unlink a flock file while
# another process may hold its inode. The old mkdir lock is no longer used.
exec 9>> "$TASK_DATA/.favourites-sync.lock"
if ! flock -n 9; then exit 0; fi
mkdir -p "$TASK_ROMS"
TASK_WORK=$(mktemp -d "$TASK_ROMS/.splore-sync-XXXXXX")
trap 'rm -rf "$TASK_WORK"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
# 0.2.7 online favourites store a cart ID in field 2, without a path. Older
# and local records store a path. Never interpret titles or authors as IDs.
LC_ALL=C awk -F '|' '
    function trim(text) {sub(/^[[:space:]]+/, "", text); sub(/[[:space:]]+$/, "", text); return text}
    {
        sub(/\r$/, "")
        value=""
        title=""
        id=trim($2)
        if (NF>1 && trim($1)=="" && id ~ /^[A-Za-z0-9_-]+$/) {
            value=id
            # Leading | produces empty $1: the zero-based title field 6 is
            # awk $7. Accept compact records lacking the reserved column too.
            title=(NF>=7 ? $7 : $6)
        }
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
            # Only modern records have a title field. Keep filenames
            # intact; sanitise title separators before emitting map rows.
            gsub(/[\t\r\n]/, " ", title)
            title=trim(title)
            if (title=="") {title=value; sub(/\.p8\.png$/, "", title)}
            # NextUI reads map.txt with a 256-byte line buffer. Fall back to
            # the ID instead of cutting UTF-8 title bytes or splitting a row.
            if (length(value)+length(title)+2>254) {title=value; sub(/\.p8\.png$/, "", title)}
            if (length(value)<=120 && value !~ /[[:cntrl:]]/ && value !~ /^\./ &&
                value !~ /^temp-/ && !seen[value]++)
                print value "\t" title
        }
    }
' "$TASK_FAV" > "$TASK_WORK/wanted"

hex_bytes() {
    if command -v hexdump >/dev/null 2>&1; then
        hexdump -v -e '1/1 "%02x"'
    else
        od -An -v -tx1 | tr -d ' \n'
    fi
}
complete_png() {
    [ -f "$1" ] || return 1
    TASK_HEAD=$(head -c 8 "$1" | hex_bytes)
    TASK_TAIL=$(tail -c 12 "$1" | hex_bytes)
    [ "$TASK_HEAD" = 89504e470d0a1a0a ] && [ "$TASK_TAIL" = 0000000049454e44ae426082 ]
}
path_exists() { [ -e "$1" ] || [ -L "$1" ]; }

TASK_MAP="$TASK_ROMS/map.txt"
if path_exists "$TASK_MAP" && [ ! -f "$TASK_MAP" ]; then
    printf '%s\n' 'Cannot update favourites: map.txt is not a regular file.' >&2
    exit 1
fi
TASK_COUNT=0
: > "$TASK_WORK/labels"
TASK_TAB=$(printf '\t')
while IFS="$TASK_TAB" read -r TASK_NAME TASK_TITLE; do
    TASK_DEST="$TASK_ROMS/$TASK_NAME"
    TASK_SOURCE=""
    # Existing cartridges also receive missing metadata and covers. Prefer
    # their own image; use a complete cache only when that image is invalid.
    if complete_png "$TASK_DEST"; then TASK_SOURCE=$TASK_DEST; fi
    if [ -z "$TASK_SOURCE" ]; then
        for TASK_CART in "$TASK_DATA/bbs/$TASK_NAME" "$TASK_DATA/bbs"/*/"$TASK_NAME"; do
            if complete_png "$TASK_CART"; then TASK_SOURCE=$TASK_CART; break; fi
        done
    fi
    if ! path_exists "$TASK_DEST" && [ -n "$TASK_SOURCE" ]; then
        cp "$TASK_SOURCE" "$TASK_WORK/cart"
        # -n also protects a file created by another app during the copy.
        mv -n "$TASK_WORK/cart" "$TASK_DEST"
        if [ ! -e "$TASK_WORK/cart" ]; then TASK_COUNT=$((TASK_COUNT+1)); fi
    fi
    [ -f "$TASK_DEST" ] || continue
    printf '%s\t%s\n' "$TASK_NAME" "$TASK_TITLE" >> "$TASK_WORK/labels"
    TASK_MEDIA="$TASK_ROMS/.media/${TASK_NAME%.*}.png"
    if ! path_exists "$TASK_MEDIA" && [ -n "$TASK_SOURCE" ]; then
        mkdir -p "$TASK_ROMS/.media"
        # Put the temporary cover on the destination filesystem so the final
        # rename is atomic. Cleanup removes an interrupted/failed temp copy.
        cp "$TASK_SOURCE" "$TASK_WORK/cover"
        mv -n "$TASK_WORK/cover" "$TASK_MEDIA"
    fi
done < "$TASK_WORK/wanted"

if [ -f "$TASK_MAP" ]; then cp "$TASK_MAP" "$TASK_WORK/map-original"; else : > "$TASK_WORK/map-original"; fi
awk -F '\t' '
    FILENAME==ARGV[1] { if (NF>1) seen[$1]=1; next }
    !seen[$1]++ {print}
' "$TASK_WORK/map-original" "$TASK_WORK/labels" > "$TASK_WORK/map-additions"
if [ -s "$TASK_WORK/map-additions" ]; then
    # Existing user and non-favourite aliases are kept byte-for-byte. Only add
    # names that have no mapping, with a newline separator if one is missing.
    cp "$TASK_WORK/map-original" "$TASK_WORK/map"
    if [ -s "$TASK_WORK/map" ] && [ "$(tail -c 1 "$TASK_WORK/map" | hex_bytes)" != 0a ]; then
        printf '\n' >> "$TASK_WORK/map"
    fi
    cat "$TASK_WORK/map-additions" >> "$TASK_WORK/map"
    mv "$TASK_WORK/map" "$TASK_MAP"
fi
echo "Synced $TASK_COUNT new Splore favourites to $TASK_ROMS"
