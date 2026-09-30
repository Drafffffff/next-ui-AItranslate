#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
source "$TASK_ROOT/ports/pocketjs-brick/pins.env"
TASK_OUT="$TASK_ROOT/build/native-consoles"
TASK_SOURCE="$TASK_ROOT/ports/native-consoles"
mkdir -p "$TASK_OUT/PICO-8.pak" "$TASK_OUT/TIC-80.pak"
docker run --rm --platform linux/arm64 -v "$TASK_ROOT:/work" "$TOOLCHAIN_IMAGE" /bin/bash -c '
    aarch64-nextui-linux-gnu-gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -O2 /work/ports/native-consoles/menu-exit.c -o /work/build/native-consoles/PICO-8.pak/menu-exit.elf
    aarch64-nextui-linux-gnu-gcc -shared -fPIC -Wall -Wextra -Werror -O2 $(pkg-config --cflags sdl2) /work/ports/native-consoles/sdl-nosensor.c -ldl -o /work/build/native-consoles/PICO-8.pak/sdl-nosensor.so
'
cp "$TASK_OUT/PICO-8.pak/menu-exit.elf" "$TASK_OUT/TIC-80.pak/menu-exit.elf"
cp "$TASK_OUT/PICO-8.pak/sdl-nosensor.so" "$TASK_OUT/TIC-80.pak/sdl-nosensor.so"
cp "$TASK_SOURCE/pico8-launch.sh" "$TASK_OUT/PICO-8.pak/launch.sh"
cp "$TASK_SOURCE/pico8-config.txt" "$TASK_OUT/PICO-8.pak/config.txt"
cp "$TASK_SOURCE/pico8-controllers.txt" "$TASK_OUT/PICO-8.pak/sdl_controllers.txt"
cp "$TASK_SOURCE/PICO-WRAPPER-LICENSE.txt" "$TASK_OUT/PICO-8.pak/"
TASK_TIC="$TASK_ROOT/build/creative-lab/TIC-80 Lab.pak"
if [ ! -f "$TASK_TIC/tic80.elf" ]; then bash "$TASK_ROOT/ports/tic80-brick/build.sh"; fi
cp "$TASK_SOURCE/tic80-launch.sh" "$TASK_OUT/TIC-80.pak/launch.sh"
cp "$TASK_TIC/tic80.elf" "$TASK_TIC/TIC80-LICENSE.txt" "$TASK_TIC/NEXTUI-PORT-LICENSE.txt" "$TASK_OUT/TIC-80.pak/"
cp -R "$TASK_TIC/licenses" "$TASK_OUT/TIC-80.pak/"
cp "$TASK_SOURCE/README.md" "$TASK_OUT/PICO-8.pak/"
cp "$TASK_SOURCE/README.md" "$TASK_OUT/TIC-80.pak/"
chmod +x "$TASK_OUT/PICO-8.pak/launch.sh" "$TASK_OUT/PICO-8.pak/menu-exit.elf" "$TASK_OUT/TIC-80.pak/launch.sh" "$TASK_OUT/TIC-80.pak/tic80.elf"
echo "Built native console packages: $TASK_OUT"
