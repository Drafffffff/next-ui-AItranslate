# NextUI + 游戏内 AI 画面翻译

在游戏里按一个热键：抓下当前画面 → 发给多模态模型翻译 → **把译文贴回原文所在的位置**。

基于 **NextUI v6.14.0**（和卡上那版完全同源），所以只替换 `minarch.elf` 一个文件就够了。

---

## 怎么用

### 1. 装

```bash
./部署到卡上.sh
```

脚本会自动找卡、备份原 `minarch.elf`、替换成新的。

### 2. 填 API key

**AI 的配置单独放一个文件**：卡上 `.userdata/shared/ai-translate.txt`

```
# 服务商：0=百炼 Qwen3-VL   1=DeepSeek(默认)   2=自定义
aiProvider=1

# 两家的 key 分开写，切服务商时自动用对应那个。只写 aiApiKey 则两家共用。
aiBailianKey=sk-你的百炼key
aiDeepseekKey=
aiApiKey=

# 仅 aiProvider=2 时用到
aiEndpoint=https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions
aiModel=qwen3-vl-plus

aiTargetLang=简体中文
aiHoldSecs=5
aiMaxImageWidth=768
aiTimeoutSecs=20
aiDebug=0
```

**开关 / 服务商 / 译文停留 / 送图宽度 / 调试信息 也能在游戏里改**：`Menu → Options → AI Translate`。
改完直接写回这个文件（注释和其它行都保留），不用拔卡。

**默认服务商是 DeepSeek**（`aiProvider=1`）。它的坐标定位不如百炼稳，译文可能贴歪；
不满意就在游戏菜单里切回「百炼 Qwen3-VL」。

> **为什么不用 `minuisettings.txt`？**（踩过的坑，别改回去）
> `CFG_sync()` 会按调用者**自己的内存结构**把整个 `minuisettings.txt` 重写一遍。
> 卡上的 `nextui.elf`（启动器）和 `settings.elf` 还是原版、不认识 `ai*` 键，
> 于是**每次开机启动器都会把 ai 那几行抹掉**——实测：写进去 8 行，开机后剩 0 行。
> 放独立文件后只有本模块读它，谁也动不了。

### DeepSeek 能不能用？

能选，**但坐标定位不可靠**。实测 `deepseek-flash` 同一份输入连问三次，
每次会给 y 坐标乘一个**不同的**整体缩放系数（1.0 / 0.70 / 0.48 乱跳），
结果就是译文会贴歪、原文盖不干净。它读字很准，但不会 grounding。
`qwen3-vl-plus` 连问两次数值完全一致，误差只有 ±5px。所以默认是百炼。

### 3. 绑热键

进游戏后 **Menu → Options → Shortcuts → "AI Translate"**，按 A 然后按你想绑的键（建议 `MENU+X`）。
绑完退回 Options 页，选 **Save Changes → "Saved for console"**（对当前机种的所有游戏生效）。

它是按机种分开存的，FC/SFC/MD 想用就各自绑一次。

> **⚠ 千万不要往 pak 的 `default.cfg` 里加 `bind` 行。**
> `Config_init()` 会把 `default.cfg` 里每一行 `bind ...` 追加进定长数组
> `core_button_mapping[RETRO_BUTTON_COUNT+1]`（只有 17 格，最后一格是终止符）。
> 原版每个 pak 的 bind 行数正好是 16，多一行就会覆盖终止符，
> 之后所有遍历该数组的循环都会读到数组外 —— **直接段错误闪退**。
> 实测踩过：加了第 17 行后游戏能进、跑几秒就崩，日志停在 `Config_readControlsString`。
> （已在 `Config_init` 里补了边界保护，现在多写只会警告并忽略，但别依赖它。）

### 4. 用

游戏里按下热键 → 画面冻住显示"翻译中…"（约 5~8 秒）→ 译文贴在原文位置。
译文**默认保留 5 秒后自动渐隐**（400ms 交叉淡出），中途按任意键可以立刻开始渐隐。
想让它一直留到你按键，把 `aiHoldSecs` 设成 `0`。

**支持所有机种**：FC / SFC / MD / GBA / GB / GBC / PCE / PS / SGB / 街机……全都行。
因为热键和覆盖层都在 `minarch` 里，和具体模拟器核心无关 —— 已核对发行包里
30+ 个 `*.pak/launch.sh` 每一个都是启动 `minarch.elf`。
模型看到的是缩放后的整屏画面，所以坐标映射与游戏原始分辨率无关。

---

## 配置项

| 键 | 默认值 | 说明 |
| --- | --- | --- |
| `aiEnable` | `0` | 总开关 |
| `aiApiKey` | 空 | 没填会提示"未配置" |
| `aiEndpoint` | 阿里云百炼的 OpenAI 兼容端点 | 换别的厂商只改这里 + `aiModel` |
| `aiModel` | `qwen3-vl-plus` | 仅 `aiProvider=2` 时生效 |
| `aiTargetLang` | `简体中文` | 会写进提示词 |
| `aiHoldSecs` | `5` | 译文停留几秒后渐隐，`0` = 一直留到按键 |
| `aiMaxImageWidth` | `768` | 送图前缩到这个宽度以内 |
| `aiTimeoutSecs` | `20` | 单次请求超时 |

### 模型怎么选（实测数据）

同一张章节标题图，各测两次：

