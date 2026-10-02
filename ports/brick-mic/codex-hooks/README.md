# Codex 任务提醒、回复和操作确认

运行 `python3 install.py` 配置，然后在 **Codex CLI** 打开 `/hooks`，审核并信任 Brick Mic 的脚本和定义。更新定义后需要重新审核；首次信任必须在 Mac 完成，不能由尚未信任的掌机桥接替代。安装脚本保留其他 hooks，并在修改前备份；不修改信任记录。

脚本将任务 ID、轮次 ID、工作目录和事件发送到本机 Brick Mic。Stop 同时传递官方 `last_assistant_message`，供掌机按需阅读当前任务上一段 AI 回复；不读取 transcript 文件，也不发送提示词。每个任务只保留最近一段，最多 12,000 字符，超过会明确标记截断；Mac 本地文件权限为 0600，蓝牙每页最多 500 字符 / 2,000 字节。

PermissionRequest 使用同步 hook，掌机在 Codex 模式按 Y 打开任务中心并进入待确认操作，查看任务、工具和操作内容后明确选择允许或拒绝。每次选择只适用于该请求；默认选择拒绝，不切换当前任务、不聚焦 Mac 窗口、不自动发送。敏感内容被隐藏或操作太长时，掌机只能拒绝，允许需回 Mac 查看完整内容。等待上限 90 秒；蓝牙断开、应用退出、请求过期、任务取消或连接变化时返回 `{}`，恢复 Codex 原有 Mac 审批流程，绝不默认允许。

桥接仅监听 `127.0.0.1` 随机端口，使用每次应用启动生成的 token；端口配置文件权限为 0600。审批还绑定当前可信 Brick 的连接 token、请求 ID、任务 ID 和轮次 ID，旧连接或重复按键不能再次审批。录音期间不处理审批按键。

任务 ID 与标题来自官方 Codex app tools 的 `list_threads` 快照，仅接收本机 Codex 桌面任务。快照原子写入 `~/Library/Application Support/Brick Mic/desktop-task-metadata.json`（0600）：`schema_version=1`、`source=codex-app/list_threads`、`host_id=local`、`generated_at`（Unix 秒）以及 `threads`（id/title/status/updated）。SessionStart、UserPromptSubmit、Stop 更新运行状态和回复，不能把独立 CLI session 或 cwd 名称登记为桌面任务。新增桌面任务需重新导出官方列表；缺少身份来源或无法确认任务输入框时不会发送。当前 Stop 接入表示正常轮次结束，不宣称能捕获所有异常退出。

使用官方 [Hooks](https://learn.chatgpt.com/docs/hooks) 和 [任务深链接](https://learn.chatgpt.com/docs/reference/commands)。仅本机任务，任务列表每页 12 个，左右翻页。
