#!/bin/bash
# 用真实响应体验证「流式解析」那部分逻辑，不用上设备试。
#
#   ./tools/测试语音解析器.sh
#
# 覆盖：UTF-8 折行（中英混排）、JSON 反转义、base64 往返、WAV 表头、
#       TTS 流式 SSE 的分块音频拼接、对话流式 delta 的 [DONE] 处理。
#
# tools/tts-sample.sse 是从 dashscope 真实抓下来的响应（裁到最小体积）。

set -e
cd "$(dirname "$0")/.."

SRC="workspace/all/voiceai/via_ui.c"
TEST="tools/test_voiceai.c"
SAMPLE="tools/tts-sample.sse"
OUT="/tmp/test_voiceai.$$"

command -v cc >/dev/null || { echo "✗ 找不到 cc"; exit 1; }
[ -f "$SRC" ] || { echo "✗ 找不到 $SRC"; exit 1; }

echo "编译宿主机测试（不依赖 SDL / 不依赖设备 / 不联网）…"
cc -O1 -Wall -Wextra -Wno-unused-parameter -o "$OUT" "$TEST" "$SRC" -lm

echo
if [ -f "$SAMPLE" ]; then
  "$OUT" "$SAMPLE"
else
  echo "⚠ 没找到 $SAMPLE，跳过流式解析那部分"
  "$OUT"
fi
RC=$?

rm -f "$OUT"
exit $RC
