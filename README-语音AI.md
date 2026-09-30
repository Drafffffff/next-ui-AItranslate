# NextUI Voice AI —— 掌机上的语音对话工具

在 TrimUI Brick 上按一个键说话，松口自动结束，**语音识别 → 大模型 → 回答显示在屏幕上**。

这是 NextUI 的**原生工具 pak**（不是模拟器插件）：出现在 `Tools` 菜单里，
和 Clock / Settings 一个层级，独立可执行文件，走设备自带的 ALSA 直接读内置麦克风。

**输入是语音，输出是文字** —— 不做 TTS 播报。屏幕上看回答，少一次 API 往返，快很多。

---

## 怎么用

### 1. 装

```bash
make build PLATFORM=tg5040     # 交叉编译（走 docker）
./部署语音AI.sh                 # 自动找卡并安装
```

`部署语音AI.sh` 只动两个地方，**不碰 `.system`**：

| 装到哪 | 装了什么 |
| --- | --- |
| `Tools/tg5040/Voice AI.pak/` | `voiceai.elf` + `launch.sh` |
| `.userdata/shared/voice-ai.txt` | 配置（**已存在就绝不覆盖**） |

### 2. Key 会自动复用

**如果你配过游戏内 AI 画面翻译，那这个工具什么都不用配。**
`voiceAsrKey` / `voiceChatKey` 留空时会自动去读
`.userdata/shared/ai-translate.txt` 里的 `aiBailianKey` / `aiDeepseekKey`。

要单独指定就在 `.userdata/shared/voice-ai.txt` 里填：

```
voiceAsrKey=sk-你的百炼key        # 识别（目前只有百炼有 qwen3-asr-flash）
voiceChatKey=sk-你的DeepSeekkey   # 对话
```

识别和对话可以分开用两家。没配 key 时工具会**直接把配置方法写在屏幕上**，
不会让你对着麦克风白说半天。

### 3. 按键

| 键 | 作用 |
| --- | --- |
| **B** | 开始说话；录音中再按一次＝提前结束；识别/对话中＝放弃本轮 |
| **Y** | 清空对话，重新开始（历史会一起清掉） |
| **MENU**（轻按） | 退出回 NextUI |

录音时屏幕上有一个**音量条**，能确认麦克风真的收到了声音（这个很重要：
Brick 上没有其它地方能看到麦克风状态，没反馈的话用户根本分不清
「没插好麦」和「麦克风坏了」）。

### 4. 说话

对着**机身底部 3.5mm 耳机孔和 USB-C 之间那个小圆孔**正常说话就行。
停下来静音 `voiceSilenceMs`（默认 1.2 秒）后自动结束录音。

---

## 硬件与链路（实测结论，别凭直觉改）

### 麦克风

Brick 有**内置单声道麦克风**（官方规格 `Built-in mono microphone`），
不是外接、不需要额外硬件。

```
采集设备  hw:0,0        16kHz / S16_LE / 单声道
```

- `Arecord` 探测顺序：`hw:0,0` → `plughw:0,0` → `default`。
  先试 `hw:` 是因为直通最省 CPU 也最可控；后两个走 ALSA 插件层自动转格式，作为兜底。
- 采集缓冲给到 200ms，避免 SD 卡或调度抖动导致丢样。
- **麦克风和扬声器是同一个声卡（hw:0,0）**。本工具不做播放，所以不存在抢占问题。

### 三个 API（全部实测过，不是照文档猜的）

| 步骤 | 端点 | 实测结果 |
| --- | --- | --- |
| 识别 | `dashscope.aliyuncs.com/compatible-mode/v1/chat/completions` | ✅ 中文识别准确，还会在 `annotations` 里返回 `language` / `emotion` |
| 对话 | `api.deepseek.com/chat/completions` | ✅ 标准 OpenAI 格式，流式可用 |
| 对话（备选） | 百炼 `compatible-mode/v1/chat/completions` | ✅ `qwen-plus` 同一套格式 |

**识别的请求形状**（这是最容易猜错的地方，卡上就这一种写法可用）：

