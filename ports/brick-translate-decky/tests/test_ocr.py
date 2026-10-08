import json
import unittest
from test_backend import backend
from ocr import parse_lines, bind_geometry


def response(words):
    return {"output": {"choices": [{"message": {"content": [{"ocr_result": {"words_info": words}}]}}]}}


class OCRTests(unittest.TestCase):
    def setUp(self):
        self.words = [{"text": "Start game", "location": [100, 200, 300, 190, 310, 230, 110, 240]},
                      {"text": "Continue", "location": [100, 250, 300, 250, 300, 290, 100, 290]}]
        self.lines = parse_lines(response(self.words), 1000, 500)

    def test_rotated_quad_uses_original_image_dimensions(self):
        self.assertEqual(self.lines[0]["box"], [0.1, 0.38, 0.31, 0.48])

    def test_missing_geometry_does_not_guess(self):
        with self.assertRaises(ValueError):
            parse_lines({"output": {"choices": [{"message": {"content": [{"text": "Start game"}]}}]}}, 1000, 500)

    def test_invalid_coordinates_fail_closed(self):
        for points in ([0, 0, 2000, 0, 2000, 30, 0, 30], [0, 0, True, 0, 10, 30, 0, 30], [0] * 7,
                       [0, 0, float("nan"), 0, 10, 30, 0, 30]):
            with self.assertRaises(ValueError):
                parse_lines(response([{"text": "bad", "location": points}]), 1000, 500)

    def test_translation_cannot_replace_ocr_geometry(self):
        result = backend.normalize_result(bind_geometry(json.dumps({"items": [
            {"ids": [0, 1], "zh": "开始并继续游戏", "box": [0, 0, 1, 1], "orig": "invented"}]}), self.lines))
        self.assertEqual(result["items"][0]["box"], [0.1, 0.38, 0.31, 0.58])
        self.assertEqual(result["items"][0]["orig"], "Start game Continue")

    def test_unknown_duplicate_and_boolean_ids_are_rejected(self):
        for items in ([{"ids": [2]}], [{"ids": [True]}], [{"ids": [0, 0]}], [{"ids": [0]}, {"ids": [0]}]):
            with self.assertRaises(ValueError):
                bind_geometry(json.dumps({"items": items}), self.lines)

    def test_empty_ocr_is_valid(self):
        self.assertEqual(parse_lines(response([]), 1000, 500), [])

    def test_small_screen_edge_overhang_is_clamped(self):
        result = parse_lines(response([{"text": "edge", "location": [980, 10, 1009, 10, 1009, 40, 980, 40]}]), 1000, 500)
        self.assertEqual(result[0]["box"], [0.98, 0.02, 1, 0.08])

    def test_truncated_ocr_is_rejected(self):
        value = response(self.words)
        value["output"]["choices"][0]["finish_reason"] = "length"
        with self.assertRaises(ValueError):
            parse_lines(value, 1000, 500)
