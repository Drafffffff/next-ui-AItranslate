#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
TASK_NET="$TASK_ROOT/build/creative-lab/network"
mkdir -p "$TASK_NET/lib"
# Headers match the libcurl supplied by the Brick firmware. These device
# libraries are link-time inputs only; installation uses the firmware copies.
if [ ! -d "$TASK_NET/curl-7.54.1" ]; then
    curl -fL --max-time 60 https://codeload.github.com/curl/curl/tar.gz/refs/tags/curl-7_54_1 -o "$TASK_NET/curl.tar.gz"
    tar -xf "$TASK_NET/curl.tar.gz" -C "$TASK_NET"
    mv "$TASK_NET/curl-curl-7_54_1" "$TASK_NET/curl-7.54.1"
fi
cp "$TASK_NET/curl-7.54.1/include/curl/curlbuild.h.dist" "$TASK_NET/curl-7.54.1/include/curl/curlbuild.h"
if [ ! -f "$TASK_NET/lib/libcurl.so.4.4.0" ]; then
    ssh nextui-brick 'tar -cf - -C /usr/lib libcurl.so.4.4.0 libnghttp2.so.14.13.3 libssl.so.1.1 libcrypto.so.1.1' > "$TASK_NET/device-libs.tar"
    tar -xf "$TASK_NET/device-libs.tar" -C "$TASK_NET/lib"
fi
ln -sf libcurl.so.4.4.0 "$TASK_NET/lib/libcurl.so.4"
ln -sf libnghttp2.so.14.13.3 "$TASK_NET/lib/libnghttp2.so.14"
