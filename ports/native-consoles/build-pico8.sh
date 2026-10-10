#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
TASK_OUT="$TASK_ROOT/build/native-consoles/PICO-8.pak"
TASK_SOURCE="$TASK_ROOT/ports/native-consoles"
mkdir -p "$TASK_OUT/bin" "$TASK_OUT/certs"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" /bin/bash -euc '
    aarch64-nextui-linux-gnu-gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -O2 /work/ports/native-consoles/menu-exit.c -o /work/build/native-consoles/PICO-8.pak/menu-exit.elf
    aarch64-nextui-linux-gnu-gcc -shared -fPIC -Wall -Wextra -Werror -O2 $(pkg-config --cflags sdl2) /work/ports/native-consoles/sdl-nosensor.c -ldl -o /work/build/native-consoles/PICO-8.pak/sdl-nosensor.so
'
cp "$TASK_SOURCE/pico8-launch.sh" "$TASK_OUT/launch.sh"
cp "$TASK_SOURCE/pico8-wget.sh" "$TASK_OUT/bin/wget"
cp "$TASK_SOURCE/pico8-config.txt" "$TASK_OUT/config.txt"
cp "$TASK_SOURCE/pico8-controllers.txt" "$TASK_OUT/sdl_controllers.txt"
cp "$TASK_SOURCE/sync-pico8-favourites.sh" "$TASK_SOURCE/PICO-WRAPPER-LICENSE.txt" "$TASK_SOURCE/README.md" "$TASK_OUT/"
cp "$TASK_SOURCE/certs/cacert.pem" "$TASK_OUT/certs/"
cp "$TASK_ROOT/LICENSE" "$TASK_OUT/NEXTUI-LICENSE.txt"
chmod +x "$TASK_OUT/launch.sh" "$TASK_OUT/bin/wget" "$TASK_OUT/menu-exit.elf"
printf 'Built PICO-8 launcher package (no commercial runtime): %s\n' "$TASK_OUT"
