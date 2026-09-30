#!/bin/bash
# 把 Voice AI 工具装到 SD 卡上（只动 Tools 目录，不碰 .system）
#
#   ./部署语音AI.sh                # 自动找卡
#   ./部署语音AI.sh /Volumes/NEXTUI  # 指定卡
#
# 装两个东西：
#   1) Tools/tg5040/Voice AI.pak/{voiceai.elf,launch.sh}
#   2) .userdata/shared/voice-ai.txt   （没有才建，已有的绝不覆盖）
#
# 注意：macOS 的 FAT32 驱动「原地覆写」大文件会偶发簇链错乱（实测踩过一次
# 128KB 内容变成目录项数据），所以这里一律先 rm 再 cp，并且逐字节校验。

set -e
cd "$(dirname "$0")"

SRC_DIR="workspace/all/voiceai/build/tg5040"
SRC="$SRC_DIR/voiceai.elf"
PAK_NAME="Voice AI.pak"

[ -f "$SRC" ] || {
  echo "✗ 还没编译出 $SRC"
  echo "  编译方法："
  echo "    make build PLATFORM=tg5040        # 走 docker 全量构建"
  echo "  或者只编这一个工具："
  echo "    docker run --rm -v \"\$PWD/workspace\":/root/workspace \\"
  echo "      ghcr.io/loveretro/tg5040-toolchain:latest \\"
  echo "      /bin/bash -c 'cd /root/workspace && make -C all/voiceai PLATFORM=tg5040'"
  exit 1
}

SRC_HASH=$(shasum -a 256 "$SRC" | cut -d' ' -f1)
echo "构建产物: $(stat -f%z "$SRC") 字节  ${SRC_HASH:0:16}…"

