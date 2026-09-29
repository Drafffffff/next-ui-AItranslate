#!/bin/bash
# 把带 AI 翻译功能的 minarch.elf 装到 SD 卡上
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
  KEY=$(grep -oE 'DASHSCOPE_API_KEY\s*=\s*["'"'"']?[^"'"'"'[:space:]]+' ~/.zshrc 2>/dev/null | head -1 | sed -E 's/.*=\s*["'"'"']?//')
  if [ -z "$KEY" ]; then
    echo "⚠ 没在 ~/.zshrc 里读到 DASHSCOPE_API_KEY，配置文件没建。请手动创建 $T"
    KEY="把你的key填这里"
  fi
  cat > "$T" <<EOF
# NextUI AI 画面翻译配置。改完存盘、卡插回设备即可生效。
#
# 开关 / 服务商 / 译文停留时间 也可以在游戏内改：
#   Menu -> Options -> AI Translate
#
# 服务商：0=百炼 Qwen3-VL   1=DeepSeek   2=自定义(用下面的 aiEndpoint/aiModel)
# 注意 DeepSeek 的坐标定位不准，贴回原位会歪，一般只建议百炼。
aiProvider=0

# 两家的 key 分开写，切服务商时自动用对应的那个。
# 只写了 aiApiKey 的话两家共用它。
aiBailianKey=$KEY
aiDeepseekKey=

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
  echo "✔ 已创建 AI 配置文件（key 长度 ${#KEY}）"
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
echo "  3) 进游戏后按 Menu -> Options -> Shortcuts -> \"AI Translate\""
echo "     按一个键绑定（建议 MENU+X），然后回 Options 选 Save Changes -> Saved for console"
echo "     ⚠ 不要往 pak 的 default.cfg 里加 bind 行 —— 会撑破 core_button_mapping 导致闪退"
echo
echo "还原方法：把 .system/tg5040/bin/minarch.elf.装AI前 覆盖回 minarch.elf"
echo "================================================================"
