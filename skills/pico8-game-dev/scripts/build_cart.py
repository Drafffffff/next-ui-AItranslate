#!/usr/bin/env python3
"""Validate the workshop response and assemble a draft .p8; never run a game.

Development helper using Python's standard library. The handheld app must port
this contract to its native/PocketJS host; Python is not assumed on the device.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile

ROOT = Path(__file__).resolve().parent.parent
FIELDS = {"schema", "project_id", "base_revision", "title", "summary", "controls",
          "lua", "sprite_overrides"}
BUTTONS = {"left", "right", "up", "down", "o", "x"}
BLOCKED = {
    "flip", "_update60", "load", "reload", "cstore", "cartdata", "dget", "dset",
    "peek", "peek2", "peek4", "poke", "poke2", "poke4", "memcpy", "memset",
    "serial", "extcmd", "io", "os", "package", "debug", "require", "dofile",
    "loadfile", "loadstring",
}
SECTIONS = {"__lua__", "__gfx__", "__gff__", "__map__", "__sfx__",
            "__music__", "__label__"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_limited(path, maximum):
    with Path(path).open("rb") as handle:
        data = handle.read(maximum + 1)
    require(len(data) <= maximum, f"file exceeds {maximum} bytes: {path}")
    return data.decode("utf-8")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def text_field(value, label, minimum, maximum):
    require(isinstance(value, str) and minimum <= len(value) <= maximum,
            f"{label} must be a string of {minimum}..{maximum} characters")
    require(not any(ord(c) < 32 or ord(c) == 127 for c in value),
            f"{label} contains control characters")


def mask_lua_literals(source):
    """Mask strings/comments for limited profile checks, not a Lua parser."""
    result = list(source)
    i = 0
    while i < len(source):
        start = i
        comment = source.startswith("--", i)
        if comment:
            i += 2
        long = re.match(r"\[(=*)\[", source[i:])
        if long:
            closing = "]" + long.group(1) + "]"
            end = source.find(closing, i + len(long.group(0)))
            require(end >= 0, "unterminated Lua long string/comment")
            i = end + len(closing)
        elif comment:
            end = source.find("\n", i)
            i = len(source) if end < 0 else end
        elif source[i] in "\"'":
            quote = source[i]
            i += 1
            closed = False
            while i < len(source):
                if source[i] == "\\":
                    i += 2
                elif source[i] == quote:
                    i += 1
                    closed = True
                    break
                else:
                    i += 1
            require(closed, "unterminated Lua quoted string")
        else:
            i += 1
            continue
        for n in range(start, min(i, len(source))):
            if result[n] not in "\n\r":
                result[n] = " "
    return "".join(result)


def check_lua(source):
    require(isinstance(source, str) and len(source) > 0,
            "lua must contain non-empty ASCII source")
    require(source.isascii(), "lua must use ASCII; put Chinese in host metadata")
    require(not any(ord(c) < 32 and c not in "\n\r\t" or ord(c) == 127
                    for c in source), "lua contains unsupported control characters")
    require(not re.search(r"(?m)^__[a-z0-9_]+__\s*$", source),
            "lua cannot contain cartridge section markers")
    code = mask_lua_literals(source)
    require(not re.search(r"(?m)^\s*#\s*include\b", code), "#include is outside profile")
    blocked = set(re.findall(r"\b[A-Za-z_][A-Za-z_0-9]*\b", code)) & BLOCKED
    require(not blocked, "outside initial profile: " + ", ".join(sorted(blocked)))
    for callback in ("_init", "_update", "_draw"):
        matches = re.findall(r"\bfunction\s+" + callback + r"\s*\(\s*\)", code)
        require(len(matches) == 1, f"expected exactly one function {callback}()")


def validate_result(result, project_id, revision):
    require(isinstance(result, dict) and set(result) == FIELDS,
            "response must contain exactly the documented fields")
    require(result["schema"] == "pico8-game-result/v1", "unsupported response schema")
    require(re.fullmatch(r"[a-z0-9][a-z0-9-]{0,47}", project_id) is not None,
            "invalid host project id")
    require(result["project_id"] == project_id, "project identity mismatch")
    require(type(result["base_revision"]) is int and result["base_revision"] == revision
            and revision >= 0, "base revision mismatch")
    text_field(result["title"], "title", 1, 32)
    text_field(result["summary"], "summary", 1, 240)
    controls = result["controls"]
    require(isinstance(controls, list) and 1 <= len(controls) <= 8,
            "controls must contain 1..8 entries")
    for item in controls:
        require(isinstance(item, dict) and set(item) == {"button", "action"},
                "control must contain button and action")
        require(isinstance(item["button"], str) and item["button"] in BUTTONS,
                "unsupported control button")
        text_field(item["action"], "control action", 1, 64)
    check_lua(result["lua"])
    patches = result["sprite_overrides"]
    require(isinstance(patches, list) and len(patches) <= 32,
            "sprite_overrides must contain 0..32 entries")
    seen = set()
    for patch in patches:
        require(isinstance(patch, dict) and set(patch) == {"id", "pixels"},
                "sprite override must contain id and pixels")
        sprite_id = patch["id"]
        require(type(sprite_id) is int and 32 <= sprite_id <= 63,
                "custom sprite id must be 32..63")
        require(sprite_id not in seen, "duplicate sprite override")
        seen.add(sprite_id)
        pixels = patch["pixels"]
        require(isinstance(pixels, list) and len(pixels) == 8
                and all(isinstance(row, str) and re.fullmatch(r"[0-9a-f]{8}", row)
                        for row in pixels), "sprite pixels must be 8x8 lowercase hex")


def parse_cart(source):
    lines = source.splitlines()
    require(len(lines) >= 4 and lines[0] == "pico-8 cartridge // http://www.pico-8.com"
            and re.fullmatch(r"version [0-9]+", lines[1]), "invalid base cartridge header")
    sections = {}
    current = None
    for line in lines[2:]:
        if re.fullmatch(r"__[a-z0-9_]+__", line):
            require(line in SECTIONS and line not in sections,
                    f"unsupported or duplicate base section: {line}")
            current = line
            sections[current] = []
        else:
            require(current is not None, "data before first cartridge section")
            sections[current].append(line)
    require("__lua__" in sections and "__gfx__" in sections,
            "base cartridge requires lua and gfx sections")
    gfx = sections["__gfx__"]
    require(len(gfx) == 128 and all(re.fullmatch(r"[0-9a-f]{128}", row) for row in gfx),
            "base gfx must be 128x128 lowercase hex")
    return lines[:2], sections


def assemble(result, base):
    header, sections = parse_cart(base)
    gfx = [list(row) for row in sections["__gfx__"]]
    for patch in result["sprite_overrides"]:
        x, y = (patch["id"] % 16) * 8, (patch["id"] // 16) * 8
        for dy, row in enumerate(patch["pixels"]):
            gfx[y + dy][x:x + 8] = row
    sections["__gfx__"] = ["".join(row) for row in gfx]
    sections["__lua__"] = result["lua"].replace("\r\n", "\n").replace("\r", "\n").splitlines()
    lines = list(header)
    for name, data in sections.items():
        lines.append(name)
        lines.extend(data)
    return "\n".join(lines) + "\n"


def atomic_write(path, source):
    path = Path(path)
    require(path.suffix == ".p8", "output must have .p8 extension")
    require(path.parent.is_dir(), "output parent directory must already exist")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix="." + path.name + ".",
                                         suffix=".tmp", delete=False, mode="w",
                                         encoding="utf-8", newline="\n") as handle:
            temporary = Path(handle.name)
            handle.write(source)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--result", type=Path, required=True)
    parser.add_argument("--project-id", required=True)
    parser.add_argument("--base-revision", type=int, required=True)
    parser.add_argument("--base-cart", type=Path, default=ROOT / "assets/starter.p8")
    parser.add_argument("--output", type=Path, required=True, help="draft .p8 path")
    args = parser.parse_args()
    try:
        result = json.loads(read_limited(args.result, 262144), object_pairs_hook=unique_object)
        validate_result(result, args.project_id, args.base_revision)
        source = assemble(result, read_limited(args.base_cart, 524288))
        atomic_write(args.output, source)
    except (ValueError, OSError, UnicodeError) as error:
        parser.exit(2, f"rejected: {error}\n")
    print(json.dumps({"status": "format_checked", "runtime_tested": False,
                      "project_id": args.project_id, "base_revision": args.base_revision,
                      "output": str(args.output),
                      "sha256": hashlib.sha256(source.encode("utf-8")).hexdigest()},
                     ensure_ascii=False))


if __name__ == "__main__":
    main()
