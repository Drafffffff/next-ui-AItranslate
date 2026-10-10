# 项目工具契约

默认加载Skill入口、此契约、基本精灵索引与模块目录。复杂参考通过 `read_skill` 按需读取；模型不能打开任意设备文件。

| 工具 | 输入与结果 |
| --- | --- |
| `terminal` | 同步 JavaScript 辅助脚本：`readText("game.lua")` 读取当前源码快照，`writeText` 写临时内存文件，`listFiles` 和 `print`；独立解释器，3秒/64MiB/16KiB输出。`system_files` 可读取运行器、Skill、工坊日志和系统诊断快照（每个64KiB，最多8个），具体虚拟路径见工具说明。无任意路径、密钥、网络、进程、import 或实际文件写入能力；不是 Linux Shell/Node.js。 |
| `read_game` | 无参数；当前项目id、已发布revision、完整草稿Lua、标题/摘要、草稿是否与已发布版本不同、精灵及音频资源概况 |
| `read_skill` | 必填 `module`，值来自模块目录；返回该模块正文。`music` 还返回当前项目完整音频与内置曲目索引；`example/<id>`返回完整教学Lua及相关社区节选 |
| `ask_user_question` | 必填question，可选options:[{label,description?}]；等待实际手柄/语音回答，返回answer和可选skipped。待回答问题随项目保存，重开可继续；不能把推荐选项当成回答 |
| `write_game` | 完整 `lua` 或局部 `edits` 二选一；也可只传title、summary或 `sprite_overrides/sfx_overrides/music_overrides`。省略字段保留原值。返回 `mode/edit_count/line_count/diff/firstChangedLine/diff_truncated`，更新同一项目草稿并使前一次检查失效 |
| `check_game` | 无参数；检查卡带与素材格式，并用真实 P8 解析器校验 Lua；检查字面量声音槽位，返回 `format_checked:true,syntax_checked:true,gameplay_tested:false,delivery`；警告不能当作已测试的玩法结论 |
| `publish_game` | 无参数；须在本轮对当前草稿成功检查后调用，保存新版本并更新唯一菜单卡带与名称映射 |

`read_game → [按需read_skill] → write_game → check_game → publish_game`，每次读取真实成功结果再继续，检查与发布分开调用。写后失败的草稿仍可继续修改；未发布草稿不会改变已发布游戏。标题1..32字符，摘要最多240字符，Lua使用非空ASCII源码，不设固定字节上限；必须有 `_init/_update/_draw`。

`edits`是1..64个 `{oldText,newText}`。旧文本取自当前 `read_game`，每项唯一、不重叠，全部针对调用开始时的同一源码匹配，不能让后一项匹配前一项新写的内容。需要插入时替换包含插入位置的现有块；删除时newText为空。宿主采用Pi v0.81.1的匹配实现：先精确匹配，必要时兼容换行、行尾空白与常见引号差异；不会猜测不同的变量、数值或逻辑。匹配失败、重复、重叠、代码/素材错误均不保存部分结果。失败后重新读取源码并提供足够上下文。无变化的edits会报错。所有校验通过后才一次保存完整草稿。

例如 `write_game({edits:[{oldText:"dog_speed=2",newText:"dog_speed=1.5"}],summary:"降低狗的速度，保留其他规则"})`；具体旧值必须来自当前源码。改标题可只传 `{title:"新标题"}`，换音符可只传相应覆盖。完整重写仍兼容原来的 `{lua,title,summary}`。diff只描述代码；素材变化以覆盖字段及实际草稿为准。差异显示有开销和长度限制，`diff_truncated:true`时读取当前完整源码确认，不把显示截断当成源码截断。

精灵内置ID1..12，自定义32..63。覆盖最多32条，每条 `{id:32,pixels:[8行字符串]}`，每行恰好8个小写 `0..f`；ID唯一。不写整张gfx；省略或空数组保留现有图像。音乐的音符、声部、覆盖范围和循环格式见 `music` 模块。所有素材覆盖在校验成功后才改草稿，未列出的资源保留。

