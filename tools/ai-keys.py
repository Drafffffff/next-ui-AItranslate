#!/usr/bin/env python3
"""Migrate existing Brick credentials without printing or executing them."""
import argparse
import datetime
import json
import os
from pathlib import Path
import shlex
import subprocess

NAMES = ("DEEPSEEK_API_KEY", "DASHSCOPE_API_KEY", "CUSTOM_API_KEY")
SOURCES = ("ai-translate.txt", "voice-ai.txt", "pocketjs-brick/app/config.json", "ai-game-workshop/config.json")


class MigrationError(ValueError):
    """Only canonical field names / source filenames, never raw input."""
    pass


def parse(raw):
    if len(raw) > 16384 or b"\0" in raw:
        raise MigrationError("公共配置文件过大或包含无效字符")
    result = {}
    for line in raw.decode("utf-8-sig").split("\n"):
        if len(line.encode()) > 1022:
            raise MigrationError("公共配置行过长")
        if line.lstrip().startswith(("#", ";")) or "=" not in line:
            continue
        name, value = (part.strip(" \t\r") for part in line.split("=", 1))
        if name not in NAMES:
            continue
        if name in result:
            raise MigrationError("公共配置包含重复字段: " + name)
        if len(value) > 511 or any(ord(c) < 33 or ord(c) > 126 for c in value):
            raise MigrationError("Key 格式无效: " + name)
        result[name] = value
    return result


def legacy(raw, filename):
    if filename.endswith(".json"):
        return json.loads(raw)
    return {k.strip(): v.strip() for line in raw.decode("utf-8-sig").splitlines()
            if "=" in line for k, v in [line.split("=", 1)]}


def collect(read):
    candidates = {name: [] for name in NAMES}
    for source in SOURCES:
        raw = read(source)
        if raw is None:
            continue
        try:
            data = legacy(raw, source)
        except (ValueError, UnicodeError):
            raise MigrationError("旧配置无法解析: " + source) from None
        pairs = []
        if source == "ai-translate.txt":
            pairs = [("aiDeepseekKey", NAMES[0]), ("aiBailianKey", NAMES[1]), ("aiCustomKey", NAMES[2])]
            target = {"0": NAMES[1], "1": NAMES[0], "2": NAMES[2]}.get(str(data.get("aiProvider", "1")))
            if target and not any(data.get(k) for k, n in pairs if n == target):
                pairs.append(("aiApiKey", target))
        elif source == "voice-ai.txt":
            endpoint = data.get("voiceChatEndpoint", "https://api.deepseek.com")
            provider = NAMES[0] if endpoint == "https://api.deepseek.com" or endpoint.startswith("https://api.deepseek.com/") else NAMES[1] if endpoint == "https://dashscope.aliyuncs.com" or endpoint.startswith("https://dashscope.aliyuncs.com/") else None
            pairs = [("voiceAsrKey", NAMES[1])]
            if provider:
                pairs.append(("voiceChatKey", provider))
        else:
            endpoint = data.get("endpoint", data.get("baseUrl", "https://api.deepseek.com"))
            if endpoint == "https://api.deepseek.com" or endpoint.startswith("https://api.deepseek.com/"):
                pairs.append(("key", NAMES[0]))
            pairs.append(("asrKey", NAMES[1]))
        for field, name in pairs:
            value = data.get(field, "")
            if isinstance(value, str) and value.strip():
                value = value.strip()
                parse((name + "=" + value).encode())
                candidates[name].append((source + ":" + field, value))
    return candidates


def migrate(read):
    original = read("ai-keys.txt")
    current = parse(original) if original is not None else {}
    added = []
    for name, items in collect(read).items():
        if current.get(name):
            continue
        values = {value for _, value in items}
        if len(values) > 1:
            raise MigrationError(name + " 在旧配置中不一致: " + ", ".join(source for source, _ in items))
        if values:
            current[name] = values.pop()
            added.append(name)
    # Keep comments and unknown fields in an existing shared file.
    lines = (original.decode("utf-8-sig").splitlines() if original is not None else ["# NextUI AI 应用公共 Key；按字面读取，不需要引号。"])
    for name in added:
        lines = [line for line in lines if line.split("=", 1)[0].strip() != name]
        lines.append(name + "=" + current[name])
    for name in NAMES[:2]:
        if name not in current:
            lines.append(name + "=")
    return original, ("\n".join(lines) + "\n").encode(), current, added


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    location = cli.add_mutually_exclusive_group(required=True)
    location.add_argument("--ssh", metavar="ALIAS")
    location.add_argument("--sd-root", type=Path)
    cli.add_argument("--apply", action="store_true", help="write the migrated shared file; default checks only")
    args = cli.parse_args()
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    root = "/mnt/SDCARD/.userdata/shared"

    def ssh(command, payload=None):
        p = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=6", args.ssh, command], input=payload, capture_output=True)
        if p.returncode:
            raise MigrationError("SSH 操作失败，请检查连接")
        return p.stdout

    def read(name):
        if args.ssh:
            path = shlex.quote(root + "/" + name)
            raw = ssh("if [ -f " + path + " ]; then printf '1'; cat " + path + "; else printf '0'; fi")
            return raw[1:] if raw[:1] == b"1" else None
        path = args.sd_root / ".userdata/shared" / name
        return path.read_bytes() if path.exists() else None

    try:
        original, updated, current, added = migrate(read)
        for name in NAMES:
            print(name + ": " + ("已配置" if current.get(name) else "未配置"))
        if not args.apply:
            print("检查完成；使用 --apply 写入。")
            return
        if original is not None and not added:
            print("公共配置已存在，无需修改。")
            return
        if args.ssh:
            target = shlex.quote(root + "/ai-keys.txt")
            stage = shlex.quote(root + "/.ai-keys-" + stamp + ".tmp")
            backup = shlex.quote(root + "/ai-key-backups/" + stamp)
            ssh("umask 077; mkdir -p " + backup)
            for name in ("ai-keys.txt",) + SOURCES:
                source = shlex.quote(root + "/" + name)
                dest = shlex.quote(root + "/ai-key-backups/" + stamp + "/" + name.replace("/", "_"))
                ssh("if [ -f " + source + " ]; then cp " + source + " " + dest + "; fi")
            ssh("umask 077; cat > " + stage + " && chmod 600 " + stage + " && mv " + stage + " " + target + " && sync", updated)
        else:
            shared = args.sd_root / ".userdata/shared"
            shared.mkdir(parents=True, exist_ok=True)
            backup = shared / "ai-key-backups" / stamp
            backup.mkdir(parents=True, mode=0o700)
            for name in ("ai-keys.txt",) + SOURCES:
                raw = read(name)
                if raw is not None:
                    path = backup / name.replace("/", "_")
                    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                    with os.fdopen(fd, "wb") as f:
                        f.write(raw)
            stage = shared / (".ai-keys-" + stamp + ".tmp")
            fd = os.open(stage, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, "wb") as f:
                f.write(updated)
                f.flush()
                os.fsync(f.fileno())
            os.replace(stage, shared / "ai-keys.txt")
        assert parse(read("ai-keys.txt")) == {**current, **{n: "" for n in NAMES[:2] if n not in current}}
        print("已写入 .userdata/shared/ai-keys.txt；旧配置已备份并保留。")
    except MigrationError as error:
        print("迁移未完成：" + str(error))
        raise SystemExit(1)
    except (ValueError, OSError, UnicodeError, TypeError, AttributeError):
        # Do not include arbitrary exception text: JSON/OS errors can contain credentials.
        print("迁移未完成：连接、配置格式或旧 Key 冲突，请检查源文件。")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