# ---- 找卡 ----
CARD="$1"
if [ -z "$CARD" ]; then
  for v in /Volumes/*; do
    if [ -d "$v/.system/tg5040/bin" ]; then CARD="$v"; break; fi
  done
fi
[ -n "$CARD" ] && [ -d "$CARD/.system/tg5040/bin" ] || {
  echo "✗ 没找到 NextUI 的卡。判据是里面有 .system/tg5040/bin/"
  exit 1
}
echo "目标卡  : $CARD"

# ---- 1) pak ----
PAK="$CARD/Tools/tg5040/$PAK_NAME"
mkdir -p "$PAK"

# 先删再拷，避开 FAT32 原地覆写的坑
rm -f "$PAK/voiceai.elf"; sync
cp "$SRC" "$PAK/voiceai.elf"; sync
if ! cmp -s "$SRC" "$PAK/voiceai.elf"; then
  echo "· 校验不一致，重新拷贝一次…"
  rm -f "$PAK/voiceai.elf"; sync
  cp "$SRC" "$PAK/voiceai.elf"; sync
fi
cmp -s "$SRC" "$PAK/voiceai.elf" || {
  echo "✗ 拷贝后校验仍不一致，卡的文件系统可能有问题，建议先用磁盘工具检修"
  exit 1
}
chmod 755 "$PAK/voiceai.elf" 2>/dev/null || true
echo "✔ voiceai.elf 已安装并逐字节校验通过"

# launch.sh（内容变了就更新）
SRC_LAUNCH="skeleton/EXTRAS/Tools/tg5040/$PAK_NAME/launch.sh"
if [ -f "$SRC_LAUNCH" ]; then
  if ! cmp -s "$SRC_LAUNCH" "$PAK/launch.sh"; then
    rm -f "$PAK/launch.sh"; sync
    cp "$SRC_LAUNCH" "$PAK/launch.sh"; sync
    echo "✔ launch.sh 已更新"
  else
    echo "· launch.sh 无变化"
  fi
  chmod 755 "$PAK/launch.sh" 2>/dev/null || true
fi

# 保险：pak 目录里不该有 macOS 的 ._ 垃圾文件，NextUI 会当成条目显示
rm -f "$PAK"/._* 2>/dev/null || true

# ---- 2) 配置文件 ----
CFG="$CARD/.userdata/shared/voice-ai.txt"
if [ -f "$CFG" ]; then
  echo "· 配置已存在，保留你现有的设置：$CFG"
else
  mkdir -p "$CARD/.userdata/shared"
  # 顺手看看能不能把 ai-translate.txt 里的 key 抄过来当默认值
  LEGACY="$CARD/.userdata/shared/ai-translate.txt"
  BKEY=""; DKEY=""
  if [ -f "$LEGACY" ]; then
    BKEY=$(grep -E '^aiBailianKey=' "$LEGACY" | head -1 | cut -d= -f2-)
    DKEY=$(grep -E '^aiDeepseekKey=' "$LEGACY" | head -1 | cut -d= -f2-)
  fi

  cat > "$CFG" <<EOF
# NextUI Voice AI —— 语音输入，文字输出。
# 改完存盘、卡插回设备即可生效（不用重新装机）。
#
# 【最省事的用法】
# 下面两个 key 留空就行 —— 留空时会自动去读
# .userdata/shared/ai-translate.txt 里的 aiBailianKey / aiDeepseekKey。
# 也就是说：只要你配过游戏内 AI 翻译，这个工具不用再配任何东西。
#
# 【想单独指定 key 就填这里】
# 识别和对话可以分开用两家（识别只有百炼有，对话 DeepSeek / 百炼都行）
voiceAsrKey=${BKEY}
voiceChatKey=${DKEY}

# ---- 模型 ----
# 识别：目前是百炼的 qwen3-asr-flash（走 OpenAI 兼容接口）
voiceAsrModel=qwen3-asr-flash
voiceAsrEndpoint=https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions

# 对话：填了 DeepSeek key 就用 deepseek-chat，否则用百炼的 qwen-plus
voiceChatModel=
voiceChatEndpoint=
# 想自己写系统提示词就填这里（\n 不会被转义，直接写一行）
voiceSystemPrompt=
voiceMaxTokens=600
voiceHistoryTurns=6

# ---- 录音 ----
# Brick 的内置麦克风在 hw:0,0；如果探不到会自动依次试 plughw:0,0 / default
voiceMicDevice=hw:0,0
voiceSampleRate=16000
# 说完之后静音多久算说完（毫秒）。嫌它断太早调大，太慢调小
voiceSilenceMs=1200
# 短于这个时长当成误触，直接丢掉
voiceMinMs=400
# 一次最多录多久（秒）
voiceMaxSecs=30
# 语音判定阈值 0~32767。环境吵、老是自动开始计时，就调大（比如 1200）
voiceVadThreshold=500

# ---- 其它 ----
voiceTimeoutSecs=30
# 每次录音都在 .userdata/ai/samples/ 留一份样本（只留最近 5 次）。默认开：
# 识别出乱码时，没有「实际发出去的音频」根本没法判断是麦克风、重采样还是接口的问题。
# 嫌占地方就改成 0
voiceKeepSamples=1
voiceDebug=0
EOF
  sync
  echo "✔ 已创建配置 $CFG"
  [ -n "$BKEY" ] && echo "  · 已从 ai-translate.txt 抄来百炼 key（长度 ${#BKEY}）"
  [ -n "$DKEY" ] && echo "  · 已从 ai-translate.txt 抄来 DeepSeek key（长度 ${#DKEY}）"
fi

sync
echo
echo "================================================================"
echo "完成。下一步："
echo "  1) 弹出卡（不要直接拔）"
echo "  2) 关机状态下插回设备，开机"
echo "  3) 主页 -> Tools -> Voice AI"
echo "  4) 按 B 说话，停一下会自动结束录音，回答显示在屏幕上"
echo
echo "按键： B = 说话 / 中止本轮      Y = 清空对话重新开始"
echo "       MENU = 退出（轻按）"
echo
echo "出问题看日志： .userdata/tg5040/logs/VoiceAI.txt"
echo "卸载：删掉 $PAK 整个目录即可"
echo "================================================================"
