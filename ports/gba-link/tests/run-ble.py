#!/usr/bin/env python3
"""Run owned BLE probes without changing installed software or game state."""
import argparse
import hashlib
import tempfile
import json
from pathlib import Path
import re
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]


def measured_metrics(log, duplex=False):
    metrics = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)(?=\s|$)', log)}
    # Teardown summaries must not overwrite the completed measurement's
    # message count. Keep setup MTU/timing metadata, then prefer the PASS line.
    prefix = 'DUPLEX_PASS' if duplex else 'PASS exchanges='
    for line in log.splitlines():
        if line.startswith(prefix):
            metrics.update({key: int(value) for key, value in re.findall(r'(\w+)=(\d+)(?=\s|$)', line)})
    return metrics

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--join', required=True)
    parser.add_argument('--peer-bt', required=True)
    parser.add_argument('--backend', choices=['bluez', 'raw-att'], default='bluez')
    parser.add_argument('--duplex', action='store_true', help='Independent opaque messages in both directions; not echo')
    parser.add_argument('--binary', type=Path, help='Explicit diagnostic ELF, snapshotted and hash verified before upload')
    parser.add_argument('--server-fd', action='store_true', help='Single-session host FD diagnostic')
    parser.add_argument('--event-length', type=int, default=0, choices=range(13), help='Informational controller hint in 625us units')
    parser.add_argument('--window', type=int, default=1, choices=range(1, 17), help='Bounded diagnostic pending messages')
    parser.add_argument('--known-hosts', required=True)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--count', type=int, default=100)
    parser.add_argument('--bytes', type=int, default=104)
    parser.add_argument('--duration', type=int, default=60, help='Maximum seconds')
    parser.add_argument('--delay-ms', type=int, default=0)
    parser.add_argument('--run-for', type=int, default=0, help='Minimum measured seconds; requires a sufficient count limit')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.duplex and (args.backend != 'raw-att' or args.window != 1 or args.run_for or args.delay_ms or args.server_fd):
        parser.error('Duplex benchmark uses raw ATT and its own bounded window; FD echo/pacing flags are not supported')
    if args.window > 1 and (not args.server_fd or args.window * args.bytes > 262144):
        parser.error('Window probe needs server FD and bounded bytes')
    if args.run_for < 0 or args.run_for > 1200 or (args.run_for and args.duration < args.run_for + 30):
        parser.error('Measurement duration must leave at least 30 seconds for setup')
    if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', args.peer_bt):
        parser.error('Invalid Bluetooth address')
    if not (10 <= args.count <= 100000 and 8 <= args.bytes <= 65536 and 15 <= args.duration <= 1800 and 0 <= args.delay_ms <= 1000):
        parser.error('Invalid test bounds')
    args.output.mkdir(parents=True, exist_ok=True)
    args.stage = 'preflight'
    args.created_logs = False
    try:
        run_test(args)
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        record = dict(transport='BLE ' + args.backend, passed=False,
                      failure_stage=args.stage, error=str(error), completed_at=int(time.time()))
        for name in ('host', 'join'):
            log = args.output / (name + '.log')
            if args.created_logs and log.exists():
                record[name + '_log'] = log.read_text()
        if hasattr(args, 'probe_sha256'):
            record['probe_sha256'] = args.probe_sha256
        (args.output / 'results.json').write_text(json.dumps(record, indent=2) + '\n')
        print(json.dumps(record, indent=2), flush=True)
        raise SystemExit(1)


