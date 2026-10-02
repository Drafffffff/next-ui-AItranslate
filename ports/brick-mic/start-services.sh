#!/bin/sh
# Spawned by the PocketJS host only after its first frame has been presented.
set -eu
printf 'bluetooth\n' > "$BRICK_MIC_RUNTIME/stage"
SYSTEM_ROOT="${SYSTEM_PATH:-/mnt/SDCARD/.system/tg5040}"
bluetooth_ready() {
    pidof bluetoothd >/dev/null 2>&1 && hciconfig hci0 2>/dev/null | grep -q 'UP RUNNING'
}
if [ "${1:-}" = --resume ]; then
    # NextUI suspend restores Bluetooth in the background, after Wi-Fi. Calling
    # bt_init concurrently would kill the other initializer's hciattach process.
    attempts=0
    while ! bluetooth_ready; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 180 ]; then
            echo 'Timed out waiting for NextUI to restore Bluetooth' >&2
            exit 1
        fi
        usleep 100000
    done
    echo 'NextUI restored Bluetooth; restarting the microphone service' >&2
elif ! bluetooth_ready && ! "$SYSTEM_ROOT/etc/bluetooth/bt_init.sh" start; then
    if ! bluetooth_ready; then
        echo 'Bluetooth initialization failed: no running controller/service' >&2
        exit 1
    fi
    echo 'Bluetooth is already available; continuing startup' >&2
fi
printf 'service\n' > "$BRICK_MIC_RUNTIME/stage"
exec ./brick-micd --socket "$BRICK_MIC_SOCKET"
