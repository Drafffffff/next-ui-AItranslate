#!/usr/bin/env python3
"""Run the real ARM64 app against local HTTP fixtures, never a paid API."""
import http.server
import json
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
import time

task_root = Path(__file__).resolve().parents[2]
output = task_root / 'build/pocketjs-port'
is_mac = sys.platform == 'darwin'
package = output / ('mac-preview' if is_mac else 'PocketJS App.pak')
prefix = 'mac-chat' if is_mac else 'chat'
validation = output / 'validation'
validation.mkdir(exist_ok=True)
request_count = 0
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'
    def log_message(self, *_):
        pass
    def reply(self, status, body):
        data = body.encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass
    def do_GET(self):
        if self.path == '/slow':
            time.sleep(.6)
            self.reply(200, '{}')
        elif self.path == '/large':
            self.reply(200, 'x' * (140 * 1024))
        elif self.path == '/unauthorized':
            self.reply(401, '{}')
        else:
            self.reply(200, json.dumps({'text': '麒麟翡翠：运行时中文'}, ensure_ascii=False))
    def do_POST(self):
        global request_count
        try:
            data = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))))
            assert self.headers.get('Authorization') == 'Bearer TEST"KEY\\ONLY'
            assert data['model'] == 'deepseek-flash'
            assert data['messages'][-1]['content'] == 'A TEST QUESTION'
            assert data['stream'] is False
            assert data['thinking']['type'] == 'disabled'
        except Exception:
            self.reply(400, '{}')
            return
        request_count += 1
        time.sleep(.25)
        self.reply(200, json.dumps({'choices': [{'message': {'content': '麒麟翡翠：这是动态中文回复。\n\n' + '长回复应当能够滚动阅读，并保存为连续对话。' * 60}}]}, ensure_ascii=False))

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
with tempfile.TemporaryDirectory(prefix='chat-', dir=validation) as root:
    root = Path(root)
    tls = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=localhost', '-keyout', str(root/'key.pem'), '-out', str(root/'cert.pem')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(root/'cert.pem', root/'key.pem')
    tls.socket = context.wrap_socket(tls.socket, server_side=True)
    threading.Thread(target=tls.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}'
    (root/'config.json').write_text(json.dumps({'version': 1, 'key': 'TEST"KEY\\ONLY', 'baseUrl': url, 'model': 'deepseek-flash', 'timeoutMs': 2000, 'savedCount': 0}))
    environment = dict(os.environ, POCKETJS_DATA=str(root), POCKETJS_FONT=str(task_root/'fonts/font1.ttf'), POCKETJS_CA='/etc/ssl/cert.pem' if is_mac else str(package/'ca-bundle.crt'), POCKETJS_TEST_URL=url, POCKETJS_TEST_TLS_URL=f'https://127.0.0.1:{tls.server_port}')
    sysroot = '/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc'
    loader = f'{sysroot}/lib/ld-linux-aarch64.so.1'
    args = [loader, '--library-path', f'{sysroot}/lib:{sysroot}/usr/lib', './pocketjs-app.elf', '--self-test', '--dump', str(validation/'chat-reading.ppm')]
    if is_mac:
        args = ['./pocketjs-app', '--self-test', '--dump', str(validation/(prefix+'-reading.ppm'))]
    result = subprocess.run(args, cwd=package, env=environment, capture_output=True, text=True, timeout=20)
    print(result.stdout, end='')
    print(result.stderr, end='')
    (validation/(prefix+'-acceptance.log')).write_text(result.stdout + result.stderr)
    assert result.returncode == 0, 'Native app acceptance failed'
    assert request_count == 1
    assert len(json.loads((root/'history.json').read_text())) == 2
    assert not list(root.glob('.*.tmp'))
    tls.shutdown()
server.shutdown()
print('PASS: local HTTP fixtures, real POST body/header escaping, secure TLS rejection, atomic files, restart history; no external API call')
