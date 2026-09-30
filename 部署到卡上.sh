#!/bin/bash
# 把带 AI 翻译功能的 minarch.elf 装到 NextUI SD 卡上（仅适用于运行 NextUI 的 TrimUI Brick）
#
#   ./部署到卡上.sh                # 自动找卡
#   ./部署到卡上.sh /Volumes/NEXTUI  # 指定卡
#
# 只替换 .system/tg5040/bin/minarch.elf 一个文件 —— 源码和卡上那版 NextUI 是同一个
# tag（v6.14.0），唯一的差异就是这个新功能。原文件备份成 minarch.elf.装AI前。
#
# 注意：macOS 的 FAT32 驱动「原地覆写」大文件时会偶发簇链错乱，
# 实测遇到过一次 128KB 内容变成目录项数据。所以这里先 rm 再 cp，并且逐字节校验。

set -e
cd "$(dirname "$0")"

SRC="workspace/all/minarch/build/tg5040/minarch.elf"
[ -f "$SRC" ] || { echo "✗ 还没编译出 $SRC"; exit 1; }
SRC_HASH=$(shasum -a 256 "$SRC" | cut -d' ' -f1)
echo "构建产物: $(stat -f%z "$SRC") 字节  ${SRC_HASH:0:16}…"

CARD="$1"
if [ -z "$CARD" ]; then
  for v in /Volumes/*; do
    if [ -d "$v/.system/tg5040/bin" ]; then CARD="$v"; break; fi
  done
fi
[ -n "$CARD" ] && [ -d "$CARD/.system/tg5040/bin" ] || {
  echo "✗ 没找到 NextUI 的卡。看着像 NextUI 卡的判据是里面有 .system/tg5040/bin/"
  exit 1
}

DST="$CARD/.system/tg5040/bin/minarch.elf"
echo "目标卡  : $CARD"
if ! grep -q '^NextUI' "$CARD/.system/version.txt" 2>/dev/null; then
  echo "✗ 本功能仅适配 NextUI，未在目标卡上识别到 NextUI 版本信息。"
  exit 1
fi

# 1) 备份
if [ ! -f "$DST.装AI前" ]; then
  cp "$DST" "$DST.装AI前" && echo "✔ 备份原文件 → minarch.elf.装AI前"
else
  echo "· 备份已存在，不覆盖"
fi

# 2) 先删再拷（避开 FAT32 原地覆写的坑），不一致就再拷一次
rm -f "$DST"; sync
cp "$SRC" "$DST"; sync
if ! cmp -s "$SRC" "$DST"; then
  echo "· 校验不一致，重新拷贝一次…"
  rm -f "$DST"; sync; cp "$SRC" "$DST"; sync
fi
cmp -s "$SRC" "$DST" || { echo "✗ 拷贝后校验仍不一致，卡的文件系统可能有问题，建议先用磁盘工具检修"; exit 1; }
echo "✔ minarch.elf 已替换，逐字节校验通过"

# 3) AI 专用配置文件（只有本功能读它；不写进 minuisettings.txt，
#    因为卡上的启动器不认识 ai* 键，每次开机都会把那些行重写掉）
T="$CARD/.userdata/shared/ai-translate.txt"
if [ ! -f "$T" ]; then
  cat > "$T" <<EOF
# NextUI AI 画面翻译配置。改完存盘、卡插回设备即可生效。
#
# 开关 / 服务商 / 译文停留时间 也可以在游戏内改：
#   Menu -> Options -> AI Translate
#
# 服务商：0=百炼 Qwen3-VL   1=DeepSeek   2=自定义(用下面的 aiEndpoint/aiModel)
# 默认使用 DeepSeek。
aiEnable=1
aiProvider=1

# 两家的 key 分开写，切服务商时自动用对应的那个。
# 只写了 aiApiKey 的话两家共用它。
aiBailianKey=
aiDeepseekKey=填入你的DeepSeekAPIKey

# 通用兜底 key
aiApiKey=

# 自定义服务商时才需要（OpenAI 兼容接口）
aiEndpoint=https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions
aiModel=qwen3-vl-plus

aiTargetLang=简体中文
aiHoldSecs=5
aiMaxImageWidth=768
aiTimeoutSecs=20
aiDebug=0
EOF
  sync
  echo "✔ 已创建 AI 配置文件，请在 $T 中填写 aiDeepseekKey"
else
  echo "· AI 配置文件已存在，保留你现有的设置"
  if ! grep -q "^aiDebug=" "$T" 2>/dev/null; then
    printf '\n# 调试：1 = 把各环节耗时叠在画面左上角，并写日志到 .userdata/ai/debug.txt\naiDebug=0\n' >> "$T"
    sync
    echo "✔ 已补上 aiDebug=0"
  fi
fi

sync
echo
echo "================================================================"
echo "完成。下一步："
echo "  1) 弹出卡（不要直接拔）"
echo "  2) 关机状态下插回设备，开机"
echo "  3) 连网并填写 API Key 后，进游戏按住 MENU，再按 X，即可翻译"
echo "     MENU+X 已内置为默认绑定，无需逐个机种设置。"
echo "     如已保存过其他绑定或 NONE，可在 Options -> Shortcuts -> AI Translate 修改。"
echo "     本功能仅适用于运行 NextUI 的 TrimUI Brick；请确认教程中的兼容版本。"
echo
echo "还原方法：把 .system/tg5040/bin/minarch.elf.装AI前 覆盖回 minarch.elf"
echo "================================================================"
