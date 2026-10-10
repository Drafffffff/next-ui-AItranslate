#!/usr/bin/env python3
"""Exercise the real PocketJS host against a local deterministic model server.

python3 ports/pocketjs-brick/workshop/verify.py --host /path/to/pocketjs-app \
    --output build/workshop-verification [--recover]

The host directory must contain its brick-app.js/.pak and Chinese fallback font.
Only temporary SD-card fixtures are written. No real model or P8 process runs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REPOSITORY = Path(__file__).resolve().parents[3]
FAKE_KEY = "workshop-fixture-key"
BACKGROUND = (0x17, 0x2D, 0x3D)
TOOL_NAMES = {"read_skill", "read_game", "ask_user_question", "write_game", "check_game", "publish_game", "terminal"}
LARGE_GAME_DATA = ""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def lua_for_revision(revision: int) -> str:
    speed = revision
    color = {1: 8, 2: 12, 3: 10}[revision]
    source = f"""-- workshop-fixture-revision-{revision}
x=64
y=64
speed={speed}
player_color={color}
function _init()
 x=64
 y=64
 music(6,200,3)
end
function _update()
 if btn(0) then x=max(4,x-speed) end
 if btn(1) then x=min(123,x+speed) end
 if btn(2) then y=max(4,y-speed) end
 if btn(3) then y=min(123,y+speed) end
 if btnp(4) then sfx(0,3) _init() end
end
function _draw()
 cls(0)
 circfill(x,y,4,player_color)
 print("d-pad move, o restart",4,4,7)
