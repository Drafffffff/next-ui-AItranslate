#!/bin/sh
set -eu

taskTestsDir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
taskMacDir=$(CDPATH= cd -- "$taskTestsDir/.." && pwd)
taskTempDir=$(mktemp -d "${TMPDIR:-/tmp}/brick-mic-short-input.XXXXXX")
trap 'rm -rf "$taskTempDir"' EXIT HUP INT TERM

python3 - "$taskMacDir/main.swift" "$taskTestsDir/ShortInputMessageHarness.swift" "$taskTempDir/Harness.swift" <<'PY'
from pathlib import Path
import sys

production = Path(sys.argv[1]).read_text()
fixture = Path(sys.argv[2]).read_text()

def between(start, end):
    if production.count(start) != 1 or production.count(end) != 1:
        raise SystemExit(f"Production extraction markers changed: {start!r}")
    return production[production.index(start):production.index(end)]

methods = between("    func message(", "    func insert(")
disconnect = between("        ble.onDisconnect=", "        ble.onMessage=")
for marker, original in [
    ("    // INJECT_PRODUCTION_MESSAGE_METHODS", methods),
    ("        // INJECT_PRODUCTION_DISCONNECT_CALLBACK", disconnect),
]:
    if fixture.count(marker) != 1:
        raise SystemExit(f"Fixture extraction marker changed: {marker!r}")
    fixture = fixture.replace(marker, original)

Path(sys.argv[3]).write_text(fixture)
PY

swiftc "$taskMacDir/Codec.swift" "$taskMacDir/ShortClipGate.swift" \
    "$taskTempDir/Harness.swift" -o "$taskTempDir/test"
"$taskTempDir/test"
