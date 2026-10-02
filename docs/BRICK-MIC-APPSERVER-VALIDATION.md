# Brick Mic：Codex App Server 验证

更新：2026-10-02。结论：不同 App Server 实例可以串行接续同一个已保存的会话，保留原 threadId 和上下文；原实例仍加载该会话时，新实例会被写入锁拒绝。跨实例接续与接管另一个实例的活动任务是两项不同能力。

## 实测结果

| 验证项 | 结果 | 证据与边界 |
|---|---|---|
| 连接官方 App Server | 通过 | 使用桌面内置 CLI 0.159.2，新建 stdio 子进程；initialize 成功 |
| 读取本机现有任务 | 通过 | 桌面本机样本 17 个，thread/list 匹配 17 个；其他主机的 11 个任务单独排除 |
| 读取现有历史与 AI 回复 | 通过抽查 | 对已有任务 thread/read(includeTurns:true)，得到 40 轮历史和 agentMessage；未恢复或改变该任务 |
| 按任务 ID 发送、接收最终回复 | 通过 | 新建 ephemeral 测试会话，收到精确随机标记及对应 turn/completed；最终复测耗时 12.951 秒 |
| 操作审批与拒绝回执 | 通过 | 精确打印标记的命令触发 requestApproval；返回 decline 后，匹配 requestId 的 serverRequest/resolved、同 itemId 的 declined 和 turn/completed 全部收到；耗时 5.246 秒 |
| 接入桌面当前活动任务 | 未通过 | 桌面本机 2 个 active 任务在独立服务均为 notLoaded；thread/loaded/list 为 0。读取持久化记录不等于连接原运行实例 |
| 掌机按键实际批准 App Server 操作 | 未验证 | 本轮验证的是协议和隔离客户端，没有接入产品 BLE 审批适配器 |

以上耗时来自少量模型生成测试，不是百炼识别耗时或掌机按键延迟。审批只验证单次拒绝，未自动批准命令或系统权限。

## 不同实例接续同一个会话：2026-10-02

按用户优先级，先验证同一个会话能否由不同实例继续，暂不以连接桌面同一运行实例为前提。使用桌面内置 CLI 0.159.2 和新建的持久化测试会话，未恢复真实用户任务。

| 场景 | 实测结果 |
|---|---|
| A 完成首轮后，B 读取同一个 threadId | 通过，能读取 A 的持久化历史 |
| A 已回复完，但仍加载会话；B resume | 被拒绝：-32600 / active_writer |
| A 退出后，B resume 同一个 threadId 并继续 | 通过，模型准确回忆只在 A 首轮出现的随机值 |
| B 已回复完，但仍加载会话；新 A2 resume | 同样被 active_writer 拒绝 |
| B 退出后，A2 接续 | 通过，模型准确回忆 B 引入的另一个随机值 |
| A、B、A2 全部退出后，新 C 恢复 | 通过，准确回忆两个随机值，仍为原 threadId；没有 fork |
| D 正在等待审批，E 尝试接管 | E resume 被 active_writer 拒绝；steer / interrupt 均为 not_found，D 的原活动轮和审批没有被改变 |
| D 在原连接拒绝审批 | requestId 对应的 resolved、itemId 对应的 declined、原 turn/completed 全部验证通过 |

三个回忆提示均未包含待回忆的随机值，回忆轮没有执行工具；历史读取只用来检查落盘，不作为模型上下文成功的证据。首轮引入值是测试准备，不能算作回忆。B、A2、C 的实际回忆轮分别耗时 8.515、12.543、11.250 秒，这是生成耗时，未单独测量恢复接口延迟。

这批结果证明的是**释放原写入者后的同会话接续**。即使上一轮已完成，也不能只凭 idle 或 notLoaded 判断会话可接管；已加载但空闲的实例仍可能持有写入锁。没有绕过写入锁，没有在同一会话并发执行两轮。全部测试子进程已退出，两条新建测试会话已归档。

尚未证明桌面界面会立即显示外部接续的新内容，也未证明能够直接操作桌面正在等待的审批。后者仍归原运行实例所有。

## 取消订阅能否快速交接

随后用同一条脚本自建会话做无模型验证：A 恢复并持有会话，B 被 active_writer 拒绝；A 调用官方 thread/unsubscribe，返回 unsubscribed。等待 45.001 秒期间，90 次 thread/loaded/list 检查均仍含该会话，未收到 thread/closed，A、B 进程一直存活。因此这次没有观察到取消订阅在 45 秒内释放写入权。