| 模型 | 往返 | y 坐标误差 | 结论 |
| --- | --- | --- | --- |
| **qwen3-vl-plus** | 7.4~8.0s | Δ −4~−2 px | **默认**，框最紧 → 字号正常、不漏译 |
| qwen3-vl-flash | 4.3~5.6s | Δ −4~+3 px | 最快，但框松（标题会被撑出底板）、偶尔漏译一条 |
| qwen3.8-flash | 32~37s | Δ −3~+2 px | 准但太慢，通用推理模型 |
| deepseek-flash | 1.0~1.3s | **每次乘不同缩放系数** | **不能用** |

`deepseek-flash` 读字很准，但坐标每次调用会整体乘一个不同的系数（实测 1.0 / 0.7 / 0.48 三档乱跳），
拿它做原位覆盖会把译文献得到处都是。换模型是唯一解。

---

## 它是怎么工作的

```
热键 → GFX_GL_screenCapture 抓当前画面
     → 缩到 aiMaxImageWidth → 存 PNG → base64
     → 拼 OpenAI 兼容的 JSON（prompt 里要求模型返回归一化坐标）
     → curl 子进程发出去（body 走 @文件，不走命令行）
     → 解析回 {"items":[{"orig","zh","box":[0~1000],"color","align"}]}
     → 每条的框换算成像素 → 取框内背景色众数 → 擦掉原文 → 把译文按框大小自动定字号画上去
     → 冻住画面等按键，然后还原
```

**本地不做任何"找文字"的图像算法**——定位完全交给模型。这是刻意的取舍：
本地找文字要求"文字外面有闭合边框"，状态条、裸标题、居中大标题全都没有框，会漏。
（那套本地检测的代码写在 `~/Downloads/ai-translate-test/overlay_v4.py`，实测能跑，但已按需求搁置。）

### 改动的文件

| 文件 | 改了什么 |
| --- | --- |
| `workspace/all/minarch/ma_ai.c` | **新增**，全部逻辑都在这里（约 730 行） |
| `workspace/all/minarch/ma_ai.h` | **新增**，只导出 `Menu_aiTranslate()` |
| `workspace/all/common/config.h/.c` | 加 `ai*` 七个配置项（结构体字段、默认值、读写、访问器） |
| `workspace/all/minarch/ma_internal.h` | 快捷键枚举加 `SHORTCUT_AI_TRANSLATE` |
| `workspace/all/minarch/ma_config.c` | 快捷键表加 `"AI Translate"`，这样它才会出现在设置界面 |
| `workspace/all/minarch/ma_input.c` | 热键分发 |
| `workspace/all/minarch/makefile` | 把 `ma_ai.c` 加进 SOURCE |

补丁存在 `ai-patch/ai-translate.patch`（只含那 6 个文件的改动），`ai-patch/ma_ai.{c,h}` 是新文件。
上游升版后重打：`patch -p1 < ai-patch/ai-translate.patch` + 拷两个新文件。

---

## 重新编译

```bash
colima start                       # Docker 守护进程（本机用 colima）
make build PLATFORM=tg5040         # 走官方 Docker 工具链镜像
```

只编 minarch 更快：

```bash
docker run --rm -v "$(pwd)/workspace":/root/workspace \
  ghcr.io/loveretro/tg5040-toolchain:latest \
  /bin/bash -c '. ~/.bashrc && cd /root/workspace/all/minarch && make'
```

**部署的坑**：macOS 的 FAT32 驱动**原地覆写**大文件时会偶发簇链错乱 ——
实测遇到过一次，`cp` 覆盖后 128KB 的内容变成了目录项数据（同尺寸、内容全错）。
所以部署脚本改成**先 `rm` 再 `cp`，并且 `cmp` 逐字节校验**，不一致就重拷。

**构建的坑**：构建过程要 `git clone` libretro-common / libchdr / rcheevos。
容器内直连 GitHub 很不稳（实测 libchdr 那次 TLS 被掐断）。
解决办法是在宿主机挂代理预克隆 `libchdr` 和 `libretro-common` 到
`workspace/all/minarch/` 下——makefile 的规则会看到目录已存在就跳过。
（`libretro-common` 那条规则是无条件的 `git clone`，所以要么它先成功，要么别删。）

---

## 已知限制

1. **模型漏读结尾字符会在画面上留半截原文**。实测遇到过一次：年代那行结尾的 `まえ` 没读出来，
   框也就短了，右侧残留一个 `え`。要根治得本地复核像素，那属于已明确否掉的"算法"。
2. **擦除外扩和容器边框互斥**。外扩小了留残字，外扩大了会啃掉紧贴文字的边框（标题底板、状态条白边框）。
   现在按屏幕尺寸取 2% 的比例外扩，偏保守。
3. **极小像素字的 OCR 本身有歧义**。同一行模糊假名，deepseek 读成 `たびびとのうみ`、
   qwen 读成 `たてもの`/`たたかう`。译文跟着原文走，所以整句可能跑偏。
4. **依赖设备上的 curl**。NextUI 的 RetroAchievements 也是靠 `popen("curl ...")` 发 HTTPS 的，
   所以设备 rootfs 里应该有。代码里做了运行时探测 + wget 兜底 + 明确的错误提示，
   万一没有会直接告诉你。
5. **每次 5~8 秒**，交互是"暂停 → 翻译 → 看 → 恢复"，做不到实时。
6. **API key 明文存在 SD 卡**上。

---

## 上游版本注意

卡上装的是 **NextUI v6.14.0**（release `20260719`）。
本目录的源码就是这个 tag，不是 main 分支 HEAD —— main 已经比卡上新的多（2026-09-15），
把 main 编出来的 minarch.elf 单独塞进旧系统有可能和 libmsettings、资源文件对不上。
上游升版后，要按新版重新打补丁并整体升级，别只换这一个文件。
