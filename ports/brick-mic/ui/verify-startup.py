#!/usr/bin/env python3
"""Verify startup ownership without Bluetooth hardware or cloud calls."""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / 'ports/brick-mic/start-services.sh'


def check(name, already_ready=False, resumed=False, restored_after=0):
    with tempfile.TemporaryDirectory(prefix='mic-startup-check-') as folder:
        root = Path(folder)
        binary = root / 'bin'
        runtime = root / 'runtime'
        system = root / 'system'
        binary.mkdir()
        runtime.mkdir()
        (system / 'etc/bluetooth').mkdir(parents=True)

        def executable(path, content):
            path.write_text('#!/bin/sh\nset -eu\n' + content)
            path.chmod(0o700)

        executable(binary / 'pidof', 'test -f "$TEST_ROOT/ready"\n')
        executable(binary / 'hciconfig', 'test -f "$TEST_ROOT/ready" && echo "UP RUNNING"\n')
        executable(binary / 'usleep', '''
count=0
[ ! -f "$TEST_ROOT/count" ] || read -r count < "$TEST_ROOT/count"
count=$((count + 1))
echo "$count" > "$TEST_ROOT/count"
if [ "$RESTORED_AFTER" -gt 0 ] && [ "$count" -ge "$RESTORED_AFTER" ]; then
    touch "$TEST_ROOT/ready"
fi
''')
        executable(system / 'etc/bluetooth/bt_init.sh', 'echo init >> "$TEST_ROOT/events"\ntouch "$TEST_ROOT/ready"\n')
        executable(root / 'brick-micd', 'echo service >> "$TEST_ROOT/events"\n')
        if already_ready:
            (root / 'ready').touch()
        env = {**os.environ, 'PATH': str(binary) + ':' + os.environ['PATH'],
               'TEST_ROOT': str(root), 'RESTORED_AFTER': str(restored_after),
               'SYSTEM_PATH': str(system), 'BRICK_MIC_RUNTIME': str(runtime),
               'BRICK_MIC_SOCKET': str(runtime / 'mic.sock')}
        result = subprocess.run(['/bin/sh', str(SCRIPT)] + (['--resume'] if resumed else []),
                                cwd=root, env=env, capture_output=True, timeout=10)
        events = (root / 'events').read_text().splitlines() if (root / 'events').exists() else []
        if resumed and not restored_after and not already_ready:
            assert result.returncode == 1 and events == [], (name, result, events)
            assert (root / 'count').read_text().strip() == '179'
            assert (runtime / 'stage').read_text().strip() == 'bluetooth'
        else:
            assert result.returncode == 0, (name, result.stderr)
            assert events == (['init', 'service'] if not resumed and not already_ready else ['service']), events
            assert (runtime / 'stage').read_text().strip() == 'service'
        print('PASS: ' + name)


check('cold startup initializes Bluetooth once')
check('available Bluetooth avoids reinitialization', already_ready=True)
check('deep wake waits for NextUI without restarting its controller', resumed=True, restored_after=3)
check('failed system restore times out without competing initialization', resumed=True)