正常关闭 A 后，B 恢复同一个 threadId 成功。没有启动模型、改变全局配置或删除写入锁，退出后重新归档此测试会话。该结果不能推论取消订阅永远不释放；协议允许卸载宽限期，也不能推论必须退出整个桌面应用。当前尚未确认可以快速释放单个桌面会话的官方入口。

## 当前桌面接入边界

- 当前宿主是 /Applications/ChatGPT.app，Bundle ID 仍为 com.openai.codex，现有按 Bundle ID 查找逻辑正确。
- 桌面实际运行的内置 CLI 为 0.159.2，Homebrew CLI 为 0.141.0；本轮以桌面版本为准。
- 桌面 App Server 通过 stdio 与宿主通信，没有 TCP 或具名 Unix 监听端点。两个版本的官方 daemon version 都确认默认 app-server-control socket 不存在。
- 未访问桌面私有 IPC，未操作 Codex 界面，未对现有任务调用 resume、fork、turn/start 或 turn/steer。
- 新启动的 App Server 可以读取同一份持久化历史，并在原写入者释放后恢复同一个会话；它不能据自身状态判断桌面写入者是否已释放，也不能承接桌面正在等待的审批。

## 推荐实施边界

优先推进不同实例串行接续同一会话：确认原写入者已经释放后，Mac 伴侣端恢复原 threadId，保留上下文；一轮执行期间保持这个实例，由它提供回复、审批、停止操作和提醒。恢复遇到 active_writer 时明确显示会话仍被占用，不能绕过锁或偷偷 fork 成另一条任务。

普通输入继续编辑用户选择的 Mac 文本框；Codex 模式的口述草稿留在 Mac 伴侣端，掌机 X 才提交到指定 threadId。直接调用 App Server 发起轮次可免去定位桌面输入框，但本轮只验证协议，没有更换产品控制层。

桌面释放所选会话、桌面显示外部新增内容和双方交接仍需验证。不能把实验中关闭测试子进程推论为必须关闭整个桌面应用。共享同一运行实例属于后续方向；它对接管活动轮和原实例审批有意义，但不是串行接续已保存会话的前提。

当前下一项卡点是**快速释放并交接所选会话的写入权**。仅取消事件订阅尚未证明足够；新实例成功恢复才能确认接管完成。目标是在桌面应用保持运行时交接所选会话。

## 可复现脚本

脚本：[validate-appserver.py](../ports/brick-mic/tools/validate-appserver.py)。只使用 Python 标准库。默认只读；--live 仅向自身新建的 ephemeral 会话发送测试消息，所有审批返回拒绝。测试子进程关闭 hooks/apps/plugins/browser_use/computer_use/multi_agent，避免影响真实掌机提醒；退出后清理子进程和临时工作目录，不修改全局配置或凭证。

本机脱敏结果保存在 build/brick-mic/appserver-validation/。实际任务 ID 的清单仅保留在本机临时目录，不放入仓库。

空清单可单独复现独立会话协议测试；读取已有任务时，清单格式为 {"threads":[{"id":"任务 UUID","status":"active","hostId":"local"}]}，必须先按目标主机过滤。

```sh
python3 ports/brick-mic/tools/validate-appserver.py \
  --codex '/Applications/ChatGPT.app/Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex' \
  --inventory /tmp/brick-mic-appserver-validation-local.json \
  --output build/brick-mic/appserver-validation/report.json \
  --live
```

协议参考：[Codex App Server](https://learn.chatgpt.com/docs/app-server)、[Developer commands](https://learn.chatgpt.com/docs/developer-commands)。协议字段以运行版本生成的 JSON Schema 为准；App Server 当前属于实验性接口。

同会话跨实例探针：[validate-shared-session.py](../ports/brick-mic/tools/validate-shared-session.py)。仅操作自己新建的持久化测试会话，记录脱敏错误类别；自动完成模型回忆、冷交接、活动轮边界及归档清理。所有审批拒绝，不操作真实桌面任务。运行会产生五个隔离测试轮次。

```sh
python3 ports/brick-mic/tools/validate-shared-session.py \
  --codex '/Applications/ChatGPT.app/Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex' \
  --output build/brick-mic/appserver-validation/shared-session.json
```

释放检查：[validate-thread-release.py](../ports/brick-mic/tools/validate-thread-release.py)。从上一批成功的本机报告取自建且已归档的测试会话，取消订阅后最多观察 45 秒，不启动模型；结束后关闭子进程并重新归档。

```sh
python3 ports/brick-mic/tools/validate-thread-release.py \
  --codex '/Applications/ChatGPT.app/Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex' \
  --source-report build/brick-mic/appserver-validation/shared-session.json \
  --output build/brick-mic/appserver-validation/thread-release.json
```
