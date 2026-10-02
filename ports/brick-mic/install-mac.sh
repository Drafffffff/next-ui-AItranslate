#!/bin/bash
set -euo pipefail
TASK_ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
exec python3 - "$TASK_ROOT" "$@" <<'PY'
import argparse
import ctypes
import datetime
import fcntl
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import signal
import subprocess
import sys
import time
import uuid

parser = argparse.ArgumentParser(description="Install and reopen the built Brick Mic application.")
parser.add_argument("task_root")
parser.add_argument("--show-permissions", action="store_true", help="Open the installed application's permission guide.")
parser.add_argument("--dry-run", action="store_true", help="Validate and report without changing files or processes.")
args = parser.parse_args()

BUNDLE_ID = "com.nextui.brickmic"
EXECUTABLE = "BrickMic"
source = Path(args.task_root) / "build/brick-mic/Brick Mic.app"
signing_helper = Path(args.task_root) / "ports/brick-mic/mac-signing.py"
installed = Path("/Applications/Brick Mic.app")
support = Path.home() / "Library/Application Support/Brick Mic"


def bundle_info(app):
    if app.is_symlink() or not app.is_dir():
        raise RuntimeError(f"Expected an application directory: {app}")
    with (app / "Contents/Info.plist").open("rb") as handle:
        info = plistlib.load(handle)
    if info.get("CFBundleIdentifier") != BUNDLE_ID or info.get("CFBundleExecutable") != EXECUTABLE:
        raise RuntimeError(f"Refusing to replace or launch a different application: {app}")
    version = info.get("CFBundleShortVersionString")
    build = info.get("CFBundleVersion")
    if not isinstance(version, str) or not version.strip() or not isinstance(build, str) or not build.strip():
        raise RuntimeError(f"Application version is missing: {app}")
    binary = app / "Contents/MacOS" / EXECUTABLE
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError(f"Application executable is missing or not executable: {binary}")
    return info


def verify_signature(app):
    # Independently invoked installers apply the same certificate pin to the
    # build, stage and final bundle, before stopping any current application.
    result = subprocess.run([sys.executable, str(signing_helper), "verify", str(app)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"Code signature verification failed for {app}: {result.stderr.strip()}")


def manifest(app):
    digest = hashlib.sha256()
    for path in sorted(app.rglob("*")):
        relative = path.relative_to(app).as_posix().encode()
        if path.is_symlink():
            digest.update(b"L\0" + relative + b"\0" + os.readlink(path).encode() + b"\0")
        elif path.is_file():
            digest.update(b"F\0" + relative + b"\0")
            digest.update(str(path.stat().st_mode & 0o777).encode() + b"\0")
            with path.open("rb") as handle:
                while chunk := handle.read(1024 * 1024):
                    digest.update(chunk)
            digest.update(b"\0")
        elif path.is_dir():
            digest.update(b"D\0" + relative + b"\0")
    return digest.hexdigest()


libproc = ctypes.CDLL("/usr/lib/libproc.dylib")
libproc.proc_pidpath.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
libproc.proc_pidpath.restype = ctypes.c_int


def executable_path(pid):
    buffer = ctypes.create_string_buffer(4096)
    if libproc.proc_pidpath(pid, buffer, len(buffer)) <= 0:
        return None
    return Path(os.fsdecode(buffer.value))


def brick_mic_processes():
    # Read the kernel's executable path; do not match arbitrary command text or
    # terminate another application's process just because it has a similar name.
    result = subprocess.run(["/bin/ps", "-axo", "pid=,uid="], check=True, capture_output=True, text=True)
    matches = {}
    for row in result.stdout.splitlines():
        values = row.split()
        if len(values) != 2 or int(values[1]) != os.getuid():
            continue
        pid = int(values[0])
        binary = executable_path(pid)
        if binary is None or binary.name != EXECUTABLE or binary.parent.name != "MacOS" or binary.parent.parent.name != "Contents":
            continue
        app = binary.parent.parent.parent
        if app.suffix != ".app":
            continue
        try:
            bundle_info(app)
            if binary.resolve() != (app / "Contents/MacOS" / EXECUTABLE).resolve():
                continue
        except (OSError, ValueError, plistlib.InvalidFileException, RuntimeError):
            continue
        matches[pid] = binary
    return matches


def stop_copies():
    processes = brick_mic_processes()
    for pid, path in processes.items():
        if executable_path(pid) != path:
            continue
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        remaining = brick_mic_processes()
        if not remaining:
            return
        time.sleep(0.1)
    raise RuntimeError("Brick Mic did not exit within 10 seconds. Close it before retrying; no process was force-killed.")


def open_installed():
    command = ["/usr/bin/open", str(installed)]
    if args.show_permissions:
        command += ["--args", "--show-permissions"]
    subprocess.run(command, check=True)


def main():
    info = bundle_info(source)
    verify_signature(source)
    expected = manifest(source)
    if installed.exists() or installed.is_symlink():
        bundle_info(installed)
    if args.dry_run:
        print(f"Validated Brick Mic {info['CFBundleShortVersionString']} ({info['CFBundleVersion']}): {source}")
        print(f"Install target: {installed}; matching running copies: {len(brick_mic_processes())}")
        return

    support.mkdir(parents=True, exist_ok=True)
    with (support / "install.lock").open("a") as lock:
        os.chmod(lock.name, 0o600)
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError("Another Brick Mic installation is already running.")

        token = uuid.uuid4().hex
        stage = installed.parent / f".Brick Mic.install-{token}.app"
        previous = installed.parent / f".Brick Mic.previous-{token}.app"
        backup = None
        replaced = False
        stopped = False
        try:
            shutil.copytree(source, stage, symlinks=True)
            bundle_info(stage)
            verify_signature(stage)
            if manifest(stage) != expected:
                raise RuntimeError("Staged application does not match the build; installation was not started.")
            if installed.exists():
                stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
                backup = support / "backups" / f"{stamp}-mac-{token[:8]}" / "Mac/Brick Mic.app"
                backup.parent.mkdir(parents=True)
                old_hash = manifest(installed)
                shutil.copytree(installed, backup, symlinks=True)
                if manifest(backup) != old_hash:
                    raise RuntimeError("Backup verification failed; the installed application was not changed.")
            stop_copies()
            stopped = True
            if installed.exists():
                os.rename(installed, previous)
            os.rename(stage, installed)
            replaced = True
            bundle_info(installed)
            verify_signature(installed)
            if manifest(installed) != expected:
                raise RuntimeError("Installed application does not match the build.")
        except Exception:
            if previous.exists():
                if installed.exists():
                    shutil.rmtree(installed)
                os.rename(previous, installed)
                if backup is not None and manifest(installed) != manifest(backup):
                    raise RuntimeError(f"Rollback verification failed. The preserved backup is at {backup}")
            elif replaced and installed.exists():
                shutil.rmtree(installed)
            if stopped and installed.exists():
                open_installed()
            raise
        finally:
            if stage.exists():
                shutil.rmtree(stage)

        if previous.exists():
            shutil.rmtree(previous)
        open_installed()
        print(f"Installed and opened Brick Mic {info['CFBundleShortVersionString']} ({info['CFBundleVersion']}): {installed}")
        if backup is not None:
            print(f"Previous version backed up: {backup}")


try:
    main()
except Exception as error:
    print(f"Brick Mic installation failed: {error}", file=sys.stderr)
    sys.exit(1)
PY
