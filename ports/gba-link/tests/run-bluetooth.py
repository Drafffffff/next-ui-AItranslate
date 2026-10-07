#!/usr/bin/env python3
"""Opt-in, isolated two-Brick Bluetooth measurements; never launch games."""
import argparse
import hashlib
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
    parser.add_argument('--transport', choices=['bluetooth', 'tcp'], default='bluetooth')
    parser.add_argument('--host-ip', help='IPv4 host address for TCP comparison')
    parser.add_argument('--allow-running-game', action='store_true', help='Keep an offline game open to prevent sleep; record the load context')
    parser.add_argument('--known-hosts', required=True)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/gba-link/bluetooth/matrix')
    args = parser.parse_args()
    if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', args.host_bt):
        parser.error('Invalid Bluetooth address')
    if args.transport == 'tcp':
        import ipaddress
        try:
            ipaddress.IPv4Address(args.host_ip)
        except (ValueError, TypeError):
            parser.error('--host-ip must be an IPv4 address for TCP comparison')
    args.output.mkdir(parents=True, exist_ok=True)
    base = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o',
            'StrictHostKeyChecking=yes', '-o', 'UserKnownHostsFile=' + args.known_hosts,
            '-i', args.identity]
    peers = [base + [args.host], base + [args.join]]
    binary = ROOT / 'build/gba-link/bluetooth/probe.elf'
    game_pids = []
    for peer in peers:
        # An offline game may keep the handheld awake. Never run alongside a
        # production link or an earlier probe. No service/game manipulation.
        subprocess.run(peer + ['! pidof probe.elf >/dev/null'], check=True)
        pid = subprocess.check_output(peer + ['pidof minarch.elf || true'], text=True).strip()
        if pid and not args.allow_running_game:
            raise SystemExit('Game is open; use --allow-running-game only for an offline game')
        game_pids.append(pid)
        sockets = subprocess.check_output(peer + ['cat /proc/net/tcp /proc/net/tcp6'], text=True)
        for line in sockets.splitlines():
            fields = line.split()
            if len(fields) > 3 and fields[3] in ('01', '0A'):
                if any(int(endpoint.rsplit(':', 1)[1], 16) == 55435 for endpoint in fields[1:3]):
                    raise SystemExit('Production GBA Link socket is active; leave the game open but disconnect its link first')
        with binary.open('rb') as source:
            subprocess.run(peer + ['mkdir -p /tmp/nextui-gba-bluetooth && cat > /tmp/nextui-gba-bluetooth/probe.elf && chmod 700 /tmp/nextui-gba-bluetooth/probe.elf'], stdin=source, check=True)
    results = []
    for length, delay in [(32, 0), (104, 0), (104, 16667), (256, 0)]:
        label = f'{args.transport}-{length}bytes-{delay}us'
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
                           + ('NP_TCP=1 ' if args.transport == 'tcp' else '') +
                           f'NP_BT_COUNT=100 NP_BT_BYTES={length} NP_BT_DELAY_US={delay} '
                           '/tmp/nextui-gba-bluetooth/probe.elf ' + role +
                           (' ' + shlex.quote(args.host_ip if args.transport == 'tcp' else args.host_bt) if index else ''))
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
        join_content = (args.output / (label + '-join.log')).read_text()
        metrics = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', content)}
        results.append(dict(test=label, return_codes=codes, metrics=metrics,
                            host_log=content, join_log=join_content,
                            probe_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                            game_pids_at_start=game_pids,
                            completed_at=int(time.time())))
        (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(label, content.strip(), flush=True)
        if codes != [0, 0]:
            raise SystemExit('Probe failed; inspect both logs')


if __name__ == '__main__':
    main()
