#!/usr/bin/env python3
"""Interactive native Mac preview with loopback fixtures, separate from SD data."""
import json
import os
from pathlib import Path
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[2]
PREVIEW = ROOT / 'build/pocketjs-port/mac-preview'
DATA = PREVIEW / 'data'


class Fixture(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, body):
        raw = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        try:
            self.wfile.write(raw)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        self.reply({'data': [{'id': 'deepseek-flash'}]})

    def do_POST(self):
        size = int(self.headers.get('Content-Length', '0'))
        if size > 131072:
            self.send_error(413)
            return
        try:
            request = json.loads(self.rfile.read(size))
            question = request['messages'][-1]['content']
        except (ValueError, KeyError, IndexError):
            self.send_error(400)
            return
        time.sleep(1.5)
        text = ('这是 Mac 本地模拟回复，用来检查中文显示和操作，不会调用真实 API。\n\n'
                f'你的问题：{question}\n\n'
                + '\n\n'.join(f'第 {i} 段：可以用方向键滚动阅读，按 B 返回。请求期间仍可操作，按 X 取消。中文补字示例：麒麟、翡翠、龘、龟。'
                               for i in range(1, 13)))
        self.reply({'choices': [{'message': {'content': text}, 'finish_reason': 'stop'}]})


def main():
    if not (PREVIEW / 'pocketjs-app').exists():
        raise SystemExit('Run ./ports/pocketjs-brick/build-mac.sh first.')
    DATA.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    config_path = DATA / 'config.json'
    config = {'version': 1, 'key': 'LOCAL-PREVIEW', 'baseUrl': f'http://127.0.0.1:{server.server_port}',
              'model': 'deepseek-flash', 'timeoutMs': 30000, 'savedCount': 0}
    if config_path.exists():
        config['savedCount'] = json.loads(config_path.read_text()).get('savedCount', 0)
    config_path.write_text(json.dumps(config, indent=2))
    if not (DATA / 'prompt.txt').exists():
        (DATA / 'prompt.txt').write_text('请介绍这个游戏的剧情，并用适合掌机阅读的短段落回答。')
    env = dict(os.environ, POCKETJS_DATA=str(DATA), POCKETJS_FONT=str(ROOT / 'fonts/font1.ttf'), POCKETJS_TEST='1')
    print('Mac 本地预览：方向键移动，Enter/空格确认，Esc 返回，X 取消，关闭窗口退出。', flush=True)
    print('使用模拟回复；数据保存在 build/pocketjs-port/mac-preview/data。', flush=True)
    try:
        return subprocess.call([str(PREVIEW / 'PocketJS Preview.app/Contents/MacOS/pocketjs-app')], cwd=PREVIEW, env=env)
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    raise SystemExit(main())
