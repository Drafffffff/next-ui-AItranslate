"""Run on the device with one existing screenshot; never prints credentials or OCR text."""
import base64
import io
import json
import logging
from pathlib import Path
import sys
import time
import types

from PIL import Image

root = Path(sys.argv[1])
sys.path.insert(0, str(root))
sys.modules["decky_plugin"] = types.SimpleNamespace(DECKY_PLUGIN_SETTINGS_DIR="", logger=logging.getLogger("benchmark"))
from main import Plugin, DEFAULTS

plugin = Plugin()
plugin.directory = Path(sys.argv[2])
plugin.settings = dict(DEFAULTS)
source = Image.open(sys.argv[3])
original_post = plugin._post
def inspect_post(body, endpoint, key, timeout):
    result = original_post(body, endpoint, key, timeout)
    if "input" in body:
        content = result.get("output", {}).get("choices", [{}])[0].get("message", {}).get("content", [])
        words = [word for part in content for word in part.get("ocr_result", {}).get("words_info", [])]
        xs = [v for word in words for v in word.get("location", [])[::2]]
        ys = [v for word in words for v in word.get("location", [])[1::2]]
        print(json.dumps({"coordinate_extent": [min(xs), min(ys), max(xs), max(ys)] if xs else [],
                          "usage": result.get("usage"), "model": body["model"]}), flush=True)
    return result
plugin._post = inspect_post
results = []
cases = [("qwen-vl-ocr-2025-11-20", int(value)) for value in sys.argv[4:]] if len(sys.argv) > 4 else [
    ("qwen-vl-ocr-2025-11-20", 1280), ("qwen-vl-ocr-2025-11-20", 1920), ("qwen3.5-ocr", 1280)]
for model, width in cases:
    resized = source.copy()
    resized.thumbnail((width, 8192), Image.Resampling.LANCZOS)
    output = io.BytesIO()
    resized.convert("RGB").save(output, format="JPEG", quality=88)
    frame = {"image": "data:image/jpeg;base64," + base64.b64encode(output.getvalue()).decode(),
             "width": resized.width, "height": resized.height}
    settings = {**plugin.settings, "ocr_model": model}
    started = time.monotonic()
    try:
        result = plugin._ocr_request(frame, "Caves of Qud", settings, plugin._key(), plugin._key(settings["ocr_endpoint"]), None)
        item = {"model": model, "width": width, "bytes": len(output.getvalue()), "seconds": round(time.monotonic() - started, 2),
                "ocr_seconds": result["ocr_seconds"], "lines": result["ocr_lines"], "translations": len(result["items"])}
    except Exception as error:
        item = {"model": model, "width": width, "error": str(error), "seconds": round(time.monotonic() - started, 2)}
    results.append(item)
    print(json.dumps(item), flush=True)
