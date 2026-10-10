#!/usr/bin/env python3
"""Exercise package launcher selection and startup checks without a handheld."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
WORKSHOP = Path(__file__).with_name('launch.sh')
BUILD = ROOT / 'ports/pocketjs-brick/container-build-app.sh'


def verify():
    # Execute the actual packaging commands, excluding the native compilation.
    source = BUILD.read_text()
    match = re.search(r"^if \[ [^\n]*POCKETJS_PACKAGE_NAME[^\n]*\n.*?^fi$", source, re.M | re.S)
    assert match, 'Package-specific launcher selection is missing'
    with tempfile.TemporaryDirectory(prefix='workshop-launch-package-') as directory:
        base = Path(directory)
        for name, launcher in [('AI Game Workshop.pak', WORKSHOP),
                               ('PocketJS App.pak', ROOT / 'ports/pocketjs-brick/launch-app.sh')]:
            package = base / name
            package.mkdir()
            subprocess.run(['bash', '-eu', '-c', match[0]], cwd=ROOT,
                           env={**os.environ, 'PKG': str(package), 'POCKETJS_PACKAGE_NAME': name}, check=True)
            assert (package / 'launch.sh').read_bytes() == launcher.read_bytes()
        print('PASS: workshop and ordinary app packages receive their own launchers')

        sd = base / "SD card's 游戏"
        data = sd / '.userdata/shared/ai-game-workshop'
        data.mkdir(parents=True)
        request = data / 'play-request.txt'
        request.write_text('game-existing-request')
        package = base / 'AI Game Workshop.pak'
        # The app stub checks what a real native boot requires, and rejects
        # the missing workshop environment that caused the reported crash.
        app = package / 'pocketjs-app.elf'
        app.write_text('''#!/bin/sh
set -eu
test "${POCKETJS_WORKSHOP:-}" = 1
test "${POCKETJS_VOICE:-}" = 1
test "$POCKETJS_DATA" = "$SHARED_USERDATA_PATH/ai-game-workshop"
test "$POCKETJS_ROMS" = "$SDCARD_PATH/Roms/AI生成游戏 (P8)"
test "$POCKETJS_HARDWARE" = 0
test "$1" = --dummy-video
test "$2" = --workshop-ready
echo 'PASS: actual launcher provides the native workshop boot environment'
exit "${FIXTURE_EXIT:-0}"
''')
        app.chmod(0o755)
        launcher = package / 'launch.sh'
        launcher.chmod(0o755)
        env = {**os.environ, 'SDCARD_PATH': str(sd), 'PLATFORM': 'tg5040',
               'SYSTEM_PATH': str(sd / '.system/tg5040'),
               'USERDATA_PATH': str(sd / '.userdata/tg5040'),
               'SHARED_USERDATA_PATH': str(data.parent),
               'LOGS_PATH': str(sd / 'logs')}
        for key in list(env):
            if key.startswith('POCKETJS_'):
                del env[key]
        result = subprocess.run(['sh', str(launcher), '--check'], env=env,
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        assert request.read_text() == 'game-existing-request'
        print(result.stdout.strip())
        result = subprocess.run(['sh', str(launcher), '--check'], env={**env, 'FIXTURE_EXIT': '17'},
                                capture_output=True, text=True)
        assert result.returncode == 17, result.stdout + result.stderr
        assert request.read_text() == 'game-existing-request'
        print('PASS: startup failures propagate; headless checks preserve pending game requests')


if __name__ == '__main__':
    verify()
