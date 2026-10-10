#!/bin/sh
set -eu
TASK_SCRIPT=${1:?sync script required}
TASK_CART=${2:?valid cached PNG cart required}
TASK_TEST=$(mktemp -d /tmp/pico-favourites-test-XXXXXX)
TASK_LOCK_PID=""
trap '[ -z "$TASK_LOCK_PID" ] || kill "$TASK_LOCK_PID" 2>/dev/null || :; rm -rf "$TASK_TEST"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
if ! command -v flock >/dev/null 2>&1; then
    printf '%s\n' 'Run this Linux fixture where flock is available (NextUI or verify container).' >&2
    exit 1
fi
TASK_DATA="$TASK_TEST/data folder's"
TASK_ROMS="$TASK_TEST/PICO folder's/Favourites"
mkdir -p "$TASK_DATA/bbs/carts" "$TASK_DATA/bbs/1" "$TASK_ROMS"
sh "$TASK_SCRIPT" "$TASK_DATA" "$TASK_ROMS"
TASK_CACHE="$TASK_DATA/bbs"
cp "$TASK_CART" "$TASK_CACHE/carts/wanted-0.p8.png"
cp "$TASK_CART" "$TASK_CACHE/1/12345.p8.png"
cp "$TASK_CART" "$TASK_CACHE/carts/browsed-0.p8.png"
cp "$TASK_CART" "$TASK_CACHE/carts/owned-0.p8.png"
cp "$TASK_CART" "$TASK_CACHE/carts/modern-1.p8.png"
cp "$TASK_CART" "$TASK_CACHE/1/15133.p8.png"
cp "$TASK_CART" "$TASK_CACHE/carts/existing-0.p8.png"
cp "$TASK_CART" "$TASK_CACHE/carts/long-title-0.p8.png"
head -c 32 "$TASK_CART" > "$TASK_CACHE/carts/partial-0.p8.png"
# A complete-looking footer alone must not accept an invalid PNG header.
printf 'notapng!' > "$TASK_CACHE/carts/bad-magic-0.p8.png"
tail -c 12 "$TASK_CART" >> "$TASK_CACHE/carts/bad-magic-0.p8.png"
printf 'keep my original' > "$TASK_ROMS/owned-0.p8.png"
cp "$TASK_CART" "$TASK_ROMS/existing-0.p8.png"
mkdir -p "$TASK_ROMS/.media"
printf 'keep my cover' > "$TASK_ROMS/.media/owned-0.p8.png"
printf 'owned-0.p8.png\tMy custom game\nother.p8.png\tNot a favourite' > "$TASK_ROMS/map.txt"
printf '||author|0||bbs/carts/wanted-0.p8.png|browsed-0\r\n||author|0||bbs/1/12345.p8.png|another title\nowned-0\npartial-0\nmissing-0\nbad-magic-0\n||author|0||bbs/carts/wanted-0.p8.png|duplicate\n' > "$TASK_DATA/favourites.txt"
printf '|15133                |11722                |1794   |noel             |                     |celeste 1.0\r\n|modern-1     |modern       |1794   |author  |                     |  Modern\tTitle  \n|existing-0|other|1794|author||   \n' >> "$TASK_DATA/favourites.txt"
printf '|long-title-0|long-title|1794|author||' >> "$TASK_DATA/favourites.txt"
awk 'BEGIN {for (i=0;i<100;i++) printf "很长的标题"; printf "\n"}' >> "$TASK_DATA/favourites.txt"
printf '|%0130d|id|1794|author||Too long ID\n' 1 >> "$TASK_DATA/favourites.txt"

# Old mkdir-lock debris must not prevent a new kernel-locked sync.
mkdir "$TASK_DATA/.favourites-sync"
printf 'dead-owner' > "$TASK_DATA/.favourites-sync/owner"
sh "$TASK_SCRIPT" "$TASK_DATA" "$TASK_ROMS"
cmp "$TASK_CART" "$TASK_ROMS/wanted-0.p8.png"
cmp "$TASK_CART" "$TASK_ROMS/12345.p8.png"
cmp "$TASK_CART" "$TASK_ROMS/15133.p8.png"
cmp "$TASK_CART" "$TASK_ROMS/modern-1.p8.png"
cmp "$TASK_CART" "$TASK_ROMS/.media/wanted-0.p8.png"
cmp "$TASK_CART" "$TASK_ROMS/.media/existing-0.p8.png"
[ ! -e "$TASK_ROMS/browsed-0.p8.png" ]
[ ! -e "$TASK_ROMS/partial-0.p8.png" ]
[ ! -e "$TASK_ROMS/missing-0.p8.png" ]
[ ! -e "$TASK_ROMS/bad-magic-0.p8.png" ]
[ "$(cat "$TASK_ROMS/owned-0.p8.png")" = 'keep my original' ]
[ "$(cat "$TASK_ROMS/.media/owned-0.p8.png")" = 'keep my cover' ]
TASK_TAB=$(printf '\t')
grep -Fx "15133.p8.png${TASK_TAB}celeste 1.0" "$TASK_ROMS/map.txt"
grep -Fx "modern-1.p8.png${TASK_TAB}Modern Title" "$TASK_ROMS/map.txt"
grep -Fx "existing-0.p8.png${TASK_TAB}existing-0" "$TASK_ROMS/map.txt"
grep -Fx "long-title-0.p8.png${TASK_TAB}long-title-0" "$TASK_ROMS/map.txt"
grep -Fx "owned-0.p8.png${TASK_TAB}My custom game" "$TASK_ROMS/map.txt"
grep -Fx "other.p8.png${TASK_TAB}Not a favourite" "$TASK_ROMS/map.txt"
LC_ALL=C awk 'length($0)>253 {exit 1}' "$TASK_ROMS/map.txt"
[ "$(awk -F '\t' '$1=="wanted-0.p8.png" {count++} END {print count+0}' "$TASK_ROMS/map.txt")" -eq 1 ]
cp "$TASK_ROMS/map.txt" "$TASK_TEST/map-original"

