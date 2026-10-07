#!/usr/bin/env python3
"""Check real IME I/O and IRQ delivery; optional old-core negative control."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/gba-link'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arm', action='store_true')
parser.add_argument('--expect-fail', action='store_true')
parser.add_argument('--core', type=Path)
args = parser.parse_args()
core = (args.core or (OUT / 'brick/gpsp_libretro.so' if args.arm else
        next((OUT / 'mac-source').glob('gpsp-*')) / 'gpsp_libretro.dylib')).resolve()
subprocess.run([sys.executable, str(ROOT / 'ports/gba-link/tests/make-ime-probe.py')], cwd=ROOT, check=True)
source = ROOT / 'ports/gba-link/tests/ime-probe.c'
include = ROOT / 'workspace/all/minarch/libretro-common/include'
binary = OUT / ('ime-probe-arm64.elf' if args.arm else 'ime-probe-mac')
results = []
if args.arm:
    lock = json.loads((ROOT / 'ports/gba-link/versions.json').read_text())
    remote = lambda p: '/work/' + str(p.relative_to(ROOT))
    compile_script = '${CROSS_COMPILE}gcc -std=c99 -Wall -Wextra -Werror -I' + shlex.quote(remote(include)) + ' ' + shlex.quote(remote(source)) + ' -ldl -o ' + shlex.quote(remote(binary))
    container = ['docker', 'run', '--rm', '--platform', 'linux/arm64', '-v', str(ROOT)+':/work', lock['toolchain'], 'bash', '-lc']
    subprocess.run(container + [compile_script], check=True)
else:
    subprocess.run(['clang', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I'+str(include), str(source), '-o', str(binary)], check=True)
for jit in ((False, True) if args.arm else (False,)):
    if args.arm:
        sdk = '/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc'
        cmd = ('NP_DYNAREC=1 ' if jit else '') + shlex.join([sdk+'/lib/ld-2.28.so', '--library-path', sdk+'/lib:'+sdk+'/usr/lib', remote(binary), remote(core), remote(OUT/'ime-probe.gba')])
        proc = subprocess.run(container+[cmd], capture_output=True, text=True)
    else:
        proc = subprocess.run([str(binary), str(core), str(OUT/'ime-probe.gba')], capture_output=True, text=True, cwd=ROOT)
    print(proc.stdout, end=''); print(proc.stderr, end='', file=sys.stderr)
    expected = 1 if args.expect_fail else 0
    assert proc.returncode == expected, proc
    assert ('reason=1' if args.expect_fail else 'result=50415353') in proc.stdout, proc.stdout
    results.append({'path': 'ARM64 JIT' if jit else 'interpreter', 'exit_code': proc.returncode, 'output': proc.stdout.strip()})
record = {'core_sha256': hashlib.sha256(core.read_bytes()).hexdigest(), 'negative_control': args.expect_fail, 'platform': 'arm64-sdk' if args.arm else 'mac', 'results': results, 'expectations_verified': True}
name = 'ime-' + ('before' if args.expect_fail else 'after') + '-' + ('arm' if args.arm else 'mac') + '.json'
(OUT/name).write_text(json.dumps(record, indent=2)+'\n')
print('PASS: IME register and actual timer IRQ expectations verified')