end
"""
    if LARGE_GAME_DATA:
        source = source.replace(' cls(0)\n', ''' cls(0)
 for tile in all(terrain) do
  pset(tile.x+14,tile.y+24,tile.c)
 end
 rectfill(124,124,127,127,terrain[2000].c)
''')
    return source + LARGE_GAME_DATA


def cartridge(lua: str) -> str:
    return "pico-8 cartridge // http://www.pico-8.com\nversion 42\n__lua__\n" + lua.rstrip() + "\n__gfx__\n" + ("0" * 128 + "\n") * 128


def title_for_revision(revision: int) -> str:
    return {1: "小猫冒险", 2: "小猫冒险·新版", 3: "小猫冒险·恢复版"}[revision]


def response_for(content: str | None, calls: list[dict] | None = None) -> dict:
    message = {"role": "assistant", "content": content}
    if calls:
        message["tool_calls"] = calls
    return {"id": "local-workshop-fixture", "object": "chat.completion", "choices": [
        {"index": 0, "message": message, "finish_reason": "tool_calls" if calls else "stop"}
    ], "usage": {"prompt_tokens": 100, "completion_tokens": 50, "total_tokens": 150}}


def tool_call(revision: int, name: str, args: dict | None = None) -> dict:
    return {"id": f"fixture-r{revision}-{name}", "type": "function", "function": {
        "name": name, "arguments": json.dumps(args or {}, ensure_ascii=False)
    }}


class MockState:
    def __init__(self) -> None:
        self.requests: list[dict] = []
        self.errors: list[str] = []
        self.project_id: str | None = None
        self.lock = threading.Lock()

    def completion(self, path: str, authorization: str | None, body: dict) -> dict:
        with self.lock:
            self.requests.append({"path": path, "body": body})
            require(path == "/v1/chat/completions", "Unexpected model endpoint")
            require(authorization == "Bearer " + FAKE_KEY, "Fixture authorization was not forwarded")
            require(body.get("stream") is True, "The native generation bridge must request SSE")
            require(FAKE_KEY not in json.dumps(body, ensure_ascii=False), "Credential leaked into model messages")
            names = {tool.get("function", {}).get("name") for tool in body.get("tools", [])}
            require(names == TOOL_NAMES, "Expected the seven real project tools")
            messages = body.get("messages", [])
            system = [message.get("content", "") for message in messages if message.get("role") == "system"]
            require(len(system) == 1, "Expected one system prompt")
            for marker in ('<skill name="pico8-game-dev">', 'references/pico8-api.md', 'references/project-contract.md', 'assets/catalog.json', 'sprite_overrides', 'modules/music.md', 'sfx_overrides', 'music_overrides'):
                require(marker in system[0], "Default PICO-8 Skill/reference missing: " + marker)
            require('there are no bundled sprites' not in system[0], "Obsolete prompt disabled the bundled sprites")
            require(len(system[0]) < 16000 and '## 编写自定义曲目' not in system[0], "Optional module leaked into the default prompt")
            user_positions = [i for i, value in enumerate(messages) if value.get("role") == "user"]
            require(len(user_positions) in (1, 2), "Expected exactly the create/modify user turns")
            revision = len(user_positions)
            tail = messages[user_positions[-1] + 1:]
            calls = {call["id"]: call["function"]["name"] for message in tail
                     for call in message.get("tool_calls", [])}
            results = {}
            for message in tail:
                if message.get("role") == "tool":
                    name = calls.get(message.get("tool_call_id"))
                    require(name in TOOL_NAMES, "Tool result lacks its assistant tool call")
                    results[name] = json.loads(message["content"])
            if "read_game" not in results:
                return response_for(None, [tool_call(revision, "read_game")])
            read = results["read_game"]
            require(len(read.get("assets", {}).get("sprites", [])) == 12, "read_game omitted built-in sprites")
            project_id = read.get("project_id")
            require(isinstance(project_id, str) and re.fullmatch(r"game-[a-z0-9-]{1,43}", project_id), "Invalid project identity")
            if self.project_id is None:
                self.project_id = project_id
            require(project_id == self.project_id, "Modification changed the project identity")
            require(read.get("revision") == revision - 1, "Project revision did not survive reopening")
            if revision == 2:
                require("workshop-fixture-revision-1" in read.get("lua", ""), "Reopened game lost its first Lua version")
            require('audio' not in read['assets'], "Unrelated game edit loaded full music data")
            if "read_skill" not in results:
                return response_for(None, [tool_call(revision, "read_skill", {"module": "game-loop"})])
            module = results["read_skill"]
            require(module.get("path") == "modules/game-loop.md" and '修改前的影响范围' in module.get("text", ""), "Native progressive module read failed")
            if "write_game" not in results:
                args = {"title": title_for_revision(revision), "summary": "方向键移动；O 重开。" if revision == 1 else "移动速度改为 2，玩家改为蓝色；方向键移动，O 重开。"}
                if revision == 1:
                    args["lua"] = lua_for_revision(revision)
                else:
                    args["edits"] = [{"oldText": "-- workshop-fixture-revision-1", "newText": "-- workshop-fixture-revision-2"}, {"oldText": "speed=1", "newText": "speed=2"}, {"oldText": "player_color=8", "newText": "player_color=12"}]
                return response_for(None, [tool_call(revision, "write_game", args)])
            require(results["write_game"].get("draft_saved") is True, "write_game did not save the draft")
            require(results["write_game"].get("project_id") == project_id, "Draft changed the project identity")
            require(results["write_game"].get("mode") == ("replace" if revision == 1 else "edit"), "Native whole/partial write mode mismatch")
            if revision == 2:
                require(results["write_game"].get("edit_count") == 3 and re.search(r'\+\s*4 speed=2', results["write_game"].get("diff", "")), "Native partial edit lost its actual numbered diff")
            if "check_game" not in results:
                return response_for(None, [tool_call(revision, "check_game")])
            require(results.get("check_game", {}).get("format_checked") is True, "Format check was omitted")
            require(results["check_game"].get("gameplay_tested") is False, "Format check claimed gameplay testing")
            if "publish_game" not in results:
                return response_for(None, [tool_call(revision, "publish_game")])
            published = results["publish_game"]
            require(published.get("published") is True and published.get("revision") == revision, "publish_game did not commit the revision")
            require(published.get("project_id") == project_id, "Publication changed the project identity")
            return response_for(f"第 {revision} 版已保存。方向键移动，O 重开；按 START 试玩。")


def make_handler(state: MockState):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, _format: str, *_args) -> None:
            pass

        def do_POST(self) -> None:
            try:
                length = int(self.headers.get("Content-Length", "0"))
                require(0 < length <= 1024 * 1024, "Request body outside the fixture limit")
                body = json.loads(self.rfile.read(length))
                result = state.completion(self.path, self.headers.get("Authorization"), body)
                status = 200
            except Exception as error:
                # Fixture failures become an ordinary HTTP error visible to the agent.
                with state.lock:
                    state.errors.append(str(error))
                result = {"error": {"message": str(error)}}
                status = 400
            if status==200 and body.get('stream'):
                message=result['choices'][0]['message'];frames=[]
                content=message.get('content') or ''
                for i in range(0,len(content),12):frames.append(dict(choices=[dict(index=0,delta=dict(content=content[i:i+12]),finish_reason=None)]))
                for index,call in enumerate(message.get('tool_calls',[])):
                    args=call['function']['arguments']
                    for i in range(0,len(args),64):
                        delta=dict(index=index,function=dict(arguments=args[i:i+64]))
                        if i==0:delta.update(id=call['id'],type='function');delta['function']['name']=call['function']['name']
                        frames.append(dict(choices=[dict(index=0,delta=dict(tool_calls=[delta]),finish_reason=None)]))
                frames.append(dict(choices=[dict(index=0,delta={},finish_reason=result['choices'][0]['finish_reason'])]));frames.append(dict(choices=[],usage=result['usage']))
                self.send_response(200);self.send_header('Content-Type','text/event-stream; charset=utf-8');self.end_headers()
                for frame in frames:
                    wire=('data: '+json.dumps(frame,ensure_ascii=False)+'\r\n\r\n').encode()
                    # Deliberately split inside UTF-8 and JSON, not just at SSE boundaries.
                    try:
                        for i in range(0,len(wire),17):self.wfile.write(wire[i:i+17]);self.wfile.flush()
                    except (BrokenPipeError,ConnectionResetError):return
                    time.sleep(getattr(self.server,'stream_delay',.002))
                self.wfile.write(b'data: [DONE]\r\n\r\n');self.wfile.flush();return
            encoded = json.dumps(result, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)
    return Handler


def json_file(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get('history'):
        messages=[]
        for page in value['history']['pages']:
            require(re.fullmatch(r'history/game-[a-z0-9-]+-[a-f0-9]{24}\.json',page['file']), 'Invalid journal page path')
            raw=(path.parent.parent/page['file']).read_bytes()
            require(hashlib.sha256(raw).hexdigest()==page['hash'], 'Journal hash mismatch')
            items=json.loads(raw);require(len(items)==page['count'], 'Journal count mismatch');messages.extend(items)
        require(len(messages)==value['history']['count'], 'Journal total mismatch')
        value['messages']=messages;value.pop('history')
    return value


def check_cartridge(path: Path, revision: int) -> None:
    content = path.read_text(encoding="ascii")
    require(content.startswith("pico-8 cartridge // http://www.pico-8.com\nversion 42\n__lua__\n"), "Invalid P8 header")
    require(f"workshop-fixture-revision-{revision}" in content, "Menu cartridge contains the wrong Lua revision")
    lua, gfx = content.split("__lua__\n", 1)[1].split("__gfx__\n", 1)
    require(lua.rstrip() == lua_for_revision(revision).rstrip(), "Published Lua differs from the model's complete program")
    require(all(re.search(r"function\s+" + name + r"\s*\(", lua) for name in ("_init", "_update", "_draw")), "Missing Lua entry points")
    rows = gfx.split("\n__", 1)[0].rstrip().splitlines()
    require(len(rows) == 128 and all(re.fullmatch(r"[0-9a-f]{128}", row) for row in rows), "Invalid 128×128 sprite section")
    if "\n__sfx__\n" in content:
        sounds=content.split("\n__sfx__\n",1)[1].split("\n__music__\n",1)[0].splitlines()
        patterns=content.split("\n__music__\n",1)[1].splitlines()
        require(len(sounds)==64 and all(re.fullmatch(r"[0-9a-f]{168}",row) for row in sounds),"Invalid tracker SFX section")
        require(len(patterns)==64 and all(re.fullmatch(r"[0-9a-f]{2} [0-9a-f]{8}",row) for row in patterns),"Invalid music section")


def check_project(data: Path, roms: Path, project_id: str, revision: int, draft: dict | None = None) -> dict:
    projects = list((data / "projects").glob("*.json"))
    require(len(projects) == 1, "One project must remain one game after reopening")
    project = json_file(projects[0])
    require(projects[0].name == project_id + ".json" and project.get("id") == project_id, "Project filename/identity changed")
    require(project.get("schema") == 1 and project.get("revision") == revision, "Unexpected project schema/revision")
    require(project.get("title") == (draft["title"] if draft else title_for_revision(revision)), "Project title did not update or its draft was lost")
    require(project.get("lua") == (draft["lua"] if draft else lua_for_revision(revision)), "Project did not retain the latest Lua/draft")
    require("pending" not in project, "A completed publication left a pending transaction")
    require([v["revision"] for v in project.get("versions", [])] == list(range(0, revision + 1)), "Version history was lost or duplicated")
    for version in project["versions"]:
        if version["revision"]==0:
            require(project.get("scaffoldReady"), "Missing playable scaffold flag")
            continue
        require(version["lua"] == lua_for_revision(version["revision"]), "Historical Lua version changed")
    messages = project.get("messages", [])
    require(sum(m.get("role") == "user" for m in messages) == 2, "Session did not retain both user requests")
    require(len(messages) == 24, "Expected two complete twelve-message Pi conversations")
    assistant_messages = [m for m in messages if m.get("role") == "assistant"]
    require(not any(m.get("stopReason") in ("error", "aborted") for m in assistant_messages), "A model turn failed or was interrupted")
    summaries = [m for m in assistant_messages if m.get("stopReason") == "stop"]
    require(len(summaries) == 2 and all(any(block.get("type") == "text" and re.search(r"[\u4e00-\u9fff]", block.get("text", "")) for block in m.get("content", [])) for m in summaries), "Both requests must finish with a persisted Chinese response")
    results = [m for m in messages if m.get("role") == "toolResult"]
    require(len(results) == 10, "Session lost part of the two complete tool traces")
    require([m.get("toolName") for m in results] == ["read_game", "read_skill", "write_game", "check_game", "publish_game"] * 2, "Tools did not run in the expected sequential order")
    require(not any(m.get("isError") for m in results), "A project tool failed")
    called_ids = {block["id"] for message in messages if message.get("role") == "assistant"
                  for block in message.get("content", []) if block.get("type") == "toolCall"}
    require(called_ids == {m["toolCallId"] for m in results}, "Saved transcript has missing or unpaired tool calls")
    require(FAKE_KEY not in json.dumps(messages, ensure_ascii=False), "Saved session contains the model credential")
    cartridges = list(roms.glob("*.p8"))
    require([p.name for p in cartridges] == [project_id + ".p8"], "A revision created an extra menu game")
    check_cartridge(cartridges[0], revision)
    mapping = (roms / "map.txt").read_text(encoding="utf-8").splitlines()
    require(mapping == [project_id + ".p8\t" + title_for_revision(revision)], "Menu mapping duplicated or changed its stable filename")
    require(not list(roms.glob(".workshop-*")), "Publication left a temporary ROM/menu file")
    return project


def check_theme(path: Path) -> dict:
    image = path.read_bytes()
    match = re.match(rb"P6\n(\d+) (\d+)\n255\n", image)
    require(match is not None, "Host did not produce its P6 frame dump")
    width, height = map(int, match.groups())
    require((width, height) == (1024, 768), "Unexpected PocketJS viewport")
    pixels = image[match.end():]
    require(len(pixels) == width * height * 3, "Truncated frame dump")
    x, y = 4, height - 4
    rgb = tuple(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3])
    require(rgb == BACKGROUND, f"Background pixel {x},{y} is {rgb}; expected settings color7 {BACKGROUND}")
    return {"width": width, "height": height, "background_pixel": {"x": x, "y": y, "rgb": rgb}}


def run_host(host: Path, mode: str, dump: Path, environment: dict[str, str], artifact: Path, timeout: float, log_label: str | None = None) -> None:
    result = subprocess.run([str(host), mode, "--dump", str(dump)], cwd=host.parent,
                            env=environment, capture_output=True, text=True, timeout=timeout)
    label = log_label or mode.lstrip("-")
    (artifact / (label + ".stdout.log")).write_text(result.stdout, encoding="utf-8")
    (artifact / (label + ".stderr.log")).write_text(result.stderr, encoding="utf-8")
    require(FAKE_KEY not in result.stdout + result.stderr, "Host logged the model credential")
    require(result.returncode == 0, f"Host {mode} failed ({result.returncode}):\n{result.stdout}\n{result.stderr}")
    require(dump.is_file(), "Host succeeded without writing its requested frame dump")



def executable(path: Path, source: str) -> None:
    path.write_text(source, encoding="utf-8")
    path.chmod(0o700)


def json_lines(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()] if path.exists() else []


def nextui_tmp_snapshot(path: Path):
    if not path.exists() and not path.is_symlink():
        return None
    stat = path.lstat()
    content = path.read_bytes() if path.is_file() else os.readlink(path) if path.is_symlink() else None
    return stat.st_mode, stat.st_ino, stat.st_size, stat.st_mtime_ns, content


def check_launchers(fixture: Path, project_id: str, lua: str, timeout: float) -> dict:
    # The authentic wrapper removes the device's resume marker. Respect any
    # existing host file rather than using the user's /tmp as a test fixture.
    markers = [Path("/tmp/resume_slot.txt"), Path("/tmp/poweroff"), Path("/tmp/reboot")]
    present = [str(p) for p in markers if p.exists() or p.is_symlink()]
    if present:
        return {"skipped": True, "reason": "Existing device lifecycle marker: " + ", ".join(present)}
    next_path = Path("/tmp/next")
    next_before = nextui_tmp_snapshot(next_path)
    source = REPOSITORY / "ports/pocketjs-brick/workshop/launch.sh"
    p8_source = REPOSITORY / "skeleton/EXTRAS/Emus/tg5040/P8.pak"
    require(source.is_file() and (p8_source / "launch.sh").is_file(), "Authentic launcher source missing")
    cases = [
        {"name": "sd-priority", "sd": "complete", "system": True, "minarch": 0, "ui": 0, "rom": True, "runner": "sd", "direct": True},
        {"name": "system-fallback", "sd": "missing", "system": True, "minarch": 0, "ui": 0, "rom": True, "runner": "system", "direct": True},
        {"name": "sd-incomplete", "sd": "incomplete", "system": True, "minarch": 0, "ui": 0, "rom": True, "runner": None, "error": "检查 P8"},
        {"name": "emulator-error", "sd": "complete", "system": True, "minarch": 37, "ui": 0, "rom": True, "runner": "sd", "error": "退出异常"},
        {"name": "ui-error", "sd": "complete", "system": True, "minarch": 0, "ui": 7, "rom": True, "runner": None},
        {"name": "missing-rom", "sd": "complete", "system": True, "minarch": 0, "ui": 0, "rom": False, "runner": None, "error": "游戏文件"},
        {"name": "no-runner", "sd": "missing", "system": False, "minarch": 0, "ui": 0, "rom": True, "runner": None, "error": "检查 P8"},
    ]
    results = []
    for case in cases:
        base = fixture / "launcher" / case["name"]
        sd = base / "sd"
        system = sd / ".system" / "tg5040"
        system_bin = system / "bin"
        system_bin.mkdir(parents=True)
        shared = sd / ".userdata" / "shared"
        userdata = sd / ".userdata" / "tg5040"
        data = shared / "ai-game-workshop"
        rom_directory = sd / "Roms" / "AI生成游戏 (P8)"
        rom_directory.mkdir(parents=True)
        rom = rom_directory / (project_id + ".p8")
        if case["rom"]:
            rom.write_text(cartridge(lua), encoding="ascii")
        tool = sd / "Tools" / "tg5040" / "AI Game Workshop.pak"
        tool.mkdir(parents=True)
        shutil.copy2(source, tool / "launch.sh")
        ui_log, minarch_log, timing_log = base / "ui.jsonl", base / "minarch.jsonl", base / "gametime.jsonl"
        counter = base / "ui-count.txt"
        executable(tool / "pocketjs-app.elf", """#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