项目的源码、素材、完整会话、最近八个版本保存在 `.userdata/shared/ai-game-workshop/projects/<id>.json`；唯一菜单卡带为 `Roms/AI生成游戏 (P8)/<id>.p8`，同目录 `map.txt` 映射标题，改名不新建入口。所有工具绑定当前项目，不能指定另一条路径或项目。辅助脚本的临时文件在运行结束后销毁；实际修改仍需 `write_game`。

发布意图先持久化，再写卡带与名称映射，最后记入版本；启动恢复可完成中断的发布。格式检查不解析完整Lua，不代表玩法、渲染或性能通过。新项目由宿主创建可试玩的第0版基础脚手架；首个Agent发布为第1版。普通对话、玩法共创、提问与摘要不等于提交；依据实际发布结果答复。START试玩、A语音、X停止当前执行。

## 单次 JSON 模式：开发端参考

以下协议仅在宿主明确没有提供上述工具、要求单个 JSON 结果时使用。`scripts/build_cart.py` 校验并组装此协议；掌机工具模式不要求返回这个 JSON。

## 一项目、一游戏、一个当前入口

项目的稳定身份使用 `project_id`，标题可以修改。下一轮需求默认作用于打开的项目。

建议卡上布局：

```text
.userdata/shared/ai-games/projects/<project_id>/
  project.json          # 标题、当前版本、需求摘要、运行格式
  conversation.jsonl    # 用户需求、助手回复、真实工具结果
  current.p8            # 当前完整卡带
  draft.p8              # 本次候选，完整生成后才参与更新
  revisions/            # 撤销用快照，数量由宿主限制
Roms/AI生成游戏 (P8)/<project_id>.p8
```

游戏目录中的路径固定，修改替换同一条目。快照留在项目内部。显示名称可通过 NextUI 的名称映射实现，不能因为改标题而产生另一条游戏入口。内置资源的精灵不能覆盖已有游戏的自定义精灵；修改时基于该项目当前 `.p8` 组装。

## 给模型的任务输入

宿主注入 Skill、开发参考及本契约，然后提供：

```json
{
  "operation": "modify",
  "project_id": "cat-dodge",
  "base_revision": 3,
  "title": "小猫躲狗",
  "request": "狗跑得慢一点，增加三条命",
  "brief": "方向键控制小猫吃鱼，躲避狗，O重新开始",
  "current_lua": "完整的当前 Lua 源码",
  "recent_messages": [],
  "assets": {
    "catalog_version": 1,
    "sprites": [{"id": 1, "name": "cat", "width": 8, "height": 8}],
    "custom_sprites": [],
    "sounds": []
  },
  "evidence": {"runtime_tested": false, "player_feedback": ""}
}
```

实际 `sprites` 应带完整的可用素材索引；上述只有一条是示意。新建项目 `base_revision=0`，已有项目传真实当前版本；宿主不要让模型推断版本。长对话逐渐压成玩法摘要，但保留完整会话在本地，且每轮提供真实当前源码。API 密钥、无关项目和系统文件不进入上下文。

## 模型响应

成功生成时输出以下字段，全部必填，不允许额外字段：

| 字段 | 含义 |
| --- | --- |
| `schema` | 固定 `pico8-game-result/v1` |
| `project_id` | 原样返回宿主指定的身份 |
| `base_revision` | 原样返回请求所基于的版本 |
| `title` | 1–32 字符的中文或英文显示标题 |
| `summary` | 1–240 字符，中文概述本轮具体改动和待试玩点 |
| `controls` | 1–8 条，`button` 为 `left/right/up/down/o/x`，`action` 为短说明 |
| `lua` | 完整非空 ASCII Lua 源码；不设固定字节上限 |
| `sprite_overrides` | 0–32 条，ID 为 `32..63`，每条 `pixels` 为 8 行、每行 8 个 `0..f` 字符 |

