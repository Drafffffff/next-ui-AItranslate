#!/bin/sh
set -eu
TASK_SCRIPT=${1:?sync script required}
TASK_CART=${2:?valid cached PNG cart required}
TASK_TEST=$(mktemp -d /tmp/pico-favourites-test-XXXXXX)
trap 'rm -rf "$TASK_TEST"' EXIT HUP INT TERM
mkdir -p "$TASK_TEST/data/bbs/carts" "$TASK_TEST/data/bbs/1" "$TASK_TEST/roms"
sh "$TASK_SCRIPT" "$TASK_TEST/data" "$TASK_TEST/roms"
cp "$TASK_CART" "$TASK_TEST/data/bbs/carts/wanted-0.p8.png"
cp "$TASK_CART" "$TASK_TEST/data/bbs/1/12345.p8.png"
cp "$TASK_CART" "$TASK_TEST/data/bbs/carts/browsed-0.p8.png"
cp "$TASK_CART" "$TASK_TEST/data/bbs/carts/owned-0.p8.png"
head -c 32 "$TASK_CART" > "$TASK_TEST/data/bbs/carts/partial-0.p8.png"
printf 'keep my original' > "$TASK_TEST/roms/owned-0.p8.png"
printf '||author|0||bbs/carts/wanted-0.p8.png|browsed-0\r\n||author|0||bbs/1/12345.p8.png|another title\nowned-0\npartial-0\nmissing-0\n||author|0||bbs/carts/wanted-0.p8.png|duplicate\n' > "$TASK_TEST/data/favourites.txt"
sh "$TASK_SCRIPT" "$TASK_TEST/data" "$TASK_TEST/roms"
cmp "$TASK_CART" "$TASK_TEST/roms/wanted-0.p8.png"
cmp "$TASK_CART" "$TASK_TEST/roms/12345.p8.png"
cmp "$TASK_CART" "$TASK_TEST/roms/.media/wanted-0.p8.png"
[ ! -e "$TASK_TEST/roms/browsed-0.p8.png" ]
[ ! -e "$TASK_TEST/roms/partial-0.p8.png" ]
[ ! -e "$TASK_TEST/roms/missing-0.p8.png" ]
[ "$(cat "$TASK_TEST/roms/owned-0.p8.png")" = 'keep my original' ]
sh "$TASK_SCRIPT" "$TASK_TEST/data" "$TASK_TEST/roms" > "$TASK_TEST/repeat.log"
grep -q 'Synced 0 new' "$TASK_TEST/repeat.log"
echo 'PASS: favourites only, sharded cache, metadata, duplicates, previews, incomplete downloads and existing ROM preservation'