# A previously synced cart repairs its missing cover even without a cache.
rm -f "$TASK_ROMS/.media/existing-0.p8.png" "$TASK_CACHE/carts/existing-0.p8.png"
sh "$TASK_SCRIPT" "$TASK_DATA" "$TASK_ROMS" > "$TASK_TEST/repeat.log"
cmp "$TASK_CART" "$TASK_ROMS/.media/existing-0.p8.png"
cmp "$TASK_TEST/map-original" "$TASK_ROMS/map.txt"
grep -q 'Synced 0 new' "$TASK_TEST/repeat.log"

# English spelling is accepted, and an empty title falls back to the ID.
mv "$TASK_DATA/favourites.txt" "$TASK_DATA/favorites.txt"
sh "$TASK_SCRIPT" "$TASK_DATA" "$TASK_ROMS" > "$TASK_TEST/english.log"
cmp "$TASK_TEST/map-original" "$TASK_ROMS/map.txt"

# A live lock blocks sync without removing its file or changing any ROM. Kill
# that exact owner (no subprocess inherits its FD) to simulate SIGKILL debris.
(
    exec 9>> "$TASK_DATA/.favourites-sync.lock"
    flock 9
    : > "$TASK_TEST/lock-ready"
    while :; do :; done
) &
TASK_LOCK_PID=$!
TASK_WAIT=0
while [ ! -f "$TASK_TEST/lock-ready" ]; do
    TASK_WAIT=$((TASK_WAIT+1))
    [ "$TASK_WAIT" -le 50 ] || exit 1
    sleep 0.02
done
rm -f "$TASK_ROMS/.media/existing-0.p8.png"
sh "$TASK_SCRIPT" "$TASK_DATA" "$TASK_ROMS"
[ ! -e "$TASK_ROMS/.media/existing-0.p8.png" ]
[ -f "$TASK_DATA/.favourites-sync.lock" ]
cmp "$TASK_TEST/map-original" "$TASK_ROMS/map.txt"
kill -KILL "$TASK_LOCK_PID"
wait "$TASK_LOCK_PID" 2>/dev/null || :
TASK_LOCK_PID=""
sh "$TASK_SCRIPT" "$TASK_DATA" "$TASK_ROMS" > "$TASK_TEST/recovered.log"
cmp "$TASK_CART" "$TASK_ROMS/.media/existing-0.p8.png"

# Cancelling a favourite does not remove its ROM, cover or custom map label.
printf '|15133|11722|1794|noel||celeste 1.0\n' > "$TASK_DATA/favorites.txt"
sh "$TASK_SCRIPT" "$TASK_DATA" "$TASK_ROMS" > "$TASK_TEST/cancelled.log"
[ -f "$TASK_ROMS/owned-0.p8.png" ]
[ -f "$TASK_ROMS/.media/owned-0.p8.png" ]
cmp "$TASK_TEST/map-original" "$TASK_ROMS/map.txt"

# Copy failures leave no partially published cover or temporary work folder.
TASK_CP=$(command -v cp)
mkdir "$TASK_TEST/failing-bin"
cat > "$TASK_TEST/failing-bin/cp" <<'EOF'
#!/bin/sh
case "$2" in */cover) exit 37 ;; esac
exec "$TASK_REAL_CP" "$@"
EOF
chmod +x "$TASK_TEST/failing-bin/cp"
rm -f "$TASK_ROMS/.media/15133.p8.png"
TASK_STATUS=0
TASK_REAL_CP="$TASK_CP" PATH="$TASK_TEST/failing-bin:$PATH" \
    sh "$TASK_SCRIPT" "$TASK_DATA" "$TASK_ROMS" > "$TASK_TEST/failure.log" 2>&1 || TASK_STATUS=$?
[ "$TASK_STATUS" -eq 37 ]
[ ! -e "$TASK_ROMS/.media/15133.p8.png" ]
cmp "$TASK_TEST/map-original" "$TASK_ROMS/map.txt"
set -- "$TASK_ROMS/".splore-sync-*
[ ! -e "$1" ]
sh "$TASK_SCRIPT" "$TASK_DATA" "$TASK_ROMS" > "$TASK_TEST/retry.log"
cmp "$TASK_CART" "$TASK_ROMS/.media/15133.p8.png"
# Successful runs do not leave temporary publication directories or files.
set -- "$TASK_ROMS/".splore-sync-*
[ ! -e "$1" ]
echo 'PASS: modern titles/map aliases, safe long-title fallback, existing-cover repair, full PNG markers, quoted paths, duplicates, cache shards, English favourites, live/SIGKILL locks, no overwrite/deletion/temp debris'