counter=Path(os.environ['WORKSHOP_FIXTURE_UI_COUNTER'])
count=int(counter.read_text())+1 if counter.exists() else 1
counter.write_text(str(count))
data=Path(os.environ['POCKETJS_DATA'])
last=data/'last-project.txt'
record={'run':count,'last_project':last.read_text() if last.exists() else None,'runner':os.environ.get('POCKETJS_P8_LAUNCH'),'data':str(data),'roms':os.environ['POCKETJS_ROMS']}
with Path(os.environ['WORKSHOP_FIXTURE_UI_LOG']).open('a',encoding='utf-8') as f: f.write(json.dumps(record,ensure_ascii=False)+'\\n')
if count==1:
 (data/'play-request.txt').write_text(os.environ['WORKSHOP_FIXTURE_PROJECT_ID'])
 last.write_text(os.environ['WORKSHOP_FIXTURE_PROJECT_ID'])
sys.exit(int(os.environ['WORKSHOP_FIXTURE_UI_STATUS']))
""".replace("'\\\\n'", "'\\n'"))
        executable(system_bin / "minarch.elf", """#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
with Path(os.environ['WORKSHOP_FIXTURE_MINARCH_LOG']).open('a',encoding='utf-8') as f:
 f.write(json.dumps({'argv':sys.argv[1:],'cwd':os.getcwd()},ensure_ascii=False)+'\\n')
