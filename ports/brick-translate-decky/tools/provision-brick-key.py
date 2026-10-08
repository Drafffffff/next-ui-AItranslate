"""Copy only the requested provider key from Brick; never print credentials."""
import configparser
import subprocess
import sys

source = sys.argv[1] if len(sys.argv) > 1 else "nextui-brick"
target = sys.argv[2] if len(sys.argv) > 2 else "bazzite"
result = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", source,
                         "cat /mnt/SDCARD/.userdata/shared/ai-keys.txt"], capture_output=True, check=True)
if len(result.stdout) > 16384:
    raise SystemExit("Brick key configuration is too large")
parser = configparser.ConfigParser(interpolation=None, strict=True)
parser.optionxform = str
parser.read_string("[keys]\n" + result.stdout.decode("utf-8-sig"))
key = parser.get("keys", "DEEPSEEK_API_KEY", fallback="").strip()
if not key or len(key) > 511 or any(not 33 <= ord(c) <= 126 for c in key):
    raise SystemExit("Brick has no valid DeepSeek key")
script = '''import os,sys,tempfile
from pathlib import Path
p=Path.home()/"homebrew/settings/brick-translate"
p.mkdir(parents=True,exist_ok=True)
fd,t=tempfile.mkstemp(dir=p,prefix=".key-")
with os.fdopen(fd,"w") as f: f.write(sys.stdin.read().strip())
os.replace(t,p/"api-key-deepseek")
'''
quote = lambda value: "'" + value.replace("'", "'\\''") + "'"
subprocess.run(["ssh", "-o", "BatchMode=yes", target, "python3 -c " + quote(script)], input=key.encode(), check=True)
print("Brick DeepSeek key copied to the plugin private configuration (0600)")
