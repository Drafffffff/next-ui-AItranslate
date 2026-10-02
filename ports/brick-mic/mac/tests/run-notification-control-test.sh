#!/bin/sh
set -eu

taskTestsDir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
taskMacDir=$(CDPATH= cd -- "$taskTestsDir/.." && pwd)
taskTempDir=$(mktemp -d "${TMPDIR:-/tmp}/brick-mic-notification-control.XXXXXX")
trap 'rm -rf "$taskTempDir"' EXIT HUP INT TERM

# Keep Control.swift intact and append the fixture in the same file. Swift's
# same-file extensions can exercise private boundaries without modifying them.
cat "$taskMacDir/Control.swift" "$taskTestsDir/NotificationControlHarness.swift" > "$taskTempDir/Harness.swift"
swiftc -swift-version 5 -parse-as-library -framework AppKit \
    -framework ApplicationServices -framework Network \
    "$taskMacDir/NotificationDelivery.swift" "$taskMacDir/DesktopTaskMetadata.swift" "$taskTempDir/Harness.swift" \
    -o "$taskTempDir/test"
mkdir "$taskTempDir/fixtures"
"$taskTempDir/test" "$taskTempDir/fixtures"
