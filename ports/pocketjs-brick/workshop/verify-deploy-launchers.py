#!/usr/bin/env python3
"""Exercise the actual launcher deployment heredoc on temporary SD fixtures.

Requires the already built fake-08 and native launcher packages. No SSH, real
handheld, native binary, emulator, user credentials or real /tmp state is used.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[3]
DEPLOY = Path(__file__).with_name("deploy-launchers.sh")
P8 = ROOT / "skeleton/EXTRAS/Emus/tg5040/P8.pak/launch.sh"
NATIVE = ROOT / "build/native-consoles/PICO-8.pak"
CORE = ROOT / "build/pocketjs-port/P8.pak"
BRIDGE = ROOT / "ports/native-consoles/pico8-emu-launch.sh"
WORKSHOP = Path(__file__).with_name("launch.sh")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def put(path: Path, content: bytes | str, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content.encode() if isinstance(content, str) else content)
    path.chmod(mode)


def payload(base: Path) -> Path:
    require((CORE / "fake08_libretro.so").is_file(), "Build the fake-08 package first.")
    require((NATIVE / "menu-exit.elf").is_file(), "Build the native launcher package first.")
    require((NATIVE / "sdl-nosensor.so").is_file(), "Build the native launcher support library first.")
    stage = base / "payload"
    shutil.copytree(CORE, stage / "P8.pak")
    shutil.copy2(P8, stage / "P8.pak/launch.sh")
    shutil.copytree(NATIVE, stage / "PICO-8.pak")
    shutil.copy2(BRIDGE, stage / "pico8-emu-launch.sh")
    shutil.copy2(WORKSHOP, stage / "workshop-launch.sh")
    require(not any(path.name in ("pico8", "pico8_64", "pico8.dat") for path in stage.rglob("*")),
            "Deployment payload must not contain a commercial runtime.")
    archive = base / "payload.tar.gz"
    with tarfile.open(archive, "w:gz") as file:
        for path in stage.iterdir():
            file.add(path, arcname=path.name)
    return archive


def remote_source() -> str:
    source = DEPLOY.read_text(encoding="utf-8")
    match = re.search(r"<<'REMOTE'\n(.*?)\nREMOTE(?:\n|$)", source, re.S)
    require(match is not None, "Deployment REMOTE heredoc was not found.")
    return match.group(1) + "\n"


@dataclass(frozen=True)
class Case:
    name: str
    sd: str = "complete"  # complete, incomplete, missing, nonexecutable
    system: bool = False
    vendor: str = "original"  # original, existing, bridge-only, missing
    runtime: bool = False


def verify(base: Path, source: str, package: Path, shell: list[str], case: Case) -> dict:
    sd = base / "SD card's 游戏"
    shared = sd / ".userdata/shared"
    sd_p8 = sd / "Emus/tg5040/P8.pak"
    system_p8 = sd / ".system/tg5040/paks/Emus/P8.pak"
    tools = sd / "Tools/tg5040/PICO-8.pak"
    emu = sd / "Emus/tg5040/PICO.pak"
    workshop = sd / "Tools/tg5040/AI Game Workshop.pak"
    custom_core = b"custom installed fake core\n"
    custom_config = b"bind A Button = B\ncustom-user-option = preserved\n"
    system_core = b"different system fake core\n"
    original_vendor = b"#!/bin/sh\n# custom original vendor entry\nexit 81\n"
    saved_vendor = b"#!/bin/sh\n# custom separately saved vendor entry\nexit 82\n"
    protected: dict[Path, str] = {}

    def protect(path: Path, content: bytes | str, mode: int = 0o644) -> None:
        put(path, content, mode)
        protected[path] = digest(path)

    if case.sd != "missing":
        protect(sd_p8 / "default.cfg", custom_config)
        put(sd_p8 / "launch.sh", "#!/bin/sh\n# old SD router\nexit 83\n", 0o644 if case.sd == "nonexecutable" else 0o755)
        if case.sd != "incomplete":
            protect(sd_p8 / "fake08_libretro.so", custom_core)
    if case.system:
        protect(system_p8 / "fake08_libretro.so", system_core)
        protect(system_p8 / "default.cfg", b"custom system config\n")
        put(system_p8 / "launch.sh", "#!/bin/sh\n# old system router\nexit 84\n", 0o755)
    selected = system_p8 if case.sd == "missing" and case.system else sd_p8
    untouched_system_router = digest(system_p8 / "launch.sh") if case.system and selected == sd_p8 else None
    if case.vendor != "missing":
        if case.vendor == "original":
            put(emu / "launch.sh", original_vendor, 0o755)
        else:
            put(emu / "launch.sh", BRIDGE.read_bytes(), 0o755)
            if case.vendor == "existing":
                protect(emu / "launch-vendor.sh", saved_vendor, 0o755)
    if case.runtime:
        # These inert bytes satisfy only the shell's readiness metadata checks;
        # actually executing them would fail, making accidental launch visible.
        protect(emu / "pico8/pico8_64", "#!/bin/sh\nexit 85\n", 0o755)
        protect(emu / "pico8/pico8.dat", b"user commercial runtime placeholder\n")
    protect(shared / "pico8-studio/config.txt", "root_path user-root\ncustom_setting preserved\n")
    protect(shared / "pico8-studio/sdl_controllers.txt", b"custom user controller mapping\n")
    protect(shared / "pico8-studio/cdata/user-save.p8d.txt", b"persistent user game save\n")
    protect(shared / "ai-game-workshop/config.json", '{"provider":"fixture","model":"user-choice"}\n')
    protect(shared / "ai-game-workshop/projects/game-existing.json", '{"title":"existing project"}\n')
    protect(sd / "Roms/AI生成游戏 (P8)/game-existing.p8", "existing user cartridge\n")
    protect(workshop / "pocketjs-app.elf", "#!/bin/sh\nexit 86\n", 0o755)
    protect(workshop / "brick-app.js", "// existing user app bundle\n")
    put(workshop / "launch.sh", "#!/bin/sh\n# old workshop wrapper\nexit 87\n", 0o755)
    put(tools / "launch.sh", "#!/bin/sh\n# legacy Tools must not launch during deploy\nexit 88\n", 0o755)
    # Open old support files across installation to prove rename preserves the
    # previous inode for already-running processes and mapped libraries.
    put(tools / "menu-exit.elf", b"old helper inode\n", 0o755)
    put(tools / "sdl-nosensor.so", b"old library inode\n")
    opened = [(path, path.open("rb"), path.read_bytes()) for path in
              (tools / "menu-exit.elf", tools / "sdl-nosensor.so")]
    utilities = base / "utilities"
    utilities.mkdir(parents=True)
    for name in ("basename", "cat", "chmod", "cp", "dirname", "grep", "mkdir", "mv", "rm", "sha256sum", "tar"):
        command = shutil.which(name)
        require(command is not None, "Missing fixture utility: " + name)
        (utilities / name).symlink_to(command)
    # The router can find this command, but --check must never execute it.
    minarch = sd / ".system/tg5040/bin/minarch.elf"
    invoked = base / "unexpected-minarch-invocation"
    put(minarch, "#!/bin/sh\nprintf '%s\\n' invoked > " + shlex.quote(str(invoked)) + "\nexit 89\n", 0o755)
    script = source.replace("TASK_SD=/mnt/SDCARD\n", "TASK_SD=" + shlex.quote(str(sd)) + "\n")
    require(script != source, "Fixed deployment SD path was not isolated.")
    script = script.replace("/usr/trimui/bin", str(base / "empty-trimui-bin"))
    environment = dict(os.environ, PATH=str(utilities), SDCARD_PATH=str(sd), PLATFORM="tg5040",
                       SYSTEM_PATH=str(sd / ".system/tg5040"), USERDATA_PATH=str(sd / ".userdata/tg5040"),
                       SHARED_USERDATA_PATH=str(shared), BIOS_PATH=str(sd / "Bios"))
    environment.pop("PICO8_RUNTIME_PATH", None)
    environment.pop("PICO8_ROMS_PATH", None)
    try:
        for index in range(2):
            stamp = "fixture-" + str(index)
            archive = shared / (stamp + ".tar.gz")
            shutil.copyfile(package, archive)
            result = subprocess.run(shell + ["-s", "--", stamp, str(archive)], input=script,
                                    env=environment, capture_output=True, text=True, timeout=15)
            require(result.returncode == 0, case.name + ": deployment failed: " + result.stderr.strip())
            require(not invoked.exists(), case.name + ": readiness probe started minarch")
            require("Official PICO-8 Tools runtime ready." in result.stdout if case.runtime else
                    "Tools requires the separately installed official PICO-8 runtime" in result.stdout,
                    case.name + ": official runtime readiness result changed")
            for path, expected in protected.items():
                require(digest(path) == expected, case.name + ": replaced protected user/runtime/core file: " + str(path.relative_to(sd)))
            require((selected / "launch.sh").read_bytes() == P8.read_bytes(), case.name + ": selected P8 router not refreshed")
            require(os.access(selected / "launch.sh", os.X_OK), case.name + ": P8 router is not executable")
            require((selected / "fake08_libretro.so").exists(), case.name + ": missing fake core was not repaired")
            if selected == system_p8:
                require(not (sd_p8 / "launch.sh").exists(), case.name + ": system P8 shadowed by a new SD launcher")
            if untouched_system_router:
                require(digest(system_p8 / "launch.sh") == untouched_system_router, case.name + ": modified the unselected system router")
            if case.sd in ("missing", "incomplete") and selected == sd_p8:
                require(digest(selected / "fake08_libretro.so") == digest(CORE / "fake08_libretro.so"), case.name + ": missing core differs from payload")
            if case.vendor != "missing":
                require((emu / "launch.sh").read_bytes() == BRIDGE.read_bytes(), case.name + ": PICO bridge not installed")
                if case.vendor == "original":
                    require((emu / "launch-vendor.sh").read_bytes() == original_vendor, case.name + ": original vendor changed on repeat deployment")
                elif case.vendor == "bridge-only":
                    require(not (emu / "launch-vendor.sh").exists(), case.name + ": bridge incorrectly saved as vendor fallback")
            else:
                require(not emu.exists(), case.name + ": deployment created an unrelated PICO package")
            require((workshop / "launch.sh").read_bytes() == WORKSHOP.read_bytes(), case.name + ": workshop wrapper not refreshed")
            for name in ("launch.sh", "menu-exit.elf", "sdl-nosensor.so", "bin/wget", "certs/cacert.pem"):
                require(digest(tools / name) == digest(NATIVE / name), case.name + ": native helper/support payload mismatch: " + name)
            for path, file, original in opened:
                file.seek(0)
                require(file.read() == original, case.name + ": helper was truncated while open: " + path.name)
            backup = shared / ("ai-game-workshop-backups/" + stamp + "-launchers")
            require((backup / "PICO-8-tools.tar").exists(), case.name + ": Tools backup missing")
            require((backup / "Tools/tg5040/AI Game Workshop.pak/launch.sh").exists(), case.name + ": workshop launcher backup missing")
            require(not archive.exists(), case.name + ": uploaded archive was not consumed")
            require(not (shared / ("p8-launchers-stage-" + stamp)).exists(), case.name + ": staging directory was not cleaned")
            require(not list(sd.rglob("*.new")), case.name + ": unfinished atomic replacement remains")
    finally:
        for _, file, _ in opened:
            file.close()
    return {"name": case.name, "passed": True, "deployments": 2, "runtime_present": case.runtime,
            "selected_p8": "system" if selected == system_p8 else "sd", "protected_files": len(protected)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Save the compact JSON report.")
    args = parser.parse_args()
    shells = [["/bin/sh"]]
    dash = shutil.which("dash")
    if dash:
        shells.append([dash])
    busybox = shutil.which("busybox")
    if busybox:
        shells.append([busybox, "sh"])
    cases = (
        Case("complete-sd"),
        Case("system-only", sd="missing", system=True),
        Case("new-install", sd="missing", vendor="missing"),
        Case("incomplete-sd-with-system", sd="incomplete", system=True),
        Case("nonexecutable-sd-router", sd="nonexecutable", system=True),
        Case("existing-custom-vendor-backup", vendor="existing"),
        Case("existing-bridge-without-vendor-backup", vendor="bridge-only"),
        Case("commercial-runtime-preserved", runtime=True),
    )
    source = remote_source()
    results = []
    with tempfile.TemporaryDirectory(prefix="workshop-deploy-launchers-") as temporary:
        base = Path(temporary)
        package = payload(base)
        for index, shell in enumerate(shells):
            for case in cases:
                result = verify(base / str(index) / case.name, source, package, shell, case)
                result["shell"] = " ".join(shell)
                results.append(result)
    report = {"passed": True, "cases": results, "checks": len(results),
              "deployments": sum(result["deployments"] for result in results),
              "real_native_binary_executed": False, "real_device_touched": False,
              "payload_contains_commercial_runtime": False, "busybox_available": bool(busybox)}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Launcher deployment verified: {len(results)} cases, {report['deployments']} installs, "
          f"{len(shells)} shells; user/core/runtime files and open helper inodes preserved.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
