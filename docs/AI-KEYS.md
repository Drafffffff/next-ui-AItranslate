# 掌机 AI 公共配置

SD 卡文件：`.userdata/shared/ai-keys.txt`；掌机实际路径：`/mnt/SDCARD/.userdata/shared/ai-keys.txt`。

```ini
DEEPSEEK_API_KEY=你的DeepSeekKey
DASHSCOPE_API_KEY=你的百炼Key
```

复制 [空配置模板](../config/ai-keys.example.txt) 即可。值按字面读取，不加引号、`export` 或转义；一行一个字段。可留空，不要重复字段。Key 为最多 511 个可见 ASCII 字符，文件最多 16 KiB。文件包含真实凭据，不要提交到 GitHub 或分享整份配置。

## 应用约定

| 应用 | 读取内容 | 其他设置 |
| --- | --- | --- |
| 游戏 AI 翻译 / AI Play | 当前服务对应的公共 Key | `ai-translate.txt` 保留开关、服务商、模型、显示和上下文设置 |
| PocketJS 掌机聊天 | `DEEPSEEK_API_KEY` | 原 `config.json` 保留模型、接口、超时和聊天记录 |
| AI Game Workshop | `DEEPSEEK_API_KEY`、`DASHSCOPE_API_KEY` | 原 `config.json` 保留模型、接口、超时；项目和历史不变 |
| Brick Mic | ASR 请求在 Mac / Linux 执行，使用对应电脑的 Key 配置 | 掌机不接收电脑 Key，也不通过蓝牙传递凭据 |

公共文件中对应 Key 非空时优先使用；字段缺失或留空时兼容旧配置。重复、过长、包含控制字符等格式错误会阻止对应请求，避免悄悄改用旧 Key。每次请求重新读取，小文件本地读取，无网络检查或后台轮询。

原有 `ai-translate.txt` 的 `aiDeepseekKey` / `aiBailianKey` / `aiApiKey`、`voice-ai.txt` 的 `voiceChatKey` / `voiceAsrKey` 和应用 `config.json` 的 Key 保留用于回退；更新模型、接口和显示设置不覆盖公共文件。

原生请求层选择公共凭据，不把新 Key 复制进 JS 配置、会话、日志或进程参数。HTTP 公共 Key 只用于对应的官方 HTTPS 地址；自定义服务仍使用单独配置的 Key。AI 翻译的自定义模式可使用公共 `CUSTOM_API_KEY`。工坊 ASR 保留已有地域 / workspace 域名，使用 `qwen-audio-3.1-asr-flash-message` 和 `/api-ws/v1/inference`。

## 从旧配置迁移

```sh
# SSH：先检查，再迁移
python3 tools/ai-keys.py --ssh nextui-brick
python3 tools/ai-keys.py --ssh nextui-brick --apply

# 或直接操作插入电脑的 SD 卡
python3 tools/ai-keys.py --sd-root /Volumes/SDCARD --apply
```

脚本只显示字段是否已配置，不显示 Key。不同来源的 Key 不一致时停止，先检查旧配置；已存在的非空公共 Key 不覆盖。写入采用临时文件原子替换，旧配置备份在 `.userdata/shared/ai-key-backups/<时间>/` 并继续保留。尝试设置文件权限 0600；SD 卡 FAT 文件系统可能不支持 Unix 权限。

## 开发接入与验证

原生 C 应用使用 `workspace/all/common/ai_credentials.h`：`nextui_ai_read_key(provider, out, size)` 返回 1（已配置）、0（缺失或空）或 -1（无效）。provider 为 `deepseek`、`bailian` 或 `custom`。原生 HTTP 服务的 `credentials` 操作只向 JS 返回是否存在。

默认读取 `SHARED_USERDATA_PATH/ai-keys.txt`，无环境变量时使用上述掌机路径。测试可设置 `NEXTUI_AI_KEYS_FILE` 指向合成配置，不能把真实 Key 打进测试输出。

```sh
cc -std=c11 -Wall -Wextra -Werror tools/test-ai-credentials.c -o /tmp/test-ai-credentials
/tmp/test-ai-credentials
python3 tools/test-ai-keys.py
```

工坊 Go ASR helper 另有共享配置优先级、热更新、地域和模型保持、错误配置不回退的测试；HTTP 桥和语音协议使用本地合成服务验证，不调用付费模型。

跨工作区的游戏工坊接入修改见 [独立补丁](patches/workshop-shared-keys.patch)，应用于工坊工作区当前源码后重新编译；不整体替换工坊源码。

## 本次安装记录（2026-10-02）

已从掌机旧配置迁移 DeepSeek / 百炼 Key，来源一致；未更改 Key、模型或接口地域。已安装公共读取版本的游戏翻译和游戏工坊。旧版备份：`.userdata/shared/ai-updates/20261002-042847/backup/`；旧配置备份在 `ai-key-backups/`。

验证：公共读取 / 迁移单元测试、实际 C HTTP 函数的本地合成 curl、PocketJS 聊天与工坊完整本地模型回合、Go ASR 协议和共享优先级测试均通过。掌机原生读取确认两项 Key 已配置，ASR 确认使用公共百炼 Key 并保留指定模型；实际动态库解析与工坊离屏初始化通过，现有项目文件校验保持一致。此次验证没有调用付费模型。

额外核对原有游戏画面翻译：`minarch.elf --ai-credentials-status` 在初始化游戏、显示和手柄之前只读检查，调用与 MENU+X 翻译及 AI Play 相同的 `ai_key()`。掌机已分别确认 DeepSeek / 百炼来源为公共文件；再用仅该诊断进程生效的不同合成 Key 确认公共优先级，原有 Key 和设置未修改。诊断不会发起翻译请求。
