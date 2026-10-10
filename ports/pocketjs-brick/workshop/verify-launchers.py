#!/usr/bin/env python3
"""Verify P8 and workshop launch routing with isolated shell fixtures.

No native app, emulator, model API, or handheld is required. Launcher copies
replace fixed /tmp lifecycle paths before execution, so real resume state and
NextUI's process queue are never touched. Run with Python 3.10 or newer.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


REPOSITORY = Path(__file__).resolve().parents[3]
P8_SOURCE = REPOSITORY / "skeleton/EXTRAS/Emus/tg5040/P8.pak/launch.sh"
WORKSHOP_SOURCE = Path(__file__).with_name("launch.sh")
PROJECT_ID = "game-launcher-fixture"
FIXED_PATHS = ("/tmp/resume_slot.txt", "/tmp/poweroff", "/tmp/reboot", "/tmp/next")

STUB = r'''
import json, os, sys
from pathlib import Path
args = sys.argv[1:]
if MODE == "official" and args == ["--check"] and os.environ["LAUNCH_FIXTURE_OFFICIAL_MARKED"] == "1":
    sys.exit(int(os.environ["LAUNCH_FIXTURE_OFFICIAL_CHECK"]))
resume = Path(os.environ["LAUNCH_FIXTURE_RESUME_PATH"])
record = {"kind": MODE, "argv": args, "cwd": os.getcwd(),
          "resume": resume.read_text() if resume.exists() else None}
if MODE == "ui":
    counter = Path(os.environ["LAUNCH_FIXTURE_UI_COUNTER"])
    count = int(counter.read_text()) + 1 if counter.exists() else 1
    counter.write_text(str(count))
    data = Path(os.environ["POCKETJS_DATA"])
    last = data / "last-project.txt"
    record.update(run=count, runner=os.environ.get("POCKETJS_P8_LAUNCH"),
                  core=os.environ.get("POCKETJS_P8_CORE"),
                  workshop_resume=os.environ.get("POCKETJS_WORKSHOP_RESUME"),
                  last_project=last.read_text() if last.exists() else None)
    if count == 1 and os.environ["LAUNCH_FIXTURE_REQUEST"] == "1":
        project_id = os.environ["LAUNCH_FIXTURE_PROJECT_ID"]
        (data / "play-request.txt").write_text(project_id)
        last.write_text(project_id)
with Path(os.environ["LAUNCH_FIXTURE_LOG"]).open("a", encoding="utf-8") as file:
    file.write(json.dumps(record, ensure_ascii=False) + "\n")
if MODE == "official":
    if len(args) != 1 or not Path(args[0]).is_file():
        sys.exit(66)
    sys.exit(int(os.environ["LAUNCH_FIXTURE_GAME_STATUS"]))
if MODE == "minarch":
    if len(args) != 2 or not all(Path(arg).is_file() for arg in args):
        sys.exit(66)
    sys.exit(int(os.environ["LAUNCH_FIXTURE_GAME_STATUS"]))
if MODE == "ui":
    sys.exit(int(os.environ["LAUNCH_FIXTURE_UI_STATUS"]))
'''


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def snapshot(directory: Path) -> dict:
    """Ignore access times, but catch check-mode writes and new directories."""
    result = {}
    for path in sorted(directory.rglob("*")):
        relative = str(path.relative_to(directory))
        stat = path.stat()
        result[relative] = (stat.st_mode, stat.st_mtime_ns,
                            hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None)
    return result


def executable(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    path.chmod(0o755)


@dataclass(frozen=True)
class Case:
    name: str
    official: str = "missing"  # missing, ready, unavailable, legacy
    sd: str = "complete"      # complete, incomplete, missing
    system: bool = False
    minarch: bool = True
    expected: str | None = "fake"
    selected: str = "sd"
    game_status: int = 0
    ui_status: int = 0
    rom: bool = True
    request: bool = True
    project_id: str = PROJECT_ID


class Fixture:
    def __init__(self, base: Path, shell: list[str], case: Case):
        self.base, self.shell, self.case = base, shell, case
        self.sd = base / "SD card's 游戏"
        self.system = self.sd / ".system/tg5040"
        self.userdata = self.sd / ".userdata/tg5040"
        self.shared = self.sd / ".userdata/shared"
        self.data = self.shared / "ai-game-workshop"
        self.roms = self.sd / "Roms/AI生成游戏 (P8)"
        self.rom = self.roms / (PROJECT_ID + ".p8")
        self.workshop = self.sd / "Tools/tg5040/AI Game Workshop.pak"
        self.sd_p8 = self.sd / "Emus/tg5040/P8.pak"
        self.system_p8 = self.system / "paks/Emus/P8.pak"
        self.log = base / "calls.jsonl"
        self.resume = base / "resume-slot.txt"
        for directory in (self.data, self.roms, self.userdata, self.workshop, self.system / "bin"):
            directory.mkdir(parents=True, exist_ok=True)
        # A machine that happens to have minarch installed must not satisfy a
        # missing-runtime fixture or accidentally start a real emulator.
        utilities = base / "utilities"
        utilities.mkdir()
        for name in ("basename", "cat", "dirname", "env", "grep", "mkdir", "rm"):
            utility = shutil.which(name)
            require(utility is not None, "Missing shell fixture utility: " + name)
            (utilities / name).symlink_to(utility)
        if case.rom:
            self.rom.write_text("pico-8 cartridge // launcher fixture\n", encoding="ascii")
        self.environment = dict(os.environ, PLATFORM="tg5040", SDCARD_PATH=str(self.sd),
            SYSTEM_PATH=str(self.system), USERDATA_PATH=str(self.userdata),
            SHARED_USERDATA_PATH=str(self.shared), BIOS_PATH=str(self.sd / "Bios"),
            SAVES_PATH=str(self.sd / "Saves"), CHEATS_PATH=str(self.sd / "Cheats"),
            LOGS_PATH=str(self.userdata / "logs"),
            PATH=str(self.system / "bin") + os.pathsep + str(utilities),
            LAUNCH_FIXTURE_LOG=str(self.log), LAUNCH_FIXTURE_UI_COUNTER=str(base / "ui-count.txt"),
            LAUNCH_FIXTURE_RESUME_PATH=str(self.resume), LAUNCH_FIXTURE_PROJECT_ID=case.project_id,
            LAUNCH_FIXTURE_REQUEST="1" if case.request else "0",
            LAUNCH_FIXTURE_OFFICIAL_CHECK="0" if case.official == "ready" else "1",
            LAUNCH_FIXTURE_OFFICIAL_MARKED="0" if case.official == "legacy" else "1",
            LAUNCH_FIXTURE_GAME_STATUS=str(case.game_status), LAUNCH_FIXTURE_UI_STATUS=str(case.ui_status))
        for key in ("POCKETJS_P8_LAUNCH", "POCKETJS_P8_CORE", "POCKETJS_WORKSHOP_RESUME"):
            self.environment.pop(key, None)
        self.copy_launcher(WORKSHOP_SOURCE, self.workshop / "launch.sh")
        self.stub(self.workshop / "pocketjs-app.elf", "ui")
        self.stub(self.system / "bin/gametimectl.elf", "timing")
        if case.minarch:
            self.stub(self.system / "bin/minarch.elf", "minarch")
        if case.official != "missing":
            self.stub(self.sd / "Tools/tg5040/PICO-8.pak/launch.sh", "official")
        for path, exists, complete in (
            (self.sd_p8, case.sd != "missing", case.sd == "complete"),
            (self.system_p8, case.system, case.system),
        ):
            if exists:
                self.copy_launcher(P8_SOURCE, path / "launch.sh")
                if complete:
                    (path / "fake08_libretro.so").write_bytes(b"fixture core; never loaded\n")

    def copy_launcher(self, source: Path, target: Path) -> None:
        text = source.read_text(encoding="utf-8")
        literals = set(re.findall(r"/tmp/[a-zA-Z0-9_.-]+", text))
        require(literals.issubset(FIXED_PATHS), f"Unexpected fixed /tmp path in {source}: {literals}")
        require(not re.search(r"\s", str(self.base)), "Fixture root must not contain whitespace")
        for original in FIXED_PATHS:
            replacement = self.resume if original == "/tmp/resume_slot.txt" else self.base / Path(original).name
            text = text.replace(original, str(replacement))
        text = text.replace("/usr/trimui/bin", str(self.base / "empty-trimui-bin"))
        shebang = "#!" + " ".join(self.shell) + "\n"
        require(text.startswith("#!/bin/sh\n"), f"Unexpected launcher shebang: {source}")
        executable(target, shebang + text[len("#!/bin/sh\n"):])

    def stub(self, target: Path, mode: str) -> None:
        require(" " not in sys.executable, "Python executable path contains spaces")
        marker = "# NEXTUI_PICO8_UNIFIED_RUNNER_V1\n" if mode == "official" and self.case.official != "legacy" else ""
        executable(target, "#!" + sys.executable + "\n" + marker + "MODE = " + repr(mode) + "\n" + STUB)

    @property
    def selected(self) -> Path:
        return self.sd_p8 if self.case.selected == "sd" else self.system_p8

    def run(self, launcher: Path, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run(self.shell + [str(launcher), *args], cwd=self.sd,
                              env=self.environment, capture_output=True, text=True, timeout=8)

    def calls(self, kind: str | None = None) -> list[dict]:
        calls = [json.loads(line) for line in self.log.read_text(encoding="utf-8").splitlines()] if self.log.exists() else []
        return calls if kind is None else [call for call in calls if call["kind"] == kind]

    def runtime_calls(self) -> list[dict]:
        return [call for call in self.calls() if call["kind"] in ("official", "minarch")]


def verify_case(base: Path, shell: list[str], case: Case) -> dict:
    fixture = Fixture(base, shell, case)
    tag = case.name + " (" + " ".join(shell) + ")"
    # --check must neither start a game nor create logs/userdata directories.
    fixture.resume.write_text("8", encoding="ascii")
    if (fixture.selected / "launch.sh").exists():
        before = snapshot(base)
        check = fixture.run(fixture.selected / "launch.sh", "--check")
        require((check.returncode == 0) == (case.expected is not None), tag + ": readiness mismatch")
        require(snapshot(base) == before, tag + ": --check wrote files or launched a process")
    fixture.resume.write_text("8", encoding="ascii")
    result = fixture.run(fixture.workshop / "launch.sh")
    require(result.returncode == case.ui_status, tag + ": wrapper changed UI exit status")
    ui_calls = fixture.calls("ui")
    expect_second_ui = case.ui_status == 0 and case.request and case.project_id == PROJECT_ID
    require(len(ui_calls) == (2 if expect_second_ui else 1), tag + ": UI process count mismatch")
    if expect_second_ui:
        require(ui_calls[1]["last_project"] == PROJECT_ID, tag + ": project selection lost")
    ready_launcher = str(fixture.selected / "launch.sh") if case.expected is not None else None
    require(all(call["runner"] == ready_launcher for call in ui_calls), tag + ": UI runner mismatch")
    core = fixture.selected / "fake08_libretro.so"
    cover_core = core if core.exists() else fixture.system_p8 / "fake08_libretro.so"
    require(all(call["core"] == (str(cover_core) if cover_core.exists() else None) for call in ui_calls),
            tag + ": cover core selection depends on gameplay readiness")
    should_play = case.expected is not None and case.ui_status == 0 and case.rom and case.request and case.project_id == PROJECT_ID
    runtime_calls = fixture.runtime_calls()
    require(len(runtime_calls) == (1 if should_play else 0), tag + ": wrong emulator invocation count")
    expected_kind = "official" if case.expected == "official" else "minarch"
    expected_args = [str(fixture.rom)] if case.expected == "official" else [str(core), str(fixture.rom)]
    if should_play:
        require(runtime_calls[0]["kind"] == expected_kind, tag + ": wrong runtime")
        require(runtime_calls[0]["argv"] == expected_args, tag + ": ROM/core argument changed")
        require(runtime_calls[0]["resume"] is None, tag + ": AI game resumed obsolete Lua")
        require([call["argv"] for call in fixture.calls("timing")] ==
                [["start", str(fixture.rom)], ["stop", str(fixture.rom)]],
                tag + ": game timing boundaries changed")
        require(ui_calls[1]["workshop_resume"] == "1", tag + ": returning UI missed resume marker")
        fixture.resume.write_text("8", encoding="ascii")
        direct = fixture.run(fixture.selected / "launch.sh", str(fixture.rom))
        require(direct.returncode == case.game_status, tag + ": direct launcher changed game exit status")
        require([(call["kind"], call["argv"], call["resume"]) for call in fixture.runtime_calls()] ==
                [(expected_kind, expected_args, None), (expected_kind, expected_args, None)],
                tag + ": menu and workshop routes differ")
        require(len(fixture.calls("ui")) == 2, tag + ": direct menu route restarted workshop")
    else:
        require(not fixture.calls("timing"), tag + ": timer started without a game")
    error = fixture.data / "launch-error.txt"
    expected_error = "退出异常" if should_play and case.game_status else (
        "暂时无法启动" if expect_second_ui and not should_play else None)
    require(error.exists() == bool(expected_error), tag + ": launch error visibility mismatch")
    if expected_error:
        require(expected_error in error.read_text(encoding="utf-8"), tag + ": wrong launch error")
    if expect_second_ui:
        require(not (fixture.data / "play-request.txt").exists(), tag + ": play request was not consumed")
    require(not (base / "next").exists(), tag + ": NextUI queue was written")
    return {"name": case.name, "passed": True, "runtime": case.expected, "played": should_play}


def verify_resume_scope(base: Path, shell: list[str], runtime: str) -> dict:
    case = Case("resume-scope-" + runtime, official="ready" if runtime == "official" else "missing",
                expected=runtime, minarch=runtime == "fake", sd="incomplete" if runtime == "official" else "complete")
    fixture = Fixture(base, shell, case)
    rom_cases = (
        (fixture.roms / "子目录/it's a game.p8", True),
        (fixture.sd / "Roms/PICO-8 (P8)/it's a game.p8", False),
        (fixture.sd / "Roms/AI生成游戏 (P8)-backup/it's a game.p8", False),
        (fixture.roms / "it's a game.p8.png", True),
    )
    for rom, fresh in rom_cases:
        rom.parent.mkdir(parents=True, exist_ok=True)
        rom.write_text("fixture", encoding="ascii")
        fixture.resume.write_text("8", encoding="ascii")
        result = fixture.run(fixture.selected / "launch.sh", str(rom))
        require(result.returncode == 0, case.name + ": scoped launch failed")
        call = fixture.runtime_calls()[-1]
        expected_args = [str(rom)] if runtime == "official" else [str(fixture.selected / "fake08_libretro.so"), str(rom)]
        require(call["argv"] == expected_args, case.name + ": quoted/spaced ROM path changed")
        require(call["resume"] == (None if fresh else "8"), case.name + ": resume scope escaped AI directory")
    return {"name": case.name, "passed": True, "paths": len(rom_cases)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Save the compact JSON verification report.")
    args = parser.parse_args()
    shells = [[str(Path("/bin/sh").resolve())]]
    dash = shutil.which("dash")
    if dash:
        shells.append([str(Path(dash).resolve())])
    busybox = shutil.which("busybox")
    if busybox:
        shells.append([str(Path(busybox).resolve()), "sh"])
    # Keep /bin/sh and dash as separate requested invocations even where sh is dash.
    cases = (
        Case("official-without-fake-core-or-minarch", official="ready", sd="incomplete", minarch=False, expected="official"),
        Case("official-with-core-but-without-minarch", official="ready", minarch=False, expected="official"),
        Case("fake-preferred-over-official", official="ready"),
        Case("fake-fallback-without-official"),
        Case("fake-fallback-with-unready-official", official="unavailable"),
        Case("legacy-tools-never-probed", official="legacy", sd="incomplete", minarch=False, expected=None),
        Case("legacy-tools-do-not-interfere-with-fake", official="legacy"),
        Case("system-p8-fallback", sd="missing", system=True, selected="system"),
        Case("system-p8-official", sd="missing", system=True, minarch=False, official="ready", selected="system", expected="official"),
        Case("sd-incomplete-does-not-switch-to-system", sd="incomplete", system=True, expected=None),
        Case("no-runtime", sd="incomplete", minarch=False, expected=None),
        Case("no-p8-package", sd="missing", minarch=False, expected=None),
        Case("unready-official-without-fake", official="unavailable", sd="incomplete", minarch=False, expected=None),
        Case("fake-missing-core", sd="incomplete", expected=None),
        Case("fake-missing-minarch", minarch=False, expected=None),
        Case("official-game-error-visible", official="ready", sd="incomplete", expected="official", game_status=7),
        Case("fake-game-error-does-not-retry-official", official="ready", game_status=9),
        Case("ui-error-never-starts-game", ui_status=17),
        Case("missing-rom-never-starts-game", rom=False),
        Case("invalid-project-id-never-starts-game", project_id="game-../escape"),
        Case("ui-exit-without-play-request", request=False),
    )
    results = []
    with tempfile.TemporaryDirectory(prefix="workshop-launchers-") as temporary:
        base = Path(temporary)
        for index, shell in enumerate(shells):
            for case in cases:
                result = verify_case(base / str(index) / case.name, shell, case)
                result["shell"] = " ".join(shell)
                results.append(result)
            for runtime in ("official", "fake"):
                result = verify_resume_scope(base / str(index) / ("resume-scope-" + runtime), shell, runtime)
                result["shell"] = " ".join(shell)
                results.append(result)
    report = {"passed": True, "checks": len(results), "shells": [" ".join(shell) for shell in shells],
              "busybox_available": bool(busybox), "real_emulator_executed": False,
              "real_tmp_state_touched": False, "cases": results}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Launchers verified: {len(results)} checks across {len(shells)} shells; "
          f"busybox {'included' if busybox else 'unavailable'}; no real emulator or /tmp state touched.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