源码字节数不是 PICO-8 的 token 上限计算方法。工坊和本脚本不设固定源码字节上限，也不计算准确的 P8 token 数或检查压缩 PNG 大小；目标运行器兼容性与宿主资源预算仍需分别考虑。

```json
{
  "schema": "pico8-game-result/v1",
  "project_id": "cat-dodge",
  "base_revision": 3,
  "title": "小猫躲狗",
  "summary": "降低狗的移动速度并加入三条命，仍需试玩确认碰撞与难度。",
  "controls": [{"button": "left", "action": "向左移动"}, {"button": "o", "action": "重新开始"}],
  "lua": "function _init()\n x=60\nend\nfunction _update()\n if btn(0) then x=max(0,x-1) end\nend\nfunction _draw()\n cls(1)\n spr(1,x,60)\nend\n",
  "sprite_overrides": []
}
```

这个示例仅展示结构和可打包代码，不实现上面摘要描述的完整玩法；实际响应的摘要必须与源码一致。

输出中不放 `__gfx__` 的整张图、不放卡带头、不放绝对路径。宿主把 `lua` 与上一版素材合并。第一次以 `assets/starter.p8` 为底稿；后续必须显式使用项目 `current.p8`，不能每次回到模板。

需要澄清时，宿主应区分“交互回复”和“产物提交”。模型只返回简短问题，不伪造符合此 schema 的空代码；这类回复不能进入卡带组装器。可以由实际 agent 框架的普通文本消息或独立澄清工具承载，不将其当游戏版本。

## 宿主更新与中断

1. 每个项目同一时间只允许一个写入任务，记录其 `base_revision`。
2. 流式响应写入临时响应文件；只有响应完成且结束原因正常，才解析完整 JSON。`finish_reason=length`、网络中断、取消都丢弃本次候选。
3. 比较项目身份和当前版本，拒绝过期响应。用组装器生成草稿，保存 Lua、素材及会话摘要。
4. 检查通过后，宿主建立旧版快照，更新该项目的当前卡带和固定游戏目录条目，再记录新版本。
5. 多个文件的更新不是一次全局原子操作。宿主需要小型提交日志和启动恢复：标明候选版本、文件 hash 与发布阶段，重启后完成或撤销未完成提交。每个文件使用同目录临时文件与 rename，避免用户看到半写入的游戏。
6. 试玩失败的版本可回退，或带上用户描述再修改。不要把“格式通过”记录为“运行成功”。

默认无需每次更新再弹一层保存确认；取消入口保持可见。录音识别结果可先展示，再由 A 提交需求。删除项目等破坏性操作在产品界面单独处理。

## DeepSeek 接入

- Skill 是我们注入的提示与资源约定。DeepSeek 不会自动访问卡上的 Skill 路径；资源读取、检索和工具分发由宿主实现。
- 支持工具的 agent 可以暴露 `read_project`、`read_game`、`read_sprite`、`submit_game`、`restore_revision` 等限定操作。只有已实现的工具才列入模型的工具 schema。
- 单次生成模式可以使用 Chat Completions 的 JSON Output，明确要求 JSON 并提供此结构示例。JSON mode 只约束 JSON 形状，不代替本契约的字段与版本校验。
- 模型名由配置或 `/models` 核实。此次面向 DeepSeek Flash；不要将旧对话工具的默认模型名或 600-token 短答预算直接用于代码生成。
- 完整请求、响应上限、超时、重试次数和 token 花费由宿主设定。自动重试只用于传输/格式失败，限制次数，避免无限生成；用户试玩反馈可以发起新的修改轮次。

官方接口依据（核查日期 2026-10-02）：[JSON Output](https://api-docs.deepseek.com/guides/json_mode/)、[Chat Completions](https://api-docs.deepseek.com/api/create-chat-completion/)、[Models](https://api-docs.deepseek.com/api/list-models/)。
