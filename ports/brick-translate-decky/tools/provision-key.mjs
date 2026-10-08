import {spawnSync} from "node:child_process";

const key = process.env.DASHSCOPE_API_KEY;
if (!key) throw new Error("DASHSCOPE_API_KEY is not set");
const target = process.argv[2] ?? "bazzite";
const script = `import os,sys,tempfile
from pathlib import Path
p=Path.home()/"homebrew/settings/brick-translate"
p.mkdir(parents=True,exist_ok=True)
key=sys.stdin.read().strip()
if not key or len(key)>4096 or any(c.isspace() for c in key): raise SystemExit("Invalid key")
fd,t=tempfile.mkstemp(dir=p,prefix=".key-")
with os.fdopen(fd,"w") as f: f.write(key)
os.replace(t,p/"api-key")
print("API key saved with mode 0600")`;
const quote = value => "'" + value.replaceAll("'", "'\\''") + "'";
const result = spawnSync("ssh", ["-o", "BatchMode=yes", target, "python3 -c " + quote(script)], {input: key, encoding: "utf8"});
if (result.status !== 0) throw new Error("Unable to provision device API key");
console.log(result.stdout.trim());
