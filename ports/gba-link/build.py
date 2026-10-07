#!/usr/bin/env python3
"""Rebuild the pinned gpSP core and NextUI front end, without a handheld."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'build/gba-link'
LOCK = json.loads((ROOT / 'ports/gba-link/versions.json').read_text())


def run(argv, cwd=ROOT):
    print('+', ' '.join(map(str, argv)), flush=True)
    subprocess.run([str(x) for x in argv], cwd=cwd, check=True)


def source(platform):
    archive = OUTPUT / 'gpsp-source.tar.gz'
    OUTPUT.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        run(['curl', '--fail', '--location', '--max-time', '120',
             f"https://codeload.github.com/libretro/gpsp/tar.gz/{LOCK['gpsp_commit']}", '-o', archive])
    if hashlib.sha256(archive.read_bytes()).hexdigest() != LOCK['gpsp_archive_sha256']:
        raise SystemExit('gpSP source checksum mismatch')
    destination = OUTPUT / (platform + '-source')
    core = destination / ('gpsp-' + LOCK['gpsp_commit'])
    if not core.exists():
        destination.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            # Archive is pinned and checksum verified. Refuse escaping entries.
            for entry in tar.getmembers():
                if entry.name.startswith('/') or '..' in Path(entry.name).parts:
                    raise SystemExit('Invalid source archive entry')
            tar.extractall(destination)
    patch = ROOT / 'ports/gba-link/patches/gpsp-rfu-queue.patch'
    if hashlib.sha256(patch.read_bytes()).hexdigest() != LOCK['gpsp_rfu_patch_sha256']:
        raise SystemExit('gpSP RFU patch checksum mismatch')
    # Accept only the pinned upstream bytes or our exact reviewed RFU patch.
    # Unknown local changes must never silently enter an update package.
    with tarfile.open(archive) as tar:
        for entry in tar.getmembers():
            if entry.isfile():
                cached = destination / entry.name
                if not cached.is_file():
                    raise SystemExit('Cached gpSP source is missing: ' + entry.name)
                actual = hashlib.sha256(cached.read_bytes()).hexdigest()
                original = hashlib.sha256(tar.extractfile(entry).read()).hexdigest()
                patched = LOCK['gpsp_rfu_patched_sha256'] if cached == core / 'rfu.c' else None
                if actual not in (original, patched):
                    raise SystemExit('Cached gpSP source was modified: ' + entry.name)
    if hashlib.sha256((core / 'rfu.c').read_bytes()).hexdigest() != LOCK['gpsp_rfu_patched_sha256']:
        run(['patch', '--batch', '--forward', '-p1', '-i', patch], core)
    if hashlib.sha256((core / 'rfu.c').read_bytes()).hexdigest() != LOCK['gpsp_rfu_patched_sha256']:
        raise SystemExit('Patched gpSP RFU source checksum mismatch')
    return core


def dependency_check():
    wrappers = [('rcheevos', 'rcheevos.makefile'), ('libchdr', 'libchdr.makefile')]
    for name, wrapper in wrappers:
        path = ROOT / 'workspace/all/minarch' / name
        if name == 'rcheevos': path.mkdir(parents=True, exist_ok=True)
        if name == 'rcheevos' and not (path / 'makefile').exists():
            shutil.copy2(ROOT / 'workspace/all/minarch' / wrapper, path / 'makefile')
    deps = [('libretro-common', 'libretro_common_commit'),
            ('libchdr', 'libchdr_commit'), ('rcheevos/src', 'rcheevos_commit')]
    for name, key in deps:
        path = ROOT / 'workspace/all/minarch' / name
        if not path.exists():
            run(['git', 'clone', 'https://github.com/' + {
                'libretro-common': 'libretro/libretro-common', 'libchdr': 'rtissera/libchdr',
                'rcheevos/src': 'RetroAchievements/rcheevos'}[name], path])
            run(['git', 'checkout', LOCK[key]], path)
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=path, text=True).strip()
        if name == 'libchdr' and not (path / 'makefile').exists():
            shutil.copy2(ROOT / 'workspace/all/minarch/libchdr.makefile', path / 'makefile')
        if actual != LOCK[key]:
            raise SystemExit(f'{name}: expected {LOCK[key]}, found {actual}; preserve local changes and inspect before rebuilding')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=['mac', 'brick'])
    parser.add_argument('--revision', help='Full manifest revision for exported source without Git metadata')
    args = parser.parse_args()
    if args.revision and (len(args.revision) != 40 or any(c not in '0123456789abcdef' for c in args.revision)):
        parser.error('--revision must be 40 lower-case hexadecimal characters')
    dependency_check()
    core = source(args.target)
    if args.target == 'mac':
        run(['make', 'clean'], core)
        run(['make', 'platform=osx', 'HAVE_DYNAREC=0', 'CC=clang', 'CXX=clang++',
             'GIT_VERSION=' + LOCK['gpsp_commit'][:7], '-j4'], core)
        run(['python3', ROOT / 'ports/gba-link/tests/make-probe.py', OUTPUT / 'link-probe.gba'])
        openssl = Path(subprocess.check_output(['brew', '--prefix', 'openssl@3'], text=True).strip())
        runner_args = ['clang', '-std=c99', '-Wall', '-Wextra', '-Werror',
             '-I' + str(ROOT / 'workspace/all/minarch'),
             '-I' + str(ROOT / 'workspace/all/minarch/libretro-common/include'),
             '-I' + str(openssl / 'include'), ROOT / 'ports/gba-link/tests/core-runner.c',
             ROOT / 'workspace/all/minarch/ma_netplay.c', '-L' + str(openssl / 'lib'),
             '-lcrypto', '-o', OUTPUT / 'core-runner']
        run(runner_args)
        if shutil.which('sdl2-config'):
            import shlex
            sdl_flags = shlex.split(subprocess.check_output(['sdl2-config', '--cflags', '--libs'], text=True))
            run(runner_args[:-1] + [OUTPUT / 'core-runner-window', '-DNP_SDL'] + sdl_flags)
    else:
        revision = (args.revision[:7] + '\n').encode() if args.revision else subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT)
        (ROOT / 'workspace/hash.txt').write_bytes(revision)
        # Root mounted at a constant path; use argument arrays to preserve spaces.
        script = ('set -e; cd /work/' + str(core.relative_to(ROOT)) + '; '
                  'make clean; make platform=arm64 CC=${CROSS_COMPILE}gcc '
                  'CXX=${CROSS_COMPILE}g++ AR=${CROSS_COMPILE}ar '
                  'GIT_VERSION=' + LOCK['gpsp_commit'][:7] + ' -j4; '
                  'cd /work/workspace/all/minarch; make PLATFORM=tg5040')
        run(['docker', 'run', '--rm', '--platform', 'linux/arm64', '-v', str(ROOT) + ':/work',
             LOCK['toolchain'], '/bin/bash', '-lc', script])
        target = OUTPUT / 'brick'
        target.mkdir(exist_ok=True)
        shutil.copy2(core / 'gpsp_libretro.so', target / 'gpsp_libretro.so')
        shutil.copy2(ROOT / 'workspace/all/minarch/build/tg5040/minarch.elf', target / 'minarch.elf')
    print('Built', args.target, 'in', OUTPUT)


if __name__ == '__main__':
    main()