```json
POST /compatible-mode/v1/chat/completions
{"model":"qwen3-asr-flash",
 "messages":[{"role":"user","content":[
   {"type":"input_audio",
    "input_audio":{"data":"data:audio/wav;base64,<整个 WAV 文件的 base64>"}}]}]}
```

响应就是普通的 `choices[0].message.content`。

实测命令和返回值：

```bash
# 用 say 生成中文语音 -> afconvert 转 16k 单声道 -> base64 -> 发过去
# 识别结果："今天天气怎么样？我想去公园散步。"
```

**对话走流式（`stream:true`）**，正文在 `choices[0].delta.content`，
逐段追加到屏幕 —— 这样不用干等两三秒才有字出来。

---

## 配置项

文件：`.userdata/shared/voice-ai.txt`

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `voiceAsrKey` | 空 | 识别用的 key，空则复用 `aiBailianKey` |
| `voiceChatKey` | 空 | 对话用的 key，空则复用 `aiDeepseekKey` |
| `voiceAsrModel` | `qwen3-asr-flash` | 百炼的语音识别模型 |
| `voiceAsrEndpoint` | 百炼 compatible-mode | 换别家改这里 |
| `voiceChatModel` | `deepseek-chat` / `qwen-plus` | 按哪把 key 自动选 |
| `voiceChatEndpoint` | `api.deepseek.com` / 百炼 | 同上 |
| `voiceSystemPrompt` | 见下 | 系统提示词，一整行 |
| `voiceMaxTokens` | `600` | 回答长度上限 |
| `voiceHistoryTurns` | `6` | 带几轮上下文 |
| `voiceMicDevice` | `hw:0,0` | 采集设备 |
| `voiceSampleRate` | `16000` | 采集速率 |
| `voiceSilenceMs` | `1200` | 静音多久算说完（毫秒） |
| `voiceMinMs` | `400` | 短于此当误触 |
| `voiceMaxSecs` | `30` | 单次最长录音 |
| `voiceVadThreshold` | `500` | 语音判定阈值 0~32767 |
| `voiceTimeoutSecs` | `30` | 单次请求超时 |
| `voiceKeepSamples` | `1` | 每次录音留样本到 `.userdata/ai/samples/`（只留最近 5 次） |

**写了空值 = 用默认值**（部署脚本生成的 `voiceChatModel=` 就是空的）。
这点特意处理过 —— 早期版本会把空值当有效值，把默认模型覆盖成空字符串。

### 调参经验

- **环境吵、老是自己开始计时** → 调大 `voiceVadThreshold`（试 1200）。
- **话说一半就断了** → 调大 `voiceSilenceMs`（试 2000）。
- **想按住说话、说完就松** → 录音中随时按 B 结束，不用等静音判定。
- 阈值是按 16bit PCM 的**均方根**算的，不是峰值。

---

## 出问题怎么查

日志：`.userdata/tg5040/logs/VoiceAI.txt`（`launch.sh` 把所有输出都重定向过去）

另外每次请求的原始报文物件都留在卡上，可以直接看：

```
.userdata/ai/stt_req.json  stt_resp.json     # 识别
.userdata/ai/chat_req.json chat_resp.json    # 对话（流式时 resp 是 SSE 原文）
.userdata/ai/mic.wav                          # 最近一次录到的音频
.userdata/ai/stderr.err                       # curl 的 stderr
```

另外 `.userdata/ai/samples/` 里有更完整的样本（`_raw.wav` 原始采集、
`_sent.wav` 实际发出的），排查步骤见上面「识别出乱码怎么查」那一节。
`mic.wav` 是**重采样后**那次录音，和 `_sent.wav` 内容一致，方便快速取用。

---

## 识别出乱码怎么查（重要）

**症状**：音量条正常跳动（说明麦克风在收音），但识别结果完全不对。

这个症状特别有误导性，因为**音量条只算 RMS** —— 采样率标错、字节序错、
声道错、重采样错，它都照样正常跳。所以「音量条对」只能证明「有声音进来」，
证明不了「送出去的音频是对的」。

