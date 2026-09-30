#!/bin/bash
# 不用设备就能验的两块逻辑：
#   1) 自测：坐标换算（0~1000 / 0~1 / 像素）+ 折行挑字号（SDL_ttf 用桩替代）
#   2) 有响应文件时，再走一遍「抠 content → 解析 items → 坐标换算」全流程
# 用法：./tools/测试解析器.sh [响应文件]     （不给文件就只跑自测）
set -e
cd "$(dirname "$0")/.."
python3 - <<'PY'
import io
src = io.open('workspace/all/minarch/ma_ai.c', encoding='utf-8').read()
def grab(a_m, b_m):
    a = src.index(a_m); b = src.index(b_m, a); return src[a:b]
# 解析段：json 小工具 + ai_parse_items + ai_boxes_normalize
fns = grab('static const char* ai_skip_ws(', '/* ------------------------------------------------------------------ 绘制 */')
# 排版段：ai_wrap / ai_fit_lines（只依赖 SDL_ttf，主机侧有桩）
fns += '\n' + grab('#define AI_MAX_LINES 16', '/* 把一条译文画进它自己的框里')
harness = io.open('tools/解析器测试主机.c', encoding='utf-8').read()
head_end = harness.index('static const char* ai_skip_ws(')
tail_start = harness.index('/* ---- 自测：坐标换算 + 折行 ---- */')
io.open('/tmp/test_parse.c','w',encoding='utf-8').write(harness[:head_end] + fns + '\n' + harness[tail_start:])
PY
clang -o /tmp/test_parse /tmp/test_parse.c -Wall -Wno-unused-function
/tmp/test_parse "$@"
