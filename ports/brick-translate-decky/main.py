import asyncio
import base64
import contextlib
import hashlib
import json
import math
import os
from pathlib import Path
import pwd
import secrets
import struct
import subprocess
import sys
import tempfile
import time
import urllib.parse

import decky_plugin
sys.path.insert(0, str(Path(__file__).resolve().parent))
from prompt import PROMPT
from ocr import OCR_PROMPT, parse_lines, bind_geometry

DEFAULTS = {
    "endpoint": "https://api.deepseek.com/chat/completions",
    "model": "deepseek-flash", "language": "简体中文", "image_width": 1280,
    "timeout": 40, "hold_seconds": 15, "font_scale": 1.0, "hotkey": True,
    "pipeline": "ocr", "ocr_model": "qwen-vl-ocr-2025-11-20",
    "ocr_width": 1280,
    "ocr_endpoint": "https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation",
}
COLORS = {"white", "black", "cyan", "yellow", "red", "blue", "green", "gray"}
KINDS = {"dialogue", "menu", "title", "label", "caption"}


def normalize_result(content):
    if not isinstance(content, str) or len(content) > 200000:
        raise ValueError("模型没有返回有效文字")
    text = content.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[-1].rsplit("```", 1)[0].strip()
    result = json.loads(text)
    if not isinstance(result, dict) or not isinstance(result.get("items"), list):
        raise ValueError("模型响应缺少 items")
    items = []
    for item in result["items"][:80]:
        if not isinstance(item, dict):
            continue
        box, zh = item.get("box"), item.get("zh")
        if not isinstance(zh, str) or not zh.strip() or not isinstance(box, list) or len(box) != 4:
            continue
        if isinstance(item.get("orig"), str) and item["orig"].strip() == zh.strip():
            continue
        if any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or not 0 <= v <= 1 for v in box):
            continue
        if box[0] >= box[2] or box[1] >= box[3]:
            continue
        items.append({"box": box, "zh": zh.strip()[:3000], "orig": str(item.get("orig", ""))[:3000],
                      "kind": item.get("kind") if isinstance(item.get("kind"), str) and item.get("kind") in KINDS else "label",
                      "color": item.get("color") if isinstance(item.get("color"), str) and item.get("color") in COLORS else "white",
                      "align": "center" if item.get("align") == "center" else "left"})
        source_height = item.get("source_height")
        if type(source_height) in (float, int) and math.isfinite(source_height) and 0 < source_height <= 1:
            items[-1]["source_height"] = source_height
    if result["items"] and not items and any(isinstance(item, dict) and item.get("orig") != item.get("zh") for item in result["items"]):
        raise ValueError("模型坐标无效，请重试或换用支持定位的视觉模型")
    return {"items": items, "note": str(result.get("note", ""))[:500],
            "context": str(result.get("context", ""))[:200], "terms": str(result.get("terms", ""))[:600]}


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix=".brick-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)