程序现在**每次录音都会留样本**（`voiceKeepSamples=1`，默认开，只留最近 5 次）：

```
.userdata/ai/samples/<时间戳>_raw.wav    麦克风原始采集，按硬件真实速率标注
.userdata/ai/samples/<时间戳>_sent.wav   重采样后、真正 base64 发出去的那段
```

**排查顺序**：

1. **先听 `_raw.wav`。**
   - 听不到人声 / 只有噪音 → 麦克风设备或阈值问题（调 `voiceVadThreshold`）
   - 人声正常 → 往下走
2. **再听 `_sent.wav`。**
   - 音调变尖（像花栗鼠）、语速变快 → 采样率标错了：数据是 48k 却标成 16k
   - 音调变低、语速变慢 → 反过来的错标
   - 两个都正常 → **问题不在音频**，在网络或接口那一层
3. 两种情况都正常但识别还是乱 → 看 `.userdata/ai/stt_resp.json` 的原始响应，
   以及 `.userdata/ai/stderr.err`。

界面上顶栏会显示**麦克风实际速率**（比如 `hw:0,0 48000Hz`）。
这个数字比配置里的 `voiceSampleRate` 更有意义 —— 硬件可能根本不支持 16k。

### 这一版修掉的正是这个 bug

原来的代码：录音阶段用**设备实际速率**计时（所以时长/VAD 是对的），
但拼 WAV 头时用的是**配置里的 16000**。设备只要不是 16k（嵌入式 codec
常见 48k），48k 的样本就被标成 16k 发出去，音调语速全错 → 识别必然乱码，
而音量条一切正常。

现在：录音阶段把硬件真实速率存进 `mic_rate`，
WAV 头一律用这个真实值，并且**先重采样到 16kHz** 再发给识别接口
（`qwen3-asr-flash` 要的就是 16k 单声道）。
重采样用线性插值，`tools/test_voiceai.c` 里用 440Hz 正弦验证过
「频率不变（过零 879 次 ≈ 880）、幅度不变、时长不变」。

## 构建

```bash
# 全量（会连带 build/EXTRAS/Tools/tg5040/Voice AI.pak）
make build PLATFORM=tg5040

# 只编这一个工具，快得多
docker run --rm -v "$PWD/workspace":/root/workspace \
  ghcr.io/loveretro/tg5040-toolchain:latest \
  /bin/bash -c 'cd /root/workspace && make -C all/voiceai PLATFORM=tg5040'
```

产物：`workspace/all/voiceai/build/tg5040/voiceai.elf`（约 103KB，零 warning）

### 不需要上设备就能验证的部分

```bash
./tools/测试语音解析器.sh
```

这个脚本在宿主机上编译 **不依赖 SDL、不依赖设备、不联网** 的那部分
（`via_ui.c`），覆盖：

- **UTF-8 折行**，重点是中英混排和 CJK 断点。这块踩过两个真实的坑：
  1. 先把行尾分隔符写成 `\0` 再读它判断下一行起点 → 多行文本从第二行起全变空
  2. 折行的行指针数组用 `realloc` 增长 → 调用方拿不到新指针
- JSON 反转义（`\uXXXX` + 代理对 + 转义引号）
- base64 往返（被 3 整除 / 余 1 / 余 2 三种长度）
- WAV 表头解析，包括**流式 TTS 那种长度字段是 `0x7FFFFFFF` 假值**的情况
- 用真实抓包 `tools/tts-sample.sse` 验证 SSE 分片 + base64 累积**不丢字节**

最后一条看着像给已砍掉的功能写的，其实不是：base64 累积漏解第一块的话，
接口照样返回 200，问题只在音频里 —— 这类错最难发现，所以留了回归样本。

---

## 代码结构

