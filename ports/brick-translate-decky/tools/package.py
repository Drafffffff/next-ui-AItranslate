import json
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1]
version = json.loads((root / "package.json").read_text())["version"]
output = root / "out" / f"Brick.Translate-{version}.zip"
output.parent.mkdir(exist_ok=True)
if not (root / "dist/index.js").is_file():
    raise SystemExit("Run npm run build first")
with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
    for name in ("plugin.json", "package.json", "main.py", "prompt.py", "ocr.py", "README.md", "dist/index.js"):
        archive.write(root / name, "brick-translate/" + name)
print(output)