class Plugin:
    async def _main(self):
        self.directory = Path(os.environ.get("DECKY_PLUGIN_SETTINGS_DIR", decky_plugin.DECKY_PLUGIN_SETTINGS_DIR))
        self.settings_path = self.directory / "settings.json"
        self.settings = dict(DEFAULTS)
        try:
            saved = json.loads(self.settings_path.read_text())
            self.settings.update({k: v for k, v in saved.items() if k in DEFAULTS})
        except (OSError, ValueError, AttributeError):
            pass
        self.lock = asyncio.Lock()
        self.capture_process = None
        self.memories = {}
        self.results = {}
        self.last_frame = None
        self.hotkey_devices = {}
        self.dismiss_armed = False
        self.input_task = asyncio.create_task(self._input_loop())
        decky_plugin.logger.info("Brick Translate ready")

    def _key_path(self, endpoint=None):
        url = urllib.parse.urlparse(endpoint or self.settings["endpoint"])
        if url.hostname == "api.deepseek.com":
            name = "deepseek"
        elif url.hostname == "dashscope.aliyuncs.com":
            name = "bailian"
        else:
            name = hashlib.sha256((endpoint or self.settings["endpoint"]).encode()).hexdigest()[:24]
        return self.directory / ("api-key-" + name)

    def _key(self, endpoint=None):
        try:
            return self._key_path(endpoint).read_text().strip()
        except OSError:
            if urllib.parse.urlparse(endpoint or self.settings["endpoint"]).hostname == "dashscope.aliyuncs.com":
                with contextlib.suppress(OSError):
                    return (self.directory / "api-key").read_text().strip()
            return ""

    async def get_key_status(self, endpoint):
        return bool(self._key(endpoint))

    async def get_settings(self):
        return {**self.settings, "has_key": bool(self._key()), "has_ocr_key": bool(self._key(self.settings["ocr_endpoint"])),
                "input_devices": sum(state.get("controller", True) for state in self.hotkey_devices.values())}

    async def arm_dismiss(self, enabled):
        if not isinstance(enabled, bool):
            raise ValueError("输入关闭状态无效")
        self.dismiss_armed = False
        if enabled:
            import fcntl
            for state in self.hotkey_devices.values():
                try:
                    while True:
                        try:
                            if not os.read(state["fd"], 4096):
                                break
                        except BlockingIOError:
                            break
                    buffer = bytearray(96)
                    fcntl.ioctl(state["fd"], 0x80000000 | (len(buffer) << 16) | (ord("E") << 8) | 0x18, buffer)
                    state["ignored"] = {code for code in range(768) if buffer[code // 8] & (1 << (code % 8))}
                    state["keys"] = set(state["ignored"])
                    for code in (2, 5, 9, 10, 16, 17):
                        with contextlib.suppress(OSError):
                            axis = bytearray(24)
                            fcntl.ioctl(state["fd"], 0x80000000 | (24 << 16) | (ord("E") << 8) | (0x40 + code), axis)
                            current, minimum, maximum, _, _, _ = struct.unpack("6i", axis)
                            state["axes"][code] = {"value": current, "minimum": minimum, "maximum": maximum}
                except OSError:
                    state["ignored"] = set()
        self.dismiss_armed = enabled

    async def get_input_status(self):
        return {"armed": self.dismiss_armed, "devices": len(self.hotkey_devices)}

    async def _input_buttons(self, state, pressed):
        # Steam Input may consume evdev events. EVIOCGKEY still gives button state.
        previous = state.get("keys")
        state["keys"] = set(pressed)
        if previous is not None and self.dismiss_armed and (pressed - previous - state["ignored"]):
            self.dismiss_armed = False
            state["fired"] = True
            await decky_plugin.emit("dismiss_translation")

    async def _input_events(self, state, data, event_struct):
        for offset in range(0, len(data) - event_struct.size + 1, event_struct.size):
            _, _, event_type, code, value = event_struct.unpack_from(data, offset)
            if event_type == 3 and code in state.get("axes", {}):
                axis = state["axes"][code]
                previous = axis["value"]
                axis["value"] = value
                if code in (16, 17):
                    press = value != 0 and value != previous
                else:
                    threshold = axis["minimum"] + (axis["maximum"] - axis["minimum"]) * 0.2
                    press = previous <= threshold < value
                if press and self.dismiss_armed:
                    self.dismiss_armed = False
                    state["fired"] = True
                    await decky_plugin.emit("dismiss_translation")
                continue
            if event_type != 1 or not code:
                continue
            if value == 0:
                state["ignored"].discard(code)
            elif value == 1 and self.dismiss_armed and code not in state["ignored"]:
                self.dismiss_armed = False
                state["fired"] = True
                await decky_plugin.emit("dismiss_translation")

    async def save_settings(self, values):
        candidate = {**self.settings, **{k: v for k, v in values.items() if k in DEFAULTS}}
        for field in ("endpoint", "ocr_endpoint"):
            url = urllib.parse.urlparse(candidate[field])
            if url.scheme != "https" or not url.hostname or url.username or url.password:
                raise ValueError("接口地址必须是 HTTPS 地址")
        if candidate["pipeline"] not in ("ocr", "vision"):
            raise ValueError("翻译链路无效")
        if type(candidate["ocr_width"]) is not int or candidate["ocr_width"] not in (0, 768, 960, 1280, 1920):
            raise ValueError("OCR 宽度无效")
        for field in ("model", "ocr_model", "language"):
            if not isinstance(candidate[field], str) or not 0 < len(candidate[field].strip()) <= 160:
                raise ValueError("模型与目标语言不能为空")
        for field, low, high in (("image_width", 640, 1920), ("timeout", 10, 90), ("hold_seconds", 0, 120), ("font_scale", 0.75, 1.5)):
            value = candidate[field]
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
                raise ValueError("设置值超出范围")
        if not isinstance(candidate["hotkey"], bool):
            raise ValueError("快捷键设置必须为开关")
        atomic_json(self.settings_path, candidate)
        self.settings = candidate
        self.memories.clear()
        self.results.clear()
        return await self.get_settings()

    async def set_api_key(self, key, endpoint=None):
        if not isinstance(key, str) or len(key) > 4096 or any(c.isspace() for c in key.strip()):
            raise ValueError("API Key 格式无效")
        self.directory.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(dir=self.directory, prefix=".key-")
        try:
            with os.fdopen(fd, "w") as stream:
                stream.write(key.strip())
            os.replace(temporary, self._key_path(endpoint))
        finally:
            with contextlib.suppress(FileNotFoundError):
                os.unlink(temporary)
        return bool(self._key(endpoint))

    async def clear_memory(self):
        self.memories.clear()
        self.results.clear()

    async def capture(self):
        if self.lock.locked():
            raise RuntimeError("翻译正在进行")
        async with self.lock:
            return await self._capture()

    async def _capture(self):
        user = os.environ.get("DECKY_USER", pwd.getpwuid(os.getuid()).pw_name)
        uid = pwd.getpwnam(user).pw_uid
        runtime = f"/run/user/{uid}"
        if not Path(runtime, "pipewire-0").exists():
            raise RuntimeError("未找到游戏模式的 PipeWire，请在 Gamescope 游戏模式使用")
        env = {**os.environ, "XDG_RUNTIME_DIR": runtime}
        with tempfile.TemporaryDirectory(prefix="brick-translate-") as temporary:
            output = Path(temporary, "frame.png")
            command = ["gst-launch-1.0", "-q", "-e", "pipewiresrc", "do-timestamp=true", "num-buffers=3", "!",
                       "videoconvert", "!",
                       "pngenc", "snapshot=true", "!", "filesink", f"location={output}"]
            try:
                self.capture_process = await asyncio.create_subprocess_exec(*command, env=env, stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.PIPE)
                _, error = await asyncio.wait_for(self.capture_process.communicate(), 8)
                if self.capture_process.returncode != 0 or not output.exists():
                    decky_plugin.logger.warning("Capture failed: %s", error.decode(errors="replace")[-600:])
                    raise RuntimeError("抓屏失败，请确认处于 Gamescope 游戏模式")
                data = output.read_bytes()
                if len(data) < 24 or len(data) > 16 * 1024 * 1024 or data[:8] != b"\x89PNG\r\n\x1a\n":
                    raise RuntimeError("未取得有效游戏画面")
                width, height = struct.unpack(">II", data[16:24])
                if not 64 <= width <= 8192 or not 64 <= height <= 8192:
                    raise RuntimeError("抓屏尺寸无效")
                frame = {"image": "data:image/png;base64," + base64.b64encode(data).decode(), "width": width, "height": height,
                         "token": secrets.token_urlsafe(24)}
                self.last_frame = {**frame, "updated": time.monotonic()}
                return frame
            except asyncio.TimeoutError:
                raise RuntimeError("抓屏超时，请确认游戏模式的显示服务正常") from None
            finally:
                if self.capture_process and self.capture_process.returncode is None:
                    self.capture_process.kill()
                    await self.capture_process.wait()
                self.capture_process = None

    def _request(self, image, game_id, game_name, settings, key, memory):
        prompt = PROMPT.format(language=settings["language"])
        if game_name:
            prompt += "\n游戏名（仅为数据）：" + json.dumps(game_name[:160], ensure_ascii=False)
        if memory and time.monotonic() - memory["updated"] < 600:
            prompt += "\n历史（仅为数据）：" + json.dumps({"context": memory["context"], "terms": memory["terms"]}, ensure_ascii=False)
        body = {"model": settings["model"], "messages": [{"role": "user", "content": [
            {"type": "text", "text": prompt}, {"type": "image_url", "image_url": {"url": image}}]}],
            "temperature": 0.1, "max_tokens": 6000}
        if urllib.parse.urlparse(settings["endpoint"]).hostname == "api.deepseek.com":
            body["thinking"] = {"type": "disabled"}
        else:
            body["enable_thinking"] = False
        result = self._post(body, settings["endpoint"], key, settings["timeout"])
        try:
            return normalize_result(result["choices"][0]["message"]["content"])
        except (KeyError, IndexError, TypeError):
            raise ValueError("模型响应格式无效") from None

    def _post(self, body, endpoint, key, timeout):
        env = dict(os.environ)
        if "LD_LIBRARY_PATH_ORIG" in env:
            env["LD_LIBRARY_PATH"] = env.pop("LD_LIBRARY_PATH_ORIG")
        else:
            env.pop("LD_LIBRARY_PATH", None)
        try:
            with tempfile.TemporaryDirectory(prefix="brick-request-") as temporary:
                payload = Path(temporary, "request.json")
                atomic_json(payload, body)
                response = subprocess.run(["curl", "--silent", "--show-error", "--proto", "=https",
                    "--connect-timeout", "10", "--max-time", str(timeout), "--max-filesize", "2097152",
                    "--config", "-", "--header", "Content-Type: application/json", "--data-binary", "@" + str(payload),
                    "--write-out", "\n%{http_code}", endpoint],
                    input=("header = " + json.dumps("Authorization: Bearer " + key) + "\n").encode(),
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, timeout=timeout + 3)
                if response.returncode:
                    decky_plugin.logger.warning("Model transport failed: curl %d", response.returncode)
                    raise RuntimeError("模型连接失败或超时，请检查网络与接口区域")
                raw, status = response.stdout.rsplit(b"\n", 1)
                code = int(status)
                if code != 200:
                    message = {401: "API Key 无效", 403: "API Key 无权限或区域不匹配", 429: "请求限流或额度不足"}.get(code, f"模型接口错误 HTTP {code}")
                    raise RuntimeError(message)
                if len(raw) > 2 * 1024 * 1024:
                    raise ValueError("模型响应过大")
                result = json.loads(raw)
        except (subprocess.TimeoutExpired, FileNotFoundError):
            raise RuntimeError("模型连接失败或超时，请检查网络与接口区域") from None
        return result

    def _ocr_request(self, frame, game_name, settings, key, ocr_key, memory):
        if len(frame["image"]) > 10 * 1024 * 1024:
            raise ValueError("原始截图超过 OCR 送图限制，请降低显示分辨率或改用视觉链路")
        body = {"model": settings["ocr_model"], "input": {"messages": [{"role": "user", "content": [{
            "image": frame["image"], "min_pixels": 3072, "max_pixels": 8388608, "enable_rotate": False}]}]},
            "parameters": {"ocr_options": {"task": "advanced_recognition"}}}
        started = time.monotonic()
        response = self._post(body, settings["ocr_endpoint"], ocr_key, settings["timeout"])
        lines = parse_lines(response, frame["width"], frame["height"])
        ocr_seconds = round(time.monotonic() - started, 1)
        if not lines:
            return {"items": [], "note": "未识别到文字", "context": "", "terms": "", "ocr_seconds": ocr_seconds, "ocr_lines": 0}
        prompt = OCR_PROMPT.format(language=settings["language"]) + "\nData: " + json.dumps({
            "game": game_name[:160], "lines": [{**line, "box": [round(v, 3) for v in line["box"]]} for line in lines],
            "history": {k: memory[k] for k in ("context", "terms")} if memory else {}}, ensure_ascii=False)
        body = {"model": settings["model"], "messages": [{"role": "user", "content": prompt}],
                "temperature": 0.1, "max_tokens": 6000}
        if urllib.parse.urlparse(settings["endpoint"]).hostname == "api.deepseek.com":
            body["thinking"] = {"type": "disabled"}
        else:
            body["enable_thinking"] = False
        translation_started = time.monotonic()
        response = self._post(body, settings["endpoint"], key, settings["timeout"])
        try:
            choice = response["choices"][0]
            if choice.get("finish_reason") == "length":
                raise ValueError("译文被截断，请重试")
            result = normalize_result(bind_geometry(choice["message"]["content"], lines))
        except (KeyError, IndexError, TypeError):
            raise ValueError("翻译模型响应格式无效") from None
        return {**result, "ocr_seconds": ocr_seconds, "ocr_lines": len(lines),
                "translate_seconds": round(time.monotonic() - translation_started, 1), "ocr_width": frame["width"]}

    async def translate(self, image, game_id="", game_name="", capture_token=""):
        if self.lock.locked():
            raise RuntimeError("翻译正在进行")
        if not isinstance(image, str) or not image.startswith(("data:image/png;base64,", "data:image/jpeg;base64,")) or len(image) > 3 * 1024 * 1024:
            raise ValueError("截图格式无效")
        key = self._key()
        if not key:
            raise RuntimeError("请先设置翻译服务 API Key")
        async with self.lock:
            settings = dict(self.settings)
            identity = str(game_id)[:80] + ":" + settings["language"]
            memory = self.memories.get(identity) if game_id else None
            if memory and time.monotonic() - memory["updated"] >= 600:
                memory = None
            started = time.monotonic()
            if settings["pipeline"] == "ocr":
                frame = getattr(self, "last_frame", None)
                if not frame or capture_token != frame["token"] or time.monotonic() - frame["updated"] > 120:
                    raise ValueError("原始截图已过期，请重新抓屏")
                ocr_key = self._key(settings["ocr_endpoint"])
                if not ocr_key:
                    raise RuntimeError("请先设置 OCR 服务的百炼 API Key")
                if settings["ocr_width"]:
                    width = min(frame["width"], settings["ocr_width"])
                    frame = {**frame, "image": image, "width": width, "height": round(frame["height"] * width / frame["width"])}
                cache_key = hashlib.sha256((json.dumps(settings, sort_keys=True) + identity + frame["image"]).encode()).hexdigest()
                cached = self.results.get(cache_key)
                if cached and time.monotonic() - cached["updated"] < 60:
                    return {**cached["result"], "seconds": 0, "ocr_seconds": 0, "translate_seconds": 0, "cache_hit": True}
                result = await asyncio.to_thread(self._ocr_request, frame, game_name, settings, key, ocr_key, memory)
                self.results[cache_key] = {"updated": time.monotonic(), "result": result}
                if len(self.results) > 4:
                    self.results.pop(next(iter(self.results)))
            else:
                result = await asyncio.to_thread(self._request, image, game_id, game_name, settings, key, memory)
            decky_plugin.logger.info("Translation complete: %d items, %.1fs", len(result["items"]), time.monotonic() - started)
            if game_id:
                terms = list(dict.fromkeys(((memory or {}).get("terms", "") + result["terms"]).split(";")))
                self.memories[identity] = {"context": result["context"], "terms": ";".join(terms[-24:])[:1500], "updated": time.monotonic()}
                if len(self.memories) > 20:
                    self.memories.pop(next(iter(self.memories)))
            return {**result, "seconds": round(time.monotonic() - started, 1)}

    async def _input_loop(self):
        import fcntl
        event_struct = struct.Struct("llHHi")
        next_scan = 0
        word_bits = struct.calcsize("L") * 8
        available = set()
        while True:
            if time.monotonic() >= next_scan:
                next_scan = time.monotonic() + 3
                available = set()
                for device in Path("/sys/class/input").glob("event*"):
                    try:
                        bits = 0
                        for word in (device / "device/capabilities/key").read_text().split():
                            bits = (bits << word_bits) | int(word, 16)
                        controller = any(bits & (1 << code) for code in (288, 304, 317, 318))
                        keyboard = bool(bits & (1 << 30) and bits & (1 << 28))
                        if not controller and not keyboard:
                            continue
                        name = "/dev/input/" + device.name
                        available.add(name)
                        if name not in self.hotkey_devices:
                            fd = os.open(name, os.O_RDONLY | os.O_NONBLOCK)
                            self.hotkey_devices[name] = {"fd": fd, "started": None, "fired": False, "ignored": set(), "axes": {},
                                                         "controller": controller, "hotkey": bool(bits & (1 << 317) and bits & (1 << 318))}
                    except (OSError, ValueError):
                        continue
            for name in list(self.hotkey_devices):
                state = self.hotkey_devices[name]
                if name not in available:
                    os.close(state["fd"])
                    del self.hotkey_devices[name]
                    continue
                try:
                    # Query current state, avoiding missed releases/SYN_DROPPED.
                    buffer = bytearray(96)
                    fcntl.ioctl(state["fd"], 0x80000000 | (len(buffer) << 16) | (ord("E") << 8) | 0x18, buffer)
                    state["ignored"].intersection_update(code for code in range(768) if buffer[code // 8] & (1 << (code % 8)))
                    pressed_keys = {code for code in range(768) if buffer[code // 8] & (1 << (code % 8))}
                    await self._input_buttons(state, pressed_keys)
                    # Also poll d-pad and trigger axes when their event stream is consumed.
                    if self.dismiss_armed:
                        for code in state.get("axes", {}):
                            with contextlib.suppress(OSError):
                                axis_buffer = bytearray(24)
                                fcntl.ioctl(state["fd"], 0x80000000 | (24 << 16) | (ord("E") << 8) | (0x40 + code), axis_buffer)
                                value = struct.unpack("6i", axis_buffer)[0]
                                if value != state["axes"][code]["value"]:
                                    await self._input_events(state, event_struct.pack(0, 0, 3, code, value), event_struct)
                    pressed = all(code in pressed_keys for code in (317, 318))
                    with contextlib.suppress(BlockingIOError):
                        data = os.read(state["fd"], event_struct.size * 64)
                        await self._input_events(state, data, event_struct)
                    if pressed and state["hotkey"] and self.settings["hotkey"]:
                        if state["started"] is None:
                            state["started"] = time.monotonic()
                        elif not state["fired"] and time.monotonic() - state["started"] >= 0.7:
                            state["fired"] = True
                            await decky_plugin.emit("toggle_translation")
                    else:
                        state["started"], state["fired"] = None, False
                except OSError:
                    os.close(state["fd"])
                    del self.hotkey_devices[name]
            await asyncio.sleep(0.08)

    async def _unload(self):
        self.input_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await self.input_task
        for state in self.hotkey_devices.values():
            os.close(state["fd"])
        if self.capture_process and self.capture_process.returncode is None:
            self.capture_process.kill()
            await self.capture_process.wait()