```
workspace/all/voiceai/
├── via_main.c       main、配置读写、界面渲染、按键     (~660 行)
├── via_pipeline.c   录音->识别->对话 的 worker 状态机    (~420 行)
├── via_api.c        curl、OpenAI 兼容 JSON、SSE、历史   (~470 行)
├── via_audio.c      ALSA 采集 + VAD + RMS               (~250 行)
├── via_ui.c         纯逻辑：折行/JSON/base64/WAV         (~500 行)
└── voiceai.h  via_audio.h  via_api.h  via_pipeline.h
```

**线程分工**（这块最容易写出竞态，特意分离）：

- **worker 线程**：ALSA 采集 + curl 网络调用。**完全不碰 SDL 绘图**。
- **主线程**：画界面、处理按键、把 worker 写好的字段刷出来。

共享字符串走 `p->lock`，状态/计数走 `SDL_atomic_t`。
**注意 SDL2 里原子类型叫 `SDL_atomic_t`，不是 SDL3 的 `SDL_AtomicInt`** —— 写成后者编不过。

---

## 踩过的坑（完整记录）

1. **配置不能写进 `minuisettings.txt`。**
   `CFG_sync()` 会按调用方自己的内存结构把整个文件重写一遍，而卡上的
   `nextui.elf` / `settings.elf` 不认识这些键 —— 每次开机都会抹掉
   （AI 翻译那边实测：写进去 8 行，开机后剩 0 行）。所以单独放 `voice-ai.txt`。

2. **`SDL_AtomicInt` 是 SDL3 的**，SDL2 里是 `SDL_atomic_t`。

3. **折行的分隔符必须先读再覆盖。** 先把 `buf[end]` 写成 `\0`，
   再判断 `buf[break_at] == '\n'` 就永远为假 —— 多行文本会从第二行起全部变空。

4. **折行的行指针数组不能用 `realloc` 增长**，否则调用方拿不到新指针，
   之后 `free` 的还是老指针。

5. **`curl -N` 必须加**，否则流式响应会被 curl 自己缓冲住，一段段出字的
   效果就没了。

6. **curl 要加 `-H 'Expect:'`** 关掉 100-continue，否则状态码和计时都会偏。

7. **macOS 的 FAT32 驱动原地覆写大文件会偶发簇链错乱**（实测遇到过一次
   128KB 内容变成目录项数据）。所以部署脚本一律先 `rm` 再 `cp`，
   并且**逐字节 `cmp` 校验**。

8. **pak 目录里不能留 macOS 的 `._*` 文件**，NextUI 会把它们当条目显示。
   部署脚本会主动删。

9. **VAD 用均方根而不是峰值**，峰值会被单次爆音带偏。

10. **空的配置值要和「没写」同等对待**，否则部署脚本生成的
    `voiceChatModel=` 会把默认值覆盖成空串。

11. **WAV 头的采样率必须用「硬件实际速率」，不能用配置里的目标值。**
    这个 bug 的症状极具误导性：音量条正常跳动，但识别结果全是乱码。
    原因是录音按设备真实速率计时（时长对），WAV 头却写死 16000，
    于是 48k 样本被当 16k 解释，音调语速全错。
    **教训：音量条只能证明「有声音」，不能证明「音频格式对」**，
    所以现在每次录音都留样本，让人能直接听。

12. **base64 解码的累加器必须是 `unsigned`。**
    用 `int` 累积到第 4 个字符（24 位）时 `acc << 6` 会左移进符号位，
    属有符号溢出 UB。用 `-fsanitize=undefined` 实测报：
    `left shift of 473454708 by 6 places cannot be represented in type 'int'`。
    这个坑特别阴 —— 接口返回 200，只有音频内容静默损坏。
    所以 `tools/测试语音解析器.sh` 值得偶尔跑一下（它用真实抓包校验字节数）。

---

## 卸载 / 还原

删掉这个目录就干净了，没有任何其它残留（`.system` 里没动过任何文件）：

```
Tools/tg5040/Voice AI.pak/
```

想保留配置就先备份 `.userdata/shared/voice-ai.txt`。
对话历史在 `.userdata/tg5040/VoiceAI/history.txt`（只存最近 20 轮）。
