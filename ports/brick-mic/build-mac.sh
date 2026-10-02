#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
INSTALL_AFTER_BUILD=false
case "${1:-}" in
  "") ;;
  --install) INSTALL_AFTER_BUILD=true; shift ;;
  *) printf 'Usage: %s [--install]\n' "$0" >&2; exit 2 ;;
esac
if [ "$#" -ne 0 ]; then printf 'Usage: %s [--install]\n' "$0" >&2; exit 2; fi
SIGNING_HELPER="$TASK_ROOT/ports/brick-mic/mac-signing.py"
# Resolve the already-pinned public identity before compiling or changing any
# bundle. A missing/unavailable identity must never fall back to adhoc signing.
SIGNING_IDENTITY=$(python3 "$SIGNING_HELPER" identity)
OUTPUT="$TASK_ROOT/build/brick-mic"
APP="$OUTPUT/Brick Mic.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
rm -rf "$APP/Contents/Resources/lucide"
cp -R "$TASK_ROOT/ports/brick-mic/mac/assets/lucide" "$APP/Contents/Resources/"
swiftc -swift-version 5 -framework AppKit "$TASK_ROOT/ports/brick-mic/mac/Brand.swift" "$TASK_ROOT/ports/brick-mic/mac/render-icon.swift" -o "$OUTPUT/render-icon"
"$OUTPUT/render-icon" "$OUTPUT/BrickMic.iconset"
iconutil -c icns "$OUTPUT/BrickMic.iconset" -o "$APP/Contents/Resources/BrickMic.icns"
swiftc -swift-version 5 -O -framework AppKit -framework CoreBluetooth -framework ApplicationServices \
  "$TASK_ROOT/ports/brick-mic/mac/Credentials.swift" \
  "$TASK_ROOT/ports/brick-mic/mac/Codec.swift" "$TASK_ROOT/ports/brick-mic/mac/ASR.swift" \
  "$TASK_ROOT/ports/brick-mic/mac/ShortClipGate.swift" \
  "$TASK_ROOT/ports/brick-mic/mac/SingleInstance.swift" \
  "$TASK_ROOT/ports/brick-mic/mac/NotificationDelivery.swift" \
  "$TASK_ROOT/ports/brick-mic/mac/DesktopTaskMetadata.swift" \
  "$TASK_ROOT/ports/brick-mic/mac/Control.swift" "$TASK_ROOT/ports/brick-mic/mac/Bluetooth.swift" "$TASK_ROOT/ports/brick-mic/mac/Brand.swift" \
  "$TASK_ROOT/ports/brick-mic/mac/Permissions.swift" "$TASK_ROOT/ports/brick-mic/mac/StatusBar.swift" "$TASK_ROOT/ports/brick-mic/mac/Interface.swift" "$TASK_ROOT/ports/brick-mic/mac/main.swift" \
  -o "$APP/Contents/MacOS/BrickMic"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>com.nextui.brickmic</string>
<key>CFBundleName</key><string>Brick Mic</string>
<key>CFBundleIconFile</key><string>BrickMic</string>
<key>CFBundleExecutable</key><string>BrickMic</string>
<key>CFBundleVersion</key><string>20</string>
<key>CFBundleShortVersionString</key><string>0.4.16</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>LSUIElement</key><true/>
<key>NSBluetoothAlwaysUsageDescription</key><string>通过蓝牙接收你在 TrimUI Brick 上按键录制的语音。</string>
<key>NSBluetoothPeripheralUsageDescription</key><string>连接 TrimUI Brick 语音输入设备。</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign --force --sign "$SIGNING_IDENTITY" --identifier com.nextui.brickmic "$APP"
python3 "$SIGNING_HELPER" verify "$APP"
printf 'Built: %s\n' "$APP"
if [ "$INSTALL_AFTER_BUILD" = true ]; then
  "$TASK_ROOT/ports/brick-mic/install-mac.sh"
fi
