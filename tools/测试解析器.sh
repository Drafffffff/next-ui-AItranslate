#!/bin/bash
# 用真实的 API 响应验证 ma_ai.c 里的 JSON 解析逻辑（不需要设备）
#   1) python3 tools/抓一份响应.py      # 抓一份真实响应到 /tmp/dashscope_raw.json
#   2) ./tools/测试解析器.sh            # 把 ma_ai.c 里的解析函数抠出来编译并跑
set -e
cd "$(dirname "$0")/.."
python3 - <<'PY'
import io
src = io.open('workspace/all/minarch/ma_ai.c', encoding='utf-8').read()
def grab(a_m, b_m):
    a = src.index(a_m); b = src.index(b_m, a); return src[a:b]
fns = grab('static const char* ai_skip_ws(', '/* ------------------------------------------------------------------ 绘制 */')
struct = src[src.index('typedef struct {'):src.index('} AI_Item;')+len('} AI_Item;')]
harness = io.open('tools/解析器测试主机.c', encoding='utf-8').read()
head_end = harness.index('static const char* ai_skip_ws(')
tail_start = harness.index('int main(int argc')
io.open('/tmp/test_parse.c','w',encoding='utf-8').write(harness[:head_end] + fns + '\n' + harness[tail_start:])
PY
clang -o /tmp/test_parse /tmp/test_parse.c -Wall
/tmp/test_parse "${1:-/tmp/dashscope_raw.json}"
