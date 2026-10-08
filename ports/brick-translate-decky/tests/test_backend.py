import asyncio
import importlib.util
import json
import logging
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import time
import types
import unittest
from unittest.mock import AsyncMock, patch

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root))
sys.modules["decky_plugin"] = types.SimpleNamespace(DECKY_PLUGIN_SETTINGS_DIR="", logger=logging.getLogger("test"), emit=None)
spec = importlib.util.spec_from_file_location("brick_backend", root / "main.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


class ResponseTests(unittest.TestCase):
    def test_rejects_wrong_coordinate_spaces_and_nonfinite(self):
        for box in ([0, 0, 1000, 1000], [0, 0, float("nan"), 1], [0, 0, True, 1], [0.6, 0, 0.2, 1]):
            with self.assertRaises(ValueError):
                backend.normalize_result(json.dumps({"items": [{"zh": "译文", "box": box}]}))

    def test_complete_dialogue_and_untrusted_metadata(self):
        text = "这是跨行的完整对白，不能截掉后半句。"
        result = backend.normalize_result("```json\n" + json.dumps({"note": "items []", "items": [
            {"zh": text, "box": [0.1, 0.2, 0.9, 0.4], "kind": [], "color": {}, "align": "<script>"},
            {"zh": "坏坐标", "box": [50, 10, 100, 20]}]}) + "\n```")
        self.assertEqual(len(result["items"]), 1)
        self.assertEqual(result["items"][0]["zh"], text)
        self.assertEqual(result["items"][0]["align"], "left")

    def test_empty_text_is_a_valid_result(self):
        self.assertEqual(backend.normalize_result('{"items":[]}')["items"], [])

    def test_identical_text_does_not_create_duplicate_overlay(self):
        result = backend.normalize_result(json.dumps({"items": [{"orig": "开始游戏", "zh": "开始游戏", "box": [0, 0, 0.3, 0.1]}]}))
        self.assertEqual(result["items"], [])


class BackendTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.plugin = backend.Plugin()
        self.plugin.directory = Path(self.temporary.name)
        self.plugin.settings_path = self.plugin.directory / "settings.json"
        self.plugin.settings = dict(backend.DEFAULTS)
        self.plugin.settings["pipeline"] = "vision"
        self.plugin.memories = {}
        self.plugin.results = {}
        self.plugin.hotkey_devices = {}
        self.plugin.dismiss_armed = False
        self.plugin.lock = asyncio.Lock()

    async def asyncTearDown(self):
        self.temporary.cleanup()

    async def test_key_is_private_and_not_returned(self):
        await self.plugin.set_api_key("test-secret")
        self.assertEqual(stat.S_IMODE(self.plugin._key_path().stat().st_mode), 0o600)
        settings = await self.plugin.get_settings()
        self.assertTrue(settings["has_key"])
        self.assertNotIn("test-secret", json.dumps(settings))

    async def test_invalid_settings_do_not_replace_saved_config(self):
        await self.plugin.save_settings({"hold_seconds": 0})
        before = self.plugin.settings_path.read_bytes()
        for change in ({"endpoint": "http://example.com"}, {"font_scale": float("nan")}, {"hotkey": "yes"}, {"image_width": 1}):
            with self.assertRaises(ValueError):
                await self.plugin.save_settings(change)
            self.assertEqual(before, self.plugin.settings_path.read_bytes())

    async def test_provider_keys_never_cross_endpoints(self):
        await self.plugin.set_api_key("deepseek-secret")
        self.plugin.settings["endpoint"] = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"
        self.assertFalse((await self.plugin.get_settings())["has_key"])
        await self.plugin.set_api_key("bailian-secret")
        self.plugin.settings["endpoint"] = "https://api.deepseek.com/chat/completions"
        self.assertEqual(self.plugin._key(), "deepseek-secret")

    async def test_parallel_translation_is_rejected(self):
        await self.plugin.lock.acquire()
        try:
            with self.assertRaisesRegex(RuntimeError, "正在进行"):
                await self.plugin.translate("data:image/png;base64,a")
        finally:
            self.plugin.lock.release()

    async def test_context_is_isolated_by_game_and_language(self):
        await self.plugin.set_api_key("test-key")
        seen = []
        def request(image, game_id, game_name, settings, key, memory):
            seen.append(memory)
            return {"items": [], "note": "", "context": "人物甲", "terms": "Alice=爱丽丝;"}
        with patch.object(self.plugin, "_request", side_effect=request):
            for game in ("1", "2", "1"):
                await self.plugin.translate("data:image/png;base64,a", game, "game")
            self.plugin.settings["language"] = "English"
            await self.plugin.translate("data:image/png;base64,a", "1", "game")
        self.assertIsNone(seen[0])
        self.assertIsNone(seen[1])
        self.assertEqual(seen[2]["context"], "人物甲")
        self.assertIsNone(seen[3])

    async def test_curl_keeps_secret_out_of_arguments_and_disables_thinking(self):
        def curl(command, **options):
            self.assertNotIn("secret-test-key", " ".join(command))
            self.assertNotIn("--location", command)
            self.assertIn(b"secret-test-key", options["input"])
            self.assertIn("--max-time", command)
            data = json.loads(Path(command[command.index("--data-binary") + 1][1:]).read_text())
            self.assertEqual(data["thinking"], {"type": "disabled"})
            self.assertNotIn("enable_thinking", data)
            self.assertEqual(stat.S_IMODE(Path(command[command.index("--data-binary") + 1][1:]).stat().st_mode), 0o600)
            body = {"choices": [{"message": {"content": '{"items":[]}'}}]}
            return subprocess.CompletedProcess(command, 0, json.dumps(body).encode() + b"\n200", b"")
        with patch.object(backend.subprocess, "run", side_effect=curl):
            self.plugin._request("data:image/png;base64,a", "", "", self.plugin.settings, "secret-test-key", None)

    async def test_redirect_is_an_error_not_a_second_request(self):
        with patch.object(backend.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, b"redirect\n302", b"")) as run:
            with self.assertRaisesRegex(RuntimeError, "302"):
                self.plugin._request("data:image/png;base64,a", "", "", self.plugin.settings, "secret", None)
            self.assertEqual(run.call_count, 1)

    async def test_ocr_pipeline_uses_full_frame_and_separate_keys(self):
        await self.plugin.set_api_key("translation-secret")
        await self.plugin.set_api_key("ocr-secret", self.plugin.settings["ocr_endpoint"])
        self.plugin.settings["pipeline"] = "ocr"
        self.plugin.settings["ocr_width"] = 0
        self.plugin.last_frame = {"token": "capture-token", "updated": time.monotonic(),
                                  "image": "data:image/png;base64,FULL", "width": 3840, "height": 2160}
        seen = []
        def post(body, endpoint, key, timeout):
            seen.append((body, endpoint, key))
            if len(seen) == 1:
                return {"output": {"choices": [{"message": {"content": [{"ocr_result": {"words_info": [
                    {"text": "Start", "location": [100, 100, 300, 100, 300, 150, 100, 150]}]}}]}}]}}
            return {"choices": [{"message": {"content": '{"items":[{"ids":[0],"zh":"开始"}]}'}}]}
        with patch.object(self.plugin, "_post", side_effect=post):
            result = await self.plugin.translate("data:image/png;base64,SMALL", capture_token="capture-token")
        self.assertEqual(seen[0][2], "ocr-secret")
        self.assertEqual(seen[0][0]["input"]["messages"][0]["content"][0]["image"], self.plugin.last_frame["image"])
        self.assertEqual(seen[0][0]["parameters"]["ocr_options"]["task"], "advanced_recognition")
        self.assertEqual(seen[1][2], "translation-secret")
        self.assertNotIn("data:image", json.dumps(seen[1][0]))
        self.assertAlmostEqual(result["items"][0]["source_height"], 50 / 2160)
        with patch.object(self.plugin, "_post", side_effect=AssertionError("Repeated frame should not call cloud")):
            cached = await self.plugin.translate("data:image/png;base64,SMALL", capture_token="capture-token")
        self.assertTrue(cached["cache_hit"])
        self.assertEqual(cached["seconds"], 0)
        await self.plugin.clear_memory()
        self.assertFalse(self.plugin.results)

    async def test_ocr_capture_token_is_required_and_expires(self):
        await self.plugin.set_api_key("translation-secret")
        self.plugin.settings["pipeline"] = "ocr"
        self.plugin.last_frame = {"token": "correct", "updated": time.monotonic()}
        with self.assertRaisesRegex(ValueError, "过期"):
            await self.plugin.translate("data:image/png;base64,a", capture_token="wrong")
        self.plugin.last_frame["updated"] -= 121
        with self.assertRaisesRegex(ValueError, "过期"):
            await self.plugin.translate("data:image/png;base64,a", capture_token="correct")

    async def test_disabling_ocr_skips_ocr_key_and_capture_token(self):
        await self.plugin.set_api_key("translation-secret")
        await self.plugin.save_settings({"pipeline": "ocr", "ocr_width": 1920})
        settings = await self.plugin.save_settings({"pipeline": "vision"})
        self.assertFalse(settings["has_ocr_key"])
        self.assertEqual(settings["ocr_width"], 1920)
        result = {"items": [], "note": "", "context": "", "terms": ""}
        with patch.object(self.plugin, "_request", return_value=result) as direct, patch.object(
            self.plugin, "_ocr_request", side_effect=AssertionError("OCR must stay disabled")
        ):
            await self.plugin.translate("data:image/png;base64,a")
        direct.assert_called_once()
        self.assertEqual(json.loads(self.plugin.settings_path.read_text())["pipeline"], "vision")

    async def test_reenabling_ocr_preserves_provider_keys_and_settings(self):
        await self.plugin.set_api_key("translation-secret")
        await self.plugin.set_api_key("ocr-secret", self.plugin.settings["ocr_endpoint"])
        before = await self.plugin.save_settings({"pipeline": "ocr", "ocr_width": 1920, "hold_seconds": 30})
        await self.plugin.save_settings({"pipeline": "vision"})
        after = await self.plugin.save_settings({"pipeline": "ocr"})
        self.assertEqual(before, after)
        self.assertEqual(self.plugin._key(), "translation-secret")
        self.assertEqual(self.plugin._key(after["ocr_endpoint"]), "ocr-secret")

    async def test_any_new_key_dismisses_once_but_not_repeat_or_release(self):
        events = struct.Struct("llHHi")
        state = {"ignored": {317, 318}, "fired": False}
        self.plugin.dismiss_armed = True
        with patch.object(backend.decky_plugin, "emit", new=AsyncMock()) as emit:
            for code, value in ((317, 1), (30, 2), (31, 0)):
                await self.plugin._input_events(state, events.pack(0, 0, 1, code, value), events)
            emit.assert_not_awaited()
            await self.plugin._input_events(state, events.pack(0, 0, 1, 304, 1) * 2, events)
            emit.assert_awaited_once_with("dismiss_translation")
        self.assertFalse(self.plugin.dismiss_armed)

    async def test_held_stick_can_dismiss_after_release_and_repress(self):
        events = struct.Struct("llHHi")
        state = {"ignored": {317}, "fired": False}
        self.plugin.dismiss_armed = True
        with patch.object(backend.decky_plugin, "emit", new=AsyncMock()) as emit:
            await self.plugin._input_events(state, events.pack(0, 0, 1, 317, 0) + events.pack(0, 0, 1, 317, 1), events)
            emit.assert_awaited_once_with("dismiss_translation")

    async def test_inputs_before_result_do_not_dismiss(self):
        events = struct.Struct("llHHi")
        state = {"ignored": set(), "fired": False}
        with patch.object(backend.decky_plugin, "emit", new=AsyncMock()) as emit:
            await self.plugin._input_events(state, events.pack(0, 0, 1, 304, 1), events)
            emit.assert_not_awaited()

    async def test_dpad_and_trigger_presses_dismiss_but_held_axes_do_not(self):
        events = struct.Struct("llHHi")
        for code, held, rest, press in ((16, 1, 0, -1), (2, 255, 0, 150)):
            state = {"ignored": set(), "fired": False, "axes": {code: {"value": held, "minimum": 0, "maximum": 255}}}
            self.plugin.dismiss_armed = True
            with patch.object(backend.decky_plugin, "emit", new=AsyncMock()) as emit:
                await self.plugin._input_events(state, events.pack(0, 0, 3, code, held), events)
                emit.assert_not_awaited()
                await self.plugin._input_events(state, events.pack(0, 0, 3, code, rest) + events.pack(0, 0, 3, code, press), events)
                emit.assert_awaited_once_with("dismiss_translation")


    async def test_button_snapshot_dismisses_without_events_once(self):
        state = {"keys": {317, 318}, "ignored": {317, 318}, "fired": False}
        self.plugin.dismiss_armed = True
        with patch.object(backend.decky_plugin, "emit", new=AsyncMock()) as emit:
            await self.plugin._input_buttons(state, {317, 318})
            emit.assert_not_awaited()
            await self.plugin._input_buttons(state, {304})
            await self.plugin._input_buttons(state, {304, 305})
            emit.assert_awaited_once_with("dismiss_translation")
        self.assertFalse(self.plugin.dismiss_armed)

    async def test_first_snapshot_and_releases_do_not_dismiss(self):
        state = {"ignored": set(), "fired": False}
        self.plugin.dismiss_armed = True
        with patch.object(backend.decky_plugin, "emit", new=AsyncMock()) as emit:
            await self.plugin._input_buttons(state, {304})
            await self.plugin._input_buttons(state, set())
            emit.assert_not_awaited()
            await self.plugin._input_buttons(state, {304})
            emit.assert_awaited_once_with("dismiss_translation")

    async def test_snapshot_before_completed_result_does_not_dismiss(self):
        state = {"keys": set(), "ignored": set(), "fired": False}
        with patch.object(backend.decky_plugin, "emit", new=AsyncMock()) as emit:
            await self.plugin._input_buttons(state, {304})
            emit.assert_not_awaited()


if __name__ == "__main__":
    unittest.main()
