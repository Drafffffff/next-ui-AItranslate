#!/usr/bin/env python3
"""Compile and exercise the actual RFU adapter with ASan and UBSan on Mac."""
import importlib.util
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('gba_build', ROOT / 'ports/gba-link/build.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
core = build.source('mac')
binary = ROOT / 'build/gba-link/rfu-queue-test'
subprocess.run(['clang', '-std=c99', '-O1', '-DINLINE=inline',
                '-fsanitize=address,undefined',
                '-I' + str(core / 'libretro/libretro-common/include'),
                '-DRFU_SOURCE="' + str(core / 'rfu.c') + '"',
                str(ROOT / 'ports/gba-link/tests/rfu-queue.c'), '-o', str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True)
(ROOT / 'build/gba-link/rfu-queue-after.log').write_text(result.stdout + result.stderr)
print(result.stdout, end='')
print(result.stderr, end='')
raise SystemExit(result.returncode)