sys.exit(int(os.environ['WORKSHOP_FIXTURE_MINARCH_STATUS']))
""".replace("'\\\\n'", "'\\n'"))
        executable(system_bin / "gametimectl.elf", """#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
with Path(os.environ['WORKSHOP_FIXTURE_TIMING_LOG']).open('a',encoding='utf-8') as f: f.write(json.dumps({'argv':sys.argv[1:]},ensure_ascii=False)+'\\n')
""".replace("'\\\\n'", "'\\n'"))
        sd_p8 = sd / "Emus" / "tg5040" / "P8.pak"
        system_p8 = system / "paks" / "Emus" / "P8.pak"
        for directory, complete in [(sd_p8, case["sd"] == "complete"), (system_p8, bool(case["system"]))]:
            if (directory == sd_p8 and case["sd"] == "missing") or (directory == system_p8 and not case["system"]):
                continue
            directory.mkdir(parents=True)
            shutil.copy2(p8_source / "launch.sh", directory / "launch.sh")
            shutil.copy2(p8_source / "default.cfg", directory / "default.cfg")
            if complete:
                (directory / "fake08_libretro.so").write_bytes(b"fixture placeholder; never loaded\n")
        environment = dict(os.environ, PLATFORM="tg5040", SDCARD_PATH=str(sd), SYSTEM_PATH=str(system),
            USERDATA_PATH=str(userdata), SHARED_USERDATA_PATH=str(shared), BIOS_PATH=str(sd / "Bios"),
            SAVES_PATH=str(sd / "Saves"), CHEATS_PATH=str(sd / "Cheats"), LOGS_PATH=str(userdata / "logs"),
            PATH=str(system_bin) + os.pathsep + os.environ.get("PATH", ""),
            WORKSHOP_FIXTURE_UI_COUNTER=str(counter), WORKSHOP_FIXTURE_UI_LOG=str(ui_log),
            WORKSHOP_FIXTURE_MINARCH_LOG=str(minarch_log), WORKSHOP_FIXTURE_TIMING_LOG=str(timing_log),
            WORKSHOP_FIXTURE_PROJECT_ID=project_id, WORKSHOP_FIXTURE_UI_STATUS=str(case["ui"]),
            WORKSHOP_FIXTURE_MINARCH_STATUS=str(case["minarch"]))
        environment.pop("POCKETJS_P8_LAUNCH", None)
        launched = subprocess.run([str(tool / "launch.sh")], cwd=tool, env=environment,
                                  capture_output=True, text=True, timeout=timeout)
        (base / "launcher.stdout.log").write_text(launched.stdout, encoding="utf-8")
        (base / "launcher.stderr.log").write_text(launched.stderr, encoding="utf-8")
        require(launched.returncode == case["ui"], f"{case['name']}: wrapper did not preserve UI exit status")
        ui_runs = json_lines(ui_log)
        expected_runs = 1 if case["ui"] else 2
        require(len(ui_runs) == expected_runs, f"{case['name']}: expected {expected_runs} UI process(es), got {len(ui_runs)}")
        if expected_runs == 2:
            require(ui_runs[1]["last_project"] == project_id, f"{case['name']}: second UI lost its project selection")
        calls = json_lines(minarch_log)
        selected = sd_p8 if case["runner"] == "sd" else system_p8 if case["runner"] == "system" else None
        if selected:
            expected_arguments = [str(selected / "fake08_libretro.so"), str(rom)]
            require(len(calls) == 1 and calls[0]["argv"] == expected_arguments, f"{case['name']}: preview used a different core or ROM")
            require(all(run["runner"] == str(selected / "launch.sh") for run in ui_runs), f"{case['name']}: UI and wrapper chose different runners")
            require(json_lines(timing_log) == [{"argv": ["start", str(rom)]}, {"argv": ["stop", str(rom)]}], f"{case['name']}: preview timing boundaries are wrong")
            require(calls[0]["cwd"] == str(userdata), f"{case['name']}: authentic P8 launcher did not establish userdata cwd")
        else:
            require(not calls, f"{case['name']}: an invalid preview still launched an emulator")
            if case["sd"] == "incomplete" or not case["system"]:
                require(all(run["runner"] is None for run in ui_runs), f"{case['name']}: incomplete SD package fell back to another runner")
        launch_error = data / "launch-error.txt"
        if case.get("error"):
            require(launch_error.is_file() and case["error"] in launch_error.read_text(encoding="utf-8"), f"{case['name']}: missing user-visible launch failure")
        else:
            require(not launch_error.exists(), f"{case['name']}: successful preview left a launch error")
        if not case["ui"]:
            require(not (data / "play-request.txt").exists(), f"{case['name']}: preview request was not consumed")
        if case.get("direct"):
            # NextUI calls the same P8 package directly. It must receive the same
            # core/ROM arguments, with no workshop UI process in this route.
            direct = subprocess.run([str(selected / "launch.sh"), str(rom)], cwd=sd,
                                    env=environment, capture_output=True, text=True, timeout=timeout)
            require(direct.returncode == 0, f"{case['name']}: direct menu P8 launcher failed")
            require([value["argv"] for value in json_lines(minarch_log)] == [expected_arguments, expected_arguments], f"{case['name']}: NextUI route differs from preview route")
            require(len(json_lines(ui_log)) == 2, f"{case['name']}: direct menu launch unexpectedly restarted the workshop")
        results.append({"name": case["name"], "passed": True, "ui_processes": expected_runs,
            "preview_minarch_calls": len(calls), "ui_exit_status": launched.returncode,
            "simulated_emulator_status": case["minarch"], "direct_menu_checked": bool(case.get("direct"))})
    require(nextui_tmp_snapshot(next_path) == next_before, "Launcher modified NextUI's /tmp/next state")
    require(not any(p.exists() or p.is_symlink() for p in markers), "Launcher created a device lifecycle marker")
    return {"skipped": False, "passed": True, "cases": results, "nextui_tmp_next_unchanged": True,
            "real_emulator_executed": False}


def main() -> int:
    global LARGE_GAME_DATA
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", type=Path, required=True, help="Native PocketJS host beside brick-app.js/.pak")
    parser.add_argument("--output", type=Path, help="Keep fixtures, logs, HTTP trace, screenshots and report in this directory")
    parser.add_argument("--recover", action="store_true", help="Also check pending third publication recovery and retention of a later unpublished draft")
    parser.add_argument("--large-source", action="store_true", help="Exercise a >2000-line game with real tile data through publication/reopening")
    parser.add_argument("--timeout", type=float, default=120, help="Host timeout per phase, in seconds")
    args = parser.parse_args()
    if args.large_source:
        tiles = [f" {{x={i%50},y={i//50},c={11 if i==1999 else i%14+1},solid={'true' if i%7==0 else 'false'}}}," for i in range(2000)]
        LARGE_GAME_DATA = "terrain={\n" + "\n".join(tiles) + "\n}\n"
        require(len(lua_for_revision(1).splitlines()) > 2000, "Large game fixture must contain >2000 source lines")
        require(len(lua_for_revision(1).encode('ascii')) > 60000, "Large game fixture must also exceed 60KB")
    host = args.host.resolve()
    require(host.is_file() and os.access(host, os.X_OK), "--host must name an executable")
    require((host.parent / "brick-app.js").is_file() and (host.parent / "brick-app.pak").is_file(), "Host directory lacks brick-app.js/.pak")
    require((host.parent / "NotoSansSC-Regular.otf").is_file(), "Host directory lacks its Chinese fallback font")
    require(args.timeout > 0, "--timeout must be positive")
    if args.output:
        artifact = args.output.resolve()
        artifact.mkdir(parents=True, exist_ok=True)
    else:
        artifact = Path(tempfile.mkdtemp(prefix="workshop-verification-"))
    fixture = Path(tempfile.mkdtemp(prefix="fixture-", dir=artifact))
    shared = fixture / "sd" / ".userdata" / "shared"
    data = shared / "ai-game-workshop"
    roms = fixture / "sd" / "Roms" / "AI生成游戏 (P8)"
    data.mkdir(parents=True)
    roms.mkdir(parents=True)
    colors = ["f5f7eaff", "ffd177ff", "2f4256ff", "e6edf4ff", "172d3dff", "a6bacaff", "172d3dff"]
    (shared / "minuisettings.txt").write_text("".join(f'color{i}="0x{color}"\n' for i, color in enumerate(colors, 1)), encoding="ascii")
    runner = fixture / "fake-p8-launch.sh"
    runner.write_text("#!/bin/sh\nexit 0\n", encoding="ascii")
    runner.chmod(0o700)
    state = MockState()
    server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(state))
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    endpoint = f"http://127.0.0.1:{server.server_port}/v1"
    (data / "config.json").write_text(json.dumps({"key": FAKE_KEY, "endpoint": endpoint,
        "model": "deepseek-flash", "timeoutMs": 10000,
        **({"contextWindowTokens": 128000} if args.large_source else {})}), encoding="utf-8")
    environment = dict(os.environ, POCKETJS_WORKSHOP="1", POCKETJS_DATA=str(data), POCKETJS_ROMS=str(roms),
        POCKETJS_FONT=str(REPOSITORY / "fonts" / "font1.ttf"), POCKETJS_P8_LAUNCH=str(runner),
        SHARED_USERDATA_PATH=str(shared), NO_PROXY="127.0.0.1,localhost", no_proxy="127.0.0.1,localhost")
    # Tests must be isolated from any previously selected tool/acceptance mode.
    environment.pop("POCKETJS_TEST", None)
    success = False
    try:
        dump = artifact / "project-revision-2.ppm"
        run_host(host, "--workshop-test", dump, environment, artifact, args.timeout)
        require(not state.errors, "Mock request validation failed: " + "; ".join(state.errors))
        require(len(state.requests) == 12, f"Expected twelve model calls for two complete requests, got {len(state.requests)}")
        require(state.project_id is not None, "The agent never read its project identity")
        project_id = state.project_id
        project = check_project(data, roms, project_id, 2)
        require((data / "last-project.txt").read_text().strip() == project_id, "Reopen selection did not persist its project")
        require((data / "play-request.txt").read_text().strip() == project_id, "Preview request points to a different project")
        theme = check_theme(dump)
        report = {"passed": True, "project_id": project_id, "revision": 2, "model_http_calls": 12,
            "projects": 1, "menu_cartridges": 1, "user_messages": 2, "tool_results": 10,
            "source_lines": len(project["lua"].splitlines()), "source_bytes": len(project["lua"].encode("ascii")),
            "stable_menu_filename": project_id + ".p8", "theme": theme,
            "gameplay_tested": False, "real_model_called": False, "recovery_checked": False, "draft_retained": None}
        if args.recover:
            pending = {"revision": 3, "title": title_for_revision(3), "summary": "模拟断电后恢复提交", "lua": lua_for_revision(3)}
            # Legacy publication-intent fixture, deliberately without a modern
            # resumable model task. Modern phase/reply recovery is tested by
            # verify-state.mjs; it legitimately adds a recovery user message.
            project.pop('work',None)
            project.update(lua=pending["lua"], title=pending["title"], summary=pending["summary"], pending=pending)
            (data / "projects" / (project_id + ".json")).write_text(json.dumps(project, ensure_ascii=False), encoding="utf-8")
            # Simulate power loss between durable intent and final menu publication.
            (roms / (project_id + ".p8")).write_text(cartridge(lua_for_revision(1)), encoding="ascii")
            (roms / "map.txt").write_text(project_id + ".p8\t" + title_for_revision(1) + "\n", encoding="utf-8")
            calls_before = len(state.requests)
            recovered_dump = artifact / "project-revision-3-recovered.ppm"
            run_host(host, "--workshop-recover", recovered_dump, environment, artifact, args.timeout)
            project = check_project(data, roms, project_id, 3)
            require(len(state.requests) == calls_before, "Recovery unexpectedly called the model")
            check_theme(recovered_dump)
            report.update(recovery_checked=True, recovered_revision=3)
            # Reopening an ordinary saved draft must repair only the published
            # menu files; it must never overwrite unpublished project work.
            published_rom = (roms / (project_id + ".p8")).read_bytes()
            published_map = (roms / "map.txt").read_bytes()
            draft = {"lua": project["lua"] + "\n-- UNPUBLISHED_DRAFT\n", "title": "未发布草稿"}
            project.update(draft)
            (data / "projects" / (project_id + ".json")).write_text(json.dumps(project, ensure_ascii=False), encoding="utf-8")
            draft_dump = artifact / "project-revision-3-draft-retained.ppm"
            run_host(host, "--workshop-recover", draft_dump, environment, artifact, args.timeout, "workshop-draft-reopen")
            check_project(data, roms, project_id, 3, draft=draft)
            require(len(state.requests) == calls_before, "Reopening an unpublished draft unexpectedly called the model")
            require((roms / (project_id + ".p8")).read_bytes() == published_rom, "Reopening published the uncommitted draft Lua")
            require((roms / "map.txt").read_bytes() == published_map, "Reopening published the uncommitted draft title")
            require((data / "last-project.txt").read_text().strip() == project_id, "Draft reopen changed the active project identity")
            require((data / "play-request.txt").read_text().strip() == project_id, "Draft reopen changed the preview path identity")
            check_theme(draft_dump)
            report["draft_retained"] = True
        report["launcher"] = check_launchers(fixture, project_id, lua_for_revision(3 if args.recover else 2), args.timeout)
        (artifact / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        success = True
        launcher_status = "; isolated authentic launcher routes" if not report["launcher"].get("skipped") else "; launcher smoke skipped to preserve existing host lifecycle markers"
        print("PASS: real PocketJS/Pi/native tool loop; one project/ROM; reopen and revise; full saved tool traces; stable menu mapping; queued preview; NextUI color7" + launcher_status + ("; pending publication recovery and unpublished draft retention without model HTTP." if args.recover else "."))
        if args.output:
            print("Artifacts: " + str(artifact))
        return 0
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
        (artifact / "http-requests.json").write_text(json.dumps(state.requests, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if success and not args.output:
            shutil.rmtree(artifact)
        elif not success:
            print("Validation artifacts retained: " + str(artifact), file=sys.stderr)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        raise SystemExit(1)
