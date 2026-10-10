#!/usr/bin/env python3
"""Launcher fixtures: no purchased binary, network, device display or saves."""
import hashlib
import os
from pathlib import Path
import platform as host_platform
import shutil
import signal
import subprocess
import tempfile
import time

SOURCE = Path(__file__).resolve().parent


def write_executable(path, body):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(body)
    path.chmod(0o755)


def snapshot(path):
    return [(str(item.relative_to(path)), hashlib.sha256(item.read_bytes()).hexdigest()
             if item.is_file() else None) for item in sorted(path.rglob("*"))]


with tempfile.TemporaryDirectory(prefix="pico8-launch-") as temporary:
    base = Path(temporary)
    sd = base / "SD card's root"
    platform = "fixture"
    tools = sd / "Tools" / platform / "PICO-8.pak"
    emu = sd / "Emus" / platform / "PICO.pak"
    runtime = emu / "pico8"
    p8 = sd / "Emus" / platform / "P8.pak" / "launch.sh"
    bios = sd / "BIOS folder's" / "PICO"
    shared = sd / "custom shared user's data"
    root_roms = sd / "Roms" / "Pico-8 (PICO)"
    root_roms.mkdir(parents=True)
    cart = sd / "Roms" / "AI生成游戏 (P8)" / "game's long name.p8"
    cart.parent.mkdir(parents=True)
    cart.write_text("pico-8 cartridge // fixture\n")
    tools.mkdir(parents=True)
    for name, destination in [("pico8-launch.sh", "launch.sh"),
                              ("pico8-config.txt", "config.txt"),
                              ("pico8-controllers.txt", "sdl_controllers.txt")]:
        shutil.copy2(SOURCE / name, tools / destination)
    (tools / "launch.sh").chmod(0o755)
    (tools / "sdl-nosensor.so").touch()
    write_executable(tools / "menu-exit.elf", """#!/bin/sh
printf '%s\\n' "$@" > "$FIXTURE_MENU"
exec "$@"
""")
    write_executable(runtime / "pico8_64", """#!/bin/sh
printf '%s\\n' "$@" > "$FIXTURE_ARGS"
printf '%s\\n' "$SDCARD_PATH" "$PLATFORM" "$SYSTEM_PATH" > "$FIXTURE_ENV"
exit "${FIXTURE_STATUS:-0}"
""")
    (runtime / "pico8.dat").write_text("fixture data")
    write_executable(tools / "sync-pico8-favourites.sh", """#!/bin/sh
printf '%s\\n' "$@" >> "$FIXTURE_SYNC"
""")
    write_executable(emu / "launch.sh", (SOURCE / "pico8-emu-launch.sh").read_text())
    write_executable(emu / "launch-vendor.sh", """#!/bin/sh
printf '%s\\n' "$@" > "$FIXTURE_VENDOR"
exit "${FIXTURE_VENDOR_STATUS:-0}"
""")
    env = os.environ.copy()
    # A developer shell's environment must not select a real device runtime.
    for key in ["LD_PRELOAD", "PICO8_RUNTIME_PATH", "PICO8_ROMS_PATH"]:
        env.pop(key, None)
    env.update(SDCARD_PATH=str(sd), PLATFORM=platform, BIOS_PATH=str(bios.parent),
               SYSTEM_PATH=str(sd / "custom system libs"), SHARED_USERDATA_PATH=str(shared),
               FIXTURE_ARGS=str(base / "args"), FIXTURE_MENU=str(base / "menu"),
               FIXTURE_SYNC=str(base / "sync"), FIXTURE_VENDOR=str(base / "vendor"),
               FIXTURE_ENV=str(base / "env"), FIXTURE_FAKE=str(base / "fake"))

    def run(script, *args, status=0, **overrides):
        result = subprocess.run([str(script), *map(str, args)], env={**env, **overrides},
                                capture_output=True, text=True, timeout=8)
        assert result.returncode == status, (script, args, result.returncode,
                                             result.stdout, result.stderr)
        return result

    def recorded(key):
        return Path(env[key]).read_text().splitlines()

    def clear_records():
        for key in ["FIXTURE_ARGS", "FIXTURE_MENU", "FIXTURE_SYNC", "FIXTURE_VENDOR", "FIXTURE_FAKE"]:
            Path(env[key]).unlink(missing_ok=True)

    # Probe is genuinely read-only, including when the runtime is incomplete.
    initial = snapshot(sd)
    run(tools / "launch.sh", "--check")
    assert snapshot(sd) == initial
    (tools / "sdl-nosensor.so").unlink()
    unsupported = snapshot(sd)
    run(tools / "launch.sh", "--check", status=1)
    assert snapshot(sd) == unsupported and not shared.exists()
    (tools / "sdl-nosensor.so").touch()
    (runtime / "pico8.dat").unlink()
    incomplete = snapshot(sd)
    run(tools / "launch.sh", "--check", status=1)
    assert snapshot(sd) == incomplete and not shared.exists()
    (runtime / "pico8.dat").write_text("fixture data")

    # Normal carts use the MENU helper, exact quoted argument and shared home.
    run(tools / "launch.sh", cart)
    argv = recorded("FIXTURE_ARGS")
    assert argv[-2:] == ["-run", str(cart)] and "-splore" not in argv
    assert recorded("FIXTURE_MENU")[0] == str(runtime / "pico8_64")
    assert argv[argv.index("-root_path") + 1] == str(root_roms), argv
    assert argv[argv.index("-home") + 1] == str(shared / "pico8-studio")
    assert not Path(env["FIXTURE_SYNC"]).exists()
    assert "PICO-8 (PICO)" not in [item.name for item in (sd / "Roms").iterdir()]
    assert recorded("FIXTURE_ENV") == [str(sd), platform, str(sd / "custom system libs")]
    config = shared / "pico8-studio" / "config.txt"
    assert f"root_path {root_roms}/" in config.read_text()
    assert f"cdata_path {shared}/pico8-studio/cdata/" in config.read_text()

    # Existing custom settings and controller files must not be rewritten.
    config.write_text("use_wget 0\ncdata_path keep my saves/\nuser setting 123\n")
    controllers = config.with_name("sdl_controllers.txt")
    controllers.write_text("custom controller start:b7\n")
    preserved = [config.read_bytes(), controllers.read_bytes()]
    run(tools / "launch.sh", cart, status=23, FIXTURE_STATUS="23")
    assert [config.read_bytes(), controllers.read_bytes()] == preserved
    assert not Path(env["FIXTURE_SYNC"]).exists()
    run(tools / "launch.sh", "relative.p8", status=64)
    run(tools / "launch.sh", sd / "missing.p8", status=66)

    clear_records()
    run(tools / "launch.sh", status=19, FIXTURE_STATUS="19")
    assert recorded("FIXTURE_ARGS")[-1] == "-splore"
    assert recorded("FIXTURE_SYNC") == [str(shared / "pico8-studio"), str(root_roms / "Favourites")]
    assert [config.read_bytes(), controllers.read_bytes()] == preserved

    # PICO menu local cartridges prefer the shared fake-08-first P8 launcher.
    write_executable(p8, """#!/bin/sh
# NEXTUI_P8_UNIFIED_RUNNER_V1
[ "${1:-}" != --check ] || exit "${FIXTURE_FAKE_CHECK:-0}"
printf '%s\\n' "$@" > "$FIXTURE_FAKE"
exit "${FIXTURE_FAKE_STATUS:-0}"
""")
    clear_records()
    run(emu / "launch.sh", cart, status=11, FIXTURE_FAKE_STATUS="11")
    assert recorded("FIXTURE_FAKE") == [str(cart)]
    assert not Path(env["FIXTURE_ARGS"]).exists() and not Path(env["FIXTURE_VENDOR"]).exists()

    # Splore never goes through fake-08, and does not pass a fake cart to Tools.
    clear_records()
    run(emu / "launch.sh", root_roms / "Splore.p8")
    assert recorded("FIXTURE_ARGS")[-1] == "-splore"
    assert Path(env["FIXTURE_SYNC"]).exists() and not Path(env["FIXTURE_FAKE"]).exists()
    clear_records()
    run(emu / "launch.sh")
    assert recorded("FIXTURE_ARGS")[-1] == "-splore"

    # No usable P8 -> official -run, without syncing favourites.
    clear_records()
    run(emu / "launch.sh", cart, FIXTURE_FAKE_CHECK="1")
    assert recorded("FIXTURE_ARGS")[-2:] == ["-run", str(cart)]
    assert not Path(env["FIXTURE_SYNC"]).exists()
    # An installed SD launcher takes precedence even when its check fails.
    system_p8 = Path(env["SYSTEM_PATH"]) / "paks" / "Emus" / "P8.pak" / "launch.sh"
    write_executable(system_p8, p8.read_text())
    clear_records()
    run(emu / "launch.sh", cart, FIXTURE_FAKE_CHECK="1")
    assert recorded("FIXTURE_ARGS")[-2:] == ["-run", str(cart)]
    assert not Path(env["FIXTURE_FAKE"]).exists()
    p8.unlink()
    clear_records()
    run(emu / "launch.sh", cart)
    assert recorded("FIXTURE_FAKE") == [str(cart)]
    assert not Path(env["FIXTURE_ARGS"]).exists()
    system_p8.unlink()
    clear_records()
    run(emu / "launch.sh", cart)
    assert recorded("FIXTURE_ARGS")[-2:] == ["-run", str(cart)]

    # Legacy P8 launchers are also unsafe to probe, so bypass them unchanged.
    write_executable(p8, """#!/bin/sh
printf '%s\\n' 'OLD P8 WAS EXECUTED' > "$FIXTURE_FAKE"
""")
    clear_records()
    run(emu / "launch.sh", cart)
    assert recorded("FIXTURE_ARGS")[-2:] == ["-run", str(cart)]
    assert not Path(env["FIXTURE_FAKE"]).exists()
    p8.unlink()

    # Missing dat -> vendor fallback, preserving arguments and exit code.
    (runtime / "pico8.dat").unlink()
    clear_records()
    run(emu / "launch.sh", cart, status=17, FIXTURE_VENDOR_STATUS="17")
    assert recorded("FIXTURE_VENDOR") == [str(cart)]
    assert not Path(env["FIXTURE_ARGS"]).exists()

    # An incomplete primary install can use a complete BIOS install.
    bios.mkdir(parents=True)
    shutil.copy2(runtime / "pico8_64", bios / "pico8_64")
    (bios / "pico8.dat").write_text("complete alternate fixture")
    run(tools / "launch.sh", "--check")
    run(tools / "launch.sh", cart)
    assert recorded("FIXTURE_MENU")[0] == str(bios / "pico8_64")
    run(tools / "launch.sh", "--check", status=1, PICO8_RUNTIME_PATH=str(runtime))

    # Never probe an older launcher: it may ignore --check and open Splore.
    write_executable(tools / "launch.sh", """#!/bin/sh
printf '%s\\n' 'OLD TOOL WAS EXECUTED' > "$FIXTURE_ARGS"
""")
    clear_records()
    run(emu / "launch.sh", cart)
    assert recorded("FIXTURE_VENDOR") == [str(cart)]
    assert not Path(env["FIXTURE_ARGS"]).exists()
    write_executable(emu / "launch-vendor.sh", """#!/bin/sh
# NEXTUI_PICO8_SPLORE_BRIDGE
exit 99
""")
    run(emu / "launch.sh", cart, status=1)

    # Exercise the real helper's exit status and graceful signal forwarding on
    # this host. SDL/physical MENU handling remains an ARM hardware check.
    compiler = shutil.which("cc")
    if compiler and host_platform.system() == "Linux":
        helper = base / "real-menu-exit"
        subprocess.run([compiler, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra",
                        "-Werror", str(SOURCE / "menu-exit.c"), "-o", str(helper)], check=True)
        child = base / "exit child"
        write_executable(child, "#!/bin/sh\nexit 7\n")
        run(helper, child, status=7)
        write_executable(child, """#!/bin/sh
trap 'exit 0' USR1
: > "$FIXTURE_READY"
while :; do sleep 0.1; done
""")
        ready = base / "child-ready"
        process = subprocess.Popen([str(helper), str(child)],
                                   env={**env, "FIXTURE_READY": str(ready)},
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 3
            while not ready.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            assert ready.exists(), "helper child did not become ready"
            process.send_signal(signal.SIGUSR1)
            stdout, stderr = process.communicate(timeout=5)
            assert process.returncode == 0, (stdout, stderr)
            assert b"MENU exit: requesting app shutdown" in stderr
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()

print("PASS: read-only readiness; quoted cart paths; runtime/root resolution; MENU helper; "
      "fake-08-first menu; official Splore and fallback; no config/save override; "
      "no legacy probe or bridge recursion; exit statuses; favourites only for Splore")
