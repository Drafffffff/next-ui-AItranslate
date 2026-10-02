#!/usr/bin/env python3
"""Exercise the real launcher without touching hardware or system settings."""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1]


def check(name, configured, daemon, changed=False):
    with tempfile.TemporaryDirectory(prefix='mic-launch-check-') as folder:
        root = Path(folder)
        system = root / 'system'
        shared = root / 'shared'
        package = root / 'package'
        (system / 'etc/bluetooth').mkdir(parents=True)
        (system / 'bin').mkdir()
        shared.mkdir()
        package.mkdir()
        shutil.copy(SOURCE / 'launch.sh', package)
        shutil.copy(SOURCE / 'resume.sync.sh', package)
        (shared / 'minuisettings.txt').write_text(f'bluetooth={int(configured)}\n')

        def script(path, body):
            path.write_text('#!/bin/sh\nset -eu\n' + body)
            path.chmod(0o700)

        script(system / 'bin/pidof', 'test "$TEST_DAEMON" = 1\n')
        script(system / 'etc/bluetooth/bt_init.sh', 'echo "$1" >> "$TEST_EVENTS"\n')
        script(package / 'pocketjs-mic.elf', '''
test "$BRICK_MIC_RECEIVER_RECORD" = /tmp/brick-mic-receiver
if [ "$TEST_CHANGED" = 1 ]; then echo bluetooth=1 > "$BRICK_MIC_SETTINGS"; fi
''')
        env = {**os.environ, 'DEVICE': 'brick', 'SYSTEM_PATH': str(system),
               'SHARED_USERDATA_PATH': str(shared), 'USERDATA_PATH': str(root / 'userdata'),
               'TEST_DAEMON': str(int(daemon)), 'TEST_CHANGED': str(int(changed)),
               'TEST_EVENTS': str(root / 'events')}
        result = subprocess.run(['/bin/sh', str(package / 'launch.sh')], env=env,
                                capture_output=True, timeout=5)
        events = (root / 'events').read_text().splitlines() if (root / 'events').exists() else []
        expected = [] if configured or daemon or changed else ['stop']
        assert result.returncode == 0 and events == expected, (name, result.stderr, events)
        print('PASS: ' + name)


check('system Bluetooth enabled before daemon appears is preserved', True, False)
check('existing daemon is preserved', False, True)
check('application-owned Bluetooth is stopped on exit', False, False)
check('system Bluetooth enabled during application is preserved', False, False, True)
