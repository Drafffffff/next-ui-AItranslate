#!/usr/bin/env python3
"""Launch two local gpSP windows with private save copies and a jitter proxy."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/gba-link'


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('--save-host', type=Path, required=True)
    parser.add_argument('--save-join', type=Path, required=True)
    parser.add_argument('--headless', action='store_true')
    args = parser.parse_args()
    for path in (args.rom, args.save_host, args.save_join):
        if not path.is_file() or not path.stat().st_size:
            parser.error('Use an existing, nonempty ROM and initialized SRAM copies: ' + str(path))
    lock = json.loads((ROOT / 'ports/gba-link/versions.json').read_text())
    core = OUT / 'mac-source' / ('gpsp-' + lock['gpsp_commit']) / 'gpsp_libretro.dylib'
    runner = OUT / 'core-runner-window'
    if not core.is_file() or not runner.is_file():
        parser.error('First run: python3 ports/gba-link/build.py mac')
    folder = OUT / ('local-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    folder.mkdir(parents=True)
    control = folder / 'jitter.txt'
    control.write_text('0\n')
    host_port, proxy_port = free_port(), free_port()
    while proxy_port == host_port:
        proxy_port = free_port()
    processes, handles = [], []
    try:
        handle = open(folder / 'proxy.log', 'w'); handles.append(handle)
        proxy = subprocess.Popen(['python3', str(ROOT / 'ports/gba-link/tests/jitter-proxy.py'),
            '--port', str(proxy_port), '--target-port', str(host_port), '--control', str(control)],
            stdout=handle, stderr=subprocess.STDOUT)
        processes.append(proxy)
        time.sleep(.2)
        if proxy.poll() is not None:
            raise RuntimeError('Proxy failed; inspect ' + str(folder / 'proxy.log'))
        games = []
        for role, source in (('host', args.save_host), ('join', args.save_join)):
            save = folder / (role + '.sav'); shutil.copy2(source, save)
            button_control = folder / (role + '.control'); button_control.write_text('0 0 0\n')
            env = dict(os.environ, NP_GAME='1', NP_REALTIME='1', NP_SAVE=str(save),
                NP_CONTROL=str(button_control), NP_INPUT_LOG=str(folder / (role + '-input.txt')),
                NP_CAPTURE=str(folder / (role + '.bmp')), NP_CAPTURE_EVERY='30', NP_LIVE_CAPTURE='1',
                NP_STOP_FILE=str(folder / (role + '.stop')))
            # Do not inherit experiment-specific scripts or speed overrides.
            for key in ('NP_SCRIPT', 'NP_FAST', 'NP_FAST_UNTIL', 'NP_ROOM', 'NP_WINDOW'):
                env.pop(key, None)
            if not args.headless:
                env['NP_WINDOW'] = '1'
            handle = open(folder / (role + '.log'), 'w'); handles.append(handle)
            game = subprocess.Popen([str(runner), str(core), str(args.rom.resolve()), role,
                '127.0.0.1', str(host_port if role == 'host' else proxy_port), 'auto', '60000'],
                env=env, stdout=handle, stderr=subprocess.STDOUT)
            processes.append(game); games.append(game)
            if role == 'host':
                deadline = time.monotonic() + 10
                while 'state=1' not in (folder / 'host.log').read_text():
                    if game.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('Host did not start; inspect ' + str(folder / 'host.log'))
                    time.sleep(.05)
        print('Two independent gpSP instances. Logs, save copies and controls:', folder, flush=True)
        print('X=A, Z=B, Return=Start; write 150 to jitter.txt for periodic 150ms holds, 0 to stop.', flush=True)
        while all(game.poll() is None for game in games):
            time.sleep(.25)
    except KeyboardInterrupt:
        pass
    finally:
        for role in ('host', 'join'):
            (folder / (role + '.stop')).touch()
        for process in processes[1:]:
            try:
                process.wait(timeout=6)
            except subprocess.TimeoutExpired:
                process.terminate(); process.wait(timeout=5)
        if processes:
            processes[0].terminate(); processes[0].wait(timeout=5)
        for handle in handles:
            handle.close()


if __name__ == '__main__':
    main()
