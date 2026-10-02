#!/bin/sh
set -eu
taskTestsDir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
taskMacDir=$(CDPATH= cd -- "$taskTestsDir/.." && pwd)
taskTempDir=$(mktemp -d "${TMPDIR:-/tmp}/brick-mic-composer-focus.XXXXXX")
trap 'rm -rf "$taskTempDir"' EXIT HUP INT TERM
cat "$taskMacDir/Control.swift" "$taskTestsDir/ComposerFocusHarness.swift" > "$taskTempDir/Harness.swift"
swiftc -swift-version 5 -parse-as-library -framework AppKit \
    -framework ApplicationServices -framework Network \
    "$taskMacDir/NotificationDelivery.swift" "$taskMacDir/DesktopTaskMetadata.swift" "$taskTempDir/Harness.swift" \
    -o "$taskTempDir/test"
"$taskTempDir/test"
