#!/bin/sh
set -eu

taskTestsDir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
taskMacDir=$(CDPATH= cd -- "$taskTestsDir/.." && pwd)
taskTempDir=$(mktemp -d "${TMPDIR:-/tmp}/brick-mic-desktop-metadata.XXXXXX")
trap 'rm -rf "$taskTempDir"' EXIT HUP INT TERM

# Pure filesystem fixtures only. No Codex process, UI, RPC or user task is read.
swiftc -swift-version 5 -parse-as-library \
    "$taskMacDir/DesktopTaskMetadata.swift" "$taskTestsDir/DesktopTaskMetadataHarness.swift" \
    -o "$taskTempDir/test"
mkdir "$taskTempDir/fixtures"
"$taskTempDir/test" "$taskTempDir/fixtures"
