#!/bin/sh
# Voice AI —— 语音输入，文字输出
#
# pak 就是一个带 launch.sh 的文件夹。约定（和 Clock.pak / Files.pak 一致）：
#   - cd 到 pak 自己的目录
#   - SDL 相关的东西由 NextUI 在环境里准备好，不用自己设 LD_LIBRARY_PATH
#     （和 bmo-pak 那种自带 SDL 的第三方 pak 不同，我们直接链设备上的
#      libSDL2-2.0.so.0 / libmsettings.so，它们在 /usr/trimui/lib 和 .system 里）

cd "$(dirname "$0")" || exit 1

# 日志：出问题时把 .userdata/tg5040/logs/VoiceAI.txt 发出来就能定位
LOG_DIR="${USERDATA_PATH:-/mnt/SDCARD/.userdata/tg5040}/logs"
mkdir -p "$LOG_DIR" 2>/dev/null

./voiceai.elf &> "$LOG_DIR/VoiceAI.txt"
