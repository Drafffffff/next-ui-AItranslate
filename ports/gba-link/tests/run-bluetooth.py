#!/usr/bin/env python3
"""Opt-in, isolated two-Brick Bluetooth measurements; never launch games."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='SSH alias/user@address')
    parser.add_argument('--join', required=True, help='SSH alias/user@address')
    parser.add_argument('--host-bt', required=True, help='Host Bluetooth MAC')
    parser.add_argument('--known-hosts', required=True)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/gba-link/bluetooth/matrix')
    args = parser.parse_args()
    if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', args.host_bt):
        parser.error('Invalid Bluetooth address')
    args.output.mkdir(parents=True, exist_ok=True)
    base = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o',
            'StrictHostKeyChecking=yes', '-o', 'UserKnownHostsFile=' + args.known_hosts,
            '-i', args.identity]
    peers = [base + [args.host], base + [args.join]]
    binary = ROOT / 'build/gba-link/bluetooth/probe.elf'
    for peer in peers:
        # Do not compete with a game or an earlier probe. No service restarts.
        subprocess.run(peer + ['! pidof minarch.elf >/dev/null && ! pidof probe.elf >/dev/null'], check=True)
        with binary.open('rb') as source:
            subprocess.run(peer + ['mkdir -p /tmp/nextui-gba-bluetooth && cat > /tmp/nextui-gba-bluetooth/probe.elf && chmod 700 /tmp/nextui-gba-bluetooth/probe.elf'], stdin=source, check=True)
    results = []
    for length, delay in [(32, 0), (104, 0), (104, 16667), (256, 0)]:
        label = f'ertm-{length}bytes-{delay}us'
        children, files = [], []
        try:
            for index, peer in enumerate(peers):
                role = 'join' if index else 'host'
                log = (args.output / (label + '-' + role + '.log')).open('w')
                files.append(log)
                # Capture/restore the actual initial state, including failures.
                command = ('set -e; previous=$(iw dev wlan0 get power_save); '
                           'case "$previous" in *off*) restore=off;; *on*) restore=on;; *) exit 4;; esac; '
                           'trap \'iw dev wlan0 set power_save "$restore"\' EXIT; '
                           'iw dev wlan0 set power_save off; '
                           f'NP_BT_COUNT=100 NP_BT_BYTES={length} NP_BT_DELAY_US={delay} '
                           '/tmp/nextui-gba-bluetooth/probe.elf ' + role +
                           (' ' + shlex.quote(args.host_bt) if index else ''))
                children.append(subprocess.Popen(peer + [command], stdout=log, stderr=subprocess.STDOUT))
                if not index:
                    time.sleep(.5)
            codes = [child.wait(timeout=60) for child in children]
        finally:
            for child in children:
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=15)
            for log in files:
                log.close()
        content = (args.output / (label + '-host.log')).read_text()
        metrics = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', content)}
        results.append(dict(test=label, return_codes=codes, metrics=metrics))
        (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(label, content.strip(), flush=True)
        if codes != [0, 0]:
            raise SystemExit('Probe failed; inspect both logs')


if __name__ == '__main__':
    main()