def run_test(args):
    binary = args.binary or ROOT / 'build/gba-link/bluetooth/ble-probe.elf'
    artifact = binary.read_bytes()
    artifact_hash = hashlib.sha256(artifact).hexdigest()
    args.probe_sha256 = artifact_hash
    base = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o',
            'ServerAliveInterval=5', '-o', 'ServerAliveCountMax=2', '-o',
            'StrictHostKeyChecking=yes', '-o', 'UserKnownHostsFile=' + args.known_hosts,
            '-i', args.identity]
    peers = [base + [args.host], base + [args.join]]
    game_pids = []
    for peer in peers:
        subprocess.run(peer + ['! pidof ble-probe.elf >/dev/null && ! pidof probe.elf >/dev/null'], check=True, timeout=30)
        game_pids.append(subprocess.check_output(peer + ['pidof minarch.elf || true'], text=True, timeout=30).strip())
        sockets = subprocess.check_output(peer + ['cat /proc/net/tcp /proc/net/tcp6'], text=True, timeout=30)
        for line in sockets.splitlines():
            fields = line.split()
            if len(fields) > 3 and fields[3] in ('01', '0A'):
                if any(int(endpoint.rsplit(':', 1)[1], 16) == 55435 for endpoint in fields[1:3]):
                    raise RuntimeError('Production GBA Link socket is active; do not compete with gameplay')
    args.stage = 'upload'
    for peer in peers:
        with tempfile.TemporaryFile() as source:
            source.write(artifact)
            source.seek(0)
            subprocess.run(peer + ['mkdir -p /tmp/nextui-gba-bluetooth && cat > /tmp/nextui-gba-bluetooth/ble-probe.elf && chmod 700 /tmp/nextui-gba-bluetooth/ble-probe.elf'], stdin=source, check=True, timeout=60)
        installed_hash = subprocess.check_output(peer + ['sha256sum /tmp/nextui-gba-bluetooth/ble-probe.elf'], text=True, timeout=30).split()[0]
        if installed_hash != artifact_hash:
            raise RuntimeError('Uploaded probe hash mismatch')
    args.stage = 'exchange'
    with (args.output / 'host.log').open('w') as host_log, (args.output / 'join.log').open('w') as join_log:
        args.created_logs = True
        # The SSH shell retains ownership of its child. A line on stdin ends the
        # probe gracefully; HUP/exit also cleans up this child only.
        host_role = 'duplex-host' if args.duplex else 'host'
        host_command = ('/tmp/nextui-gba-bluetooth/ble-probe.elf -role ' + host_role + ' '
                        + (f'-count {args.count} -bytes {args.bytes} ' if args.duplex else '') +
                        f'-duration {args.duration + 15}s ' + ('-server-fd ' if args.server_fd else '') + '& child=$!; '
                        'trap \'kill -TERM "$child" 2>/dev/null || true\' EXIT HUP INT TERM; '
                        'IFS= read -r finish || true; kill -TERM "$child" 2>/dev/null || true; wait "$child"')
        host = subprocess.Popen(peers[0] + [host_command], stdin=subprocess.PIPE, stdout=host_log, stderr=subprocess.STDOUT)
        join = None
        try:
            time.sleep(1)
            role = 'join-raw' if args.backend == 'raw-att' else 'join'
            if args.duplex:
                role = 'duplex-join'
            command = ('/tmp/nextui-gba-bluetooth/ble-probe.elf -role ' + role + ' '
                       f'-peer {shlex.quote(args.peer_bt)} -duration {args.duration}s '
                       f'-count {args.count} -bytes {args.bytes} -delay {args.delay_ms}ms')
            if args.run_for:
                command += f' -run-for {args.run_for}s'
            if args.event_length:
                command += f' -event-length {args.event_length}'
            if args.window > 1:
                command += f' -window {args.window}'
            join = subprocess.Popen(peers[1] + [command], stdout=join_log, stderr=subprocess.STDOUT)
            join_code = join.wait(timeout=args.duration + 20)
        finally:
            if join is not None and join.poll() is None:
                join.terminate()
                join.wait(timeout=15)
            if host.stdin:
                try:
                    host.stdin.write(b'STOP\n')
                    host.stdin.flush()
                except BrokenPipeError:
                    pass
                host.stdin.close()
            host_code = host.wait(timeout=30)
    content = (args.output / 'join.log').read_text()
    host_content = (args.output / 'host.log').read_text()
    transfer_passed = ('DUPLEX_PASS messages=' in content and 'DUPLEX_PASS messages=' in host_content) if args.duplex else 'PASS exchanges=' in content
    metrics = measured_metrics(content, args.duplex)
    host_metrics = measured_metrics(host_content, args.duplex)
    if args.duplex:
        transfer_passed = transfer_passed and all(m.get('messages') == args.count and
            m.get('bytes_each_direction') == args.count * args.bytes and
            m.get('final_queued_bytes') == 0 for m in (metrics, host_metrics))
    record = dict(transport='BLE ' + args.backend, duplex=args.duplex, server_fd=args.server_fd or args.duplex, return_codes=[host_code, join_code],
                  passed=host_code == join_code == 0 and transfer_passed,
                  metrics=metrics, host_metrics=host_metrics,
                  sample_kind='peer arrival since independent local batch submission; not echo RTT' if args.duplex else 'echo completion RTT including pending queue',
                  host_log=host_content, join_log=content, game_pids=game_pids,
                  completed_at=int(time.time()), probe_sha256=artifact_hash)
    (args.output / 'results.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2), flush=True)
    if not record['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
