# 游戏生成器 Agent 实验室

Mac 已搭建完成。入口调用生产 `projectAgent`，经过真实 PocketJS/QuickJS、Pi、原生 HTTP/SSE、文件服务、PICO-8 语法检查和发布服务。Python 线程池调度多个独立原生进程；每个案例拥有独立的项目、会话、配置、ROM 和 fake-08 进程。

## 已有报告

- [流式回收后的真实六类评测](../../../../build/workshop-eval/runs/live-streaming-fixed/report.html)：DeepSeek Flash，实际 4 路并行，平台、赛车、Boss、3D、局部减速与局部跳高的交付检查 6/6 通过。平台与 3D 累计响应约 19.6 / 18.5 MiB，原先的 16 MiB 累计限制已被跨过；Mac 单请求缓冲分配峰值最高 64 KiB。此报告使用 `streaming-buffer-v2`；模型代码评审中局部减速的指标格式不符合 rubric，被保留为评审失败，未据此伪造评分。
- [最终原生版本的真实修改重复测试](../../../../build/workshop-eval/runs/live-final-native-reassessed/report.html)：`streaming-buffer-v3`，两种修改各重复两次，修正等价表达式检查误报后 4/4 通过。原始 [3/4 报告](../../../../build/workshop-eval/runs/live-final-native/report.html) 保留；一例将 `dx*1` / `dy*1` 写成 `dx` / `dy`，两处变化之外的源码、素材、声音完全一致，实际 VM 速度与重开断言通过。复评没有再次生成、改写卡带或重放 VM。
- [最终 Mac/ARM 传输对照](../../../../build/workshop-eval/runs/streaming-verified/report.html)：32 MiB SSE 在两端均完成，JS 包、提示词、工具参数与卡带字节一致；9 个离线场景中 8 个成功，唯一失败为故意注入的断线，未执行工具。该压力夹具不代表模型生成质量。
- [真实 DeepSeek Flash 基线与代码评审](../../../../build/workshop-eval/runs/live-baseline-reviewed/report.html)：两个案例同时运行，机器检查 2/2 通过。新建平台游戏 Agent 耗时约 209 秒，精确减速约 5.4 秒；整轮耗时还包含回放和评审。保存了卡带、源码、提示词、工具轨迹、截图和声音。
- [当前环境对照](../../../../build/workshop-eval/runs/validated-environment/report.html)：六个离线案例，实际并行度 4；Mac/掌机的新建路径使用字节相同的 Agent JS 包、相同提示词、工具参数与成功路径，生成卡带字节一致。
- 局部修改、素材保留、语法错误修复、提问后继续、游戏运行时报错、超时清理子进程，以及重复运行和提示词对比，已通过 `eval:verify` 验证。故意制造的游戏崩溃必须在报告中显示失败。
- 新增原生回归覆盖超过 32 MiB 的 SSE、普通 JSON 回退、断线时禁止执行未完成工具；`eval:verify` 共 13 类场景，其中故意崩溃与断线应显示失败，验证程序确认这两种失败后通过。

真实报告保存的是当次源码与 Skill 指纹；后续代码迭代不会改变旧报告的含义。模型代码评审只是待核实意见，行号范围检查不能证明意见正确。不能将这两个样本的通过率推广为整体游戏质量。工具交付警告（如动态声音编号、镜头重置位置）单独列为待核实，避免把有效代码当作硬错误。

## 使用

已编译好一个独立快照，可立即运行，避免其他任务修改工作树干扰本次测试：

```sh
npm --prefix ports/pocketjs-brick/workshop run eval:run -- \
  --snapshot prepared --live --ssh-config nextui-brick --workers 4
```

`prepared` 指向最近成功构建的快照。修改提示词后，重新 `eval:freeze --build` 会更新这个指针；原报告和快照保留。

在仓库根目录运行：

```sh
# 代码、Skill、依赖变化后重新构建
npm --prefix ports/pocketjs-brick/workshop run eval:build

# 离线验证环境，不调用收费模型
npm --prefix ports/pocketjs-brick/workshop run eval:verify

# 真实提示词评测，读取掌机当前生成模型配置与共享 Key
npm --prefix ports/pocketjs-brick/workshop run eval:run -- \
  --live --ssh-config nextui-brick --workers 4 --label baseline

# 只评测两个案例，可加模型代码评审
npm --prefix ports/pocketjs-brick/workshop run eval:run -- \
  --live --ssh-config nextui-brick --workers 2 \
  --case platform-create --case speed-change --judge --label focused

# 离线验证 Mac/ARM 原生路径一致性
npm --prefix ports/pocketjs-brick/workshop run eval:run -- \
  --workers 4 --parity-ssh nextui-brick --label parity
```

默认生成 `build/workshop-eval/runs/日期-标签/report.html` 和 `summary.json`。直接在浏览器打开 HTML，随整个结果目录一起保存或分享，才能保留源码链接和音频。每个案例完成后报告会更新，打开中的页面需要刷新。报告脚本的导航、失败筛选、搜索和空状态经过 DOM 单元验证；由于内置浏览器拒绝 `file://`，未做浏览器布局截图检查。

需要一个可单独移动的 HTML 时，导出完整的离线报告；截图、音频以及卡带、源码、工具轨迹、项目历史和提示词下载都内置，不需要旁边的 `cases/` 目录：

```sh
npm --prefix ports/pocketjs-brick/workshop run eval:export -- \
  build/workshop-eval/runs/live-streaming-fixed \
  --output build/workshop-eval/report-standalone.html
```

导出命令的相对路径按仓库根目录解析，不受 `npm --prefix` 改变工作目录影响；输出必须为新文件。单文件版通常比普通报告大，因为包含全部声音和证据。

输出目录必须是新目录，避免覆盖历史结果。

默认不带 `--live` 时使用脚本化 SSE 响应，验证真实工具链；它不评估模型生成能力。真实评测会使用 API 额度。`--workers` 支持 1–16，默认 4；`--repeat` 支持 1–20。并行任务重叠时间会记录在图表中，不只记录设定的线程数。模型限流和重试使用原 Agent 实现。

## 与其他任务同时开发时固定评测版本

如果其他任务正在修改同一工作树，构建前后校验会拒绝混合版本。可用 APFS 独立复制快照，把本次输入固定下来再构建；不使用指向可变源码的硬链接或软链接，不复制旧项目、ROM、录音或密钥数据。

```sh
npm --prefix ports/pocketjs-brick/workshop run eval:freeze -- \
  --output /absolute/path/workshop-eval-snapshot --build

npm --prefix ports/pocketjs-brick/workshop run eval:run -- \
  --snapshot /absolute/path/workshop-eval-snapshot --live --ssh-config nextui-brick --workers 4 \
  --output /absolute/path/new-report-directory
```

快照包含当时的生产源码、Skill、锁定依赖与公开运行时源码。复制后再次校验输入 hash，后续 Mac 构建和 ARM 对照都从这个快照运行。报告指纹指向所测快照，不能声称它包含复制后的新修改。修改提示词后捕获另一个快照，用同一组案例比较即可。未完成或校验失败的快照应重新捕获到新目录；原工作树不受影响。

## 有针对性地迭代

先固定案例、模型与重复次数建立基线，检查具体失败和对应产物。一次只修改一个提示词模块或 Agent 行为，再跑相同案例。建议至少重复三次；单次成功和模型评分不证明稳定改善。

```sh
# 基线：三个重复样本
npm --prefix ports/pocketjs-brick/workshop run eval:run -- \
  --live --ssh-config nextui-brick --workers 3 --repeat 3 \
  --case platform-create --label platform-a --output /tmp/workshop-platform-a

# 修改现有 Skill/Agent 后：先 eval:build，再运行并比较
npm --prefix ports/pocketjs-brick/workshop run eval:run -- \
  --live --ssh-config nextui-brick --workers 3 --repeat 3 \
  --case platform-create --label platform-b --output /tmp/workshop-platform-b \
  --compare /tmp/workshop-platform-a/summary.json

# 只追加实验提示词，不改掌机默认提示词
npm --prefix ports/pocketjs-brick/workshop run eval:run -- \
  --live --ssh-config nextui-brick --case platform-create \
  --prompt-append /absolute/path/variant.txt --label variant \
  --compare /tmp/workshop-platform-a/summary.json
```

`--prompt-file` 可完整替换 system prompt，与 `--prompt-append` 互斥。默认完全使用生产提示词，渐进加载模块仍走真实 `read_skill`。每一轮的实际提示词存为 `prompt-N.txt`，报告保留 hash。比较按案例 ID 与重复编号匹配；修改模型、Skill、工具代码或案例后，不能把结果变化全归因于提示词。

修正客观评测规则后，可用 `--reassess-run` 对保存证据重新判定，不花模型额度。输出新目录，保留原始报告、生成版本、卡带 hash、用量与原始 VM 回放；复评记录保存原报告和新判定程序的 hash。请求和回放步骤必须相同，不能借复评把不同需求或未运行的断言写成已验证。

```sh
npm --prefix ports/pocketjs-brick/workshop run eval:run -- \
  --reassess-run /absolute/path/live-final-native \
  --label corrected --output /absolute/path/new-corrected-report
```

局部减速只接受测试案例明确列出的 `dx*1` / `dx` 与 `dy*1` / `dy` 组合；其余源码逐字一致。没有通用的“语义相似即通过”判断。额外更改时间、错误速度或重写其他逻辑仍会失败；该判定边界纳入原生回归验证。

项目历史使用生产加载函数恢复；报告同时保存原始分段历史清单和完整对话视图，读取时验证每段 hash 与消息数量。若历史包含已知密钥，报告会脱敏并标记 `history_redacted`。

报告区分 Agent 耗时、整轮评测耗时、首次流式输出延迟、Agent token 用量、工具错误、加载的 Skill 模块、客观检查和体验待试玩项。对比表的耗时使用 Agent 耗时，避免把外部评审耗时混入模型生成速度。

## 案例与多轮对话

默认 [`cases.json`](cases.json) 包含平台、赛车、Boss 战、3D 地牢、局部减速和局部跳高六个案例。新建案例有各自的代码评审判据；修改案例另外检查精确源码差异、精灵和音乐保持不变，并通过真实 VM 断言检查位移、胜利、重开和按住跳跃键。

复制该文件，用 `--suite /absolute/path/suite.json` 选择自己的用例。`turns` 可以包含多条需求，评测会在每轮后重新读取同一个保存项目再修改，保留历史、上下文和压缩记录。不会为下一轮创建另一个项目。`answers` 是 `ask_user_question` 的预设用户回答；缺省回答明确为“自行决定并完成”，并记录 `scripted-user-response`，没有声称是真人回答。

```json
{
  "schema": 1,
  "cases": [{
    "id": "iterative-collect",
    "name": "同一收集游戏的连续修改",
    "category": "多轮",
    "seed": "collect",
    "turns": [
      "把猫的速度从每帧1.5像素改为每帧1像素，其余保持不变。",
      "时间限制从30秒改成45秒，保留之前的速度、所有图像和声音。"
    ],
    "rubric": ["之前的速度修改仍存在", "重开后的时间为45秒"],
    "expect": {"unchanged_sprites": true, "unchanged_audio": true},
    "replay": [
      {"frames": 1, "mask": 0, "name": "title"},
      {"frames": 1, "mask": 16, "name": "start"},
      {"assert": "assert(state=='play' and left==45)", "name": "reset-time"}
    ]
  }]
}
```

以上自定义案例用 `--live` 测试；内置离线响应只覆盖默认案例和指定故障夹具，不是任意需求的模型替代品。`seed` 使用 Skill 中的原始社区/维护示例资源，通过生产保存和发布服务建成初始项目。

按键 mask 为左 1、右 2、上 4、下 8、O 16、X 32，可相加组合。`frames` 是游戏逻辑帧：`_update` 为 30 Hz，`_update60` 为 60 Hz。回放使用种子 42，在原 `_init` 前设定；游戏自行 `srand`、读取时间或加载外部卡带仍可能破坏确定性。Lua 断言属于可信评测代码，可以设置边界状态；报告与文件会保留这些操作，不能把设置胜利边界描述成正常通关。

自定义新游戏不能依靠猜测变量名写规则断言。可以固定一个参考游戏测试精准修改，或先人工审查生成游戏，再追加它专用的回放断言。通用回放只证明能启动并接受一段输入，不能自动证明每个关卡可通关、视觉正确或好玩。

## 可选代码评审

`--judge` 在 Agent 结束后进行独立的文本代码评审。评审请求不改变 Agent 的生产模型参数和可用工具。评分 0–4、有逐条依据与行号，引用只验证范围。评审 token 单独保存在 `rubric_review.usage`，不混入 Agent token 汇总。

已有运行可重新评审，无需再次生成游戏；输出仍写到新目录，保留原始报告：

```sh
npm --prefix ports/pocketjs-brick/workshop run eval:run -- \
  --live --ssh-config nextui-brick --workers 2 \
  --review-run build/workshop-eval/runs/live-baseline \
  --label reviewed --output /tmp/workshop-reviewed
```

评审使用 JSON 输出并关闭独立评审请求的 thinking，避免推理耗尽 JSON 输出预算。DeepSeek 参数依据：[JSON Output](https://api-docs.deepseek.com/guides/json_mode/)、[Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode/)。模型评审不看截图、不实际操控游戏，也不能作为自动验收意见。

## 当前评测发现的实际问题

[当前生产 Agent 的真实评测](../../../../build/workshop-eval/runs/live-current/report.html) 的两个并行样本中，局部减速通过，新建平台游戏失败。源码、Skill、提示词与模型配置使用固定快照，失败真实保留。

新建流程先完成 `read_game` 和所需 Skill 读取，下一次模型流式响应累计约 16 MiB 后，原生 HTTP 服务返回“响应内容过大”，尚未执行写入或发布。`brick-services.c` 的 workshop 响应上限是 16 MiB，接收代码累积完整响应，包括 SSE 包装。这是传输/缓冲限制的证据，不能把它解读为 Lua 玩法生成错误。适合下一轮先验证已消费 SSE 数据的回收或分段处理，再评估提示词的生成质量；本次没有改变生产模型的输出预算或隐藏失败。

后续 `streaming-buffer-v2` 快照已修复上述传输机制：识别 SSE 后逐段交付并回收已消费字节，只保留未交付数据、未完成 UTF-8 后缀与待校验的 curl 状态尾部。完成结果用 `streamTail` 标记剩余正文，Agent 不再从尾部重复减去全部已接收长度。普通 JSON 回应仍保留完整正文，16 MiB 未消费数据与普通响应上限仍在；模型、提示词和输出预算未为本次修复调整。断线与取消仍不执行未完成的工具参数。

每个案例记录原生 `serviceMetrics()` 的累计接收量、回收量、单请求缓冲分配峰值，报告案例内可展开“流式响应缓冲”。该指标与硬件 `metrics()` 分开，避免同名接口覆盖。缓冲峰值不是整个进程的内存，也不能代替掌机 UI 帧率测试。

`streaming-buffer-v3` 进一步处理掌机批量解析的性能：SSE 用游标一次扫描交付文本，避免每处理一行就复制全部剩余内容；原生每次交付最多 64 KiB，未消费队列达到 256 KiB 时让管道施加背压，连 HTTP 已结束时的剩余正文也分块交付。仍保留最终完成检查，完整工具参数不会提前执行。最新 `prepared` 快照指向该版本；13 类原生回归及真实修改案例按此版本另行验证，不能把 V2 报告写成 V3 的生成结果。

大流量 ARM 回放使用与生产相同量级的 120 秒配置超时（长流式请求沿用生产总时限），避免用小夹具的 10 秒超时测试 32 MiB 跨 SSH 传输。先前使用短超时的失败保留在 [传输压力测试首轮](../../../../build/workshop-eval/runs/streaming-transport/report.html)，其掌机日志也暴露了旧解析器的长帧；不会把它隐藏或改写成通过。

ARM 构建复用已安装的固定日期 Rust 工具链，缺失时才安装；无需每次评测都访问下载服务器。快照仍固定原生代码与 JS 包；构建入口文件也加入后续快照的源文件指纹。

失败请求未收到完整 provider usage，所以报告 token 仅含已返回的用量，不估算完整消耗。仍可运行的卡带是初始脚手架，报告的“新版本发布”和“修改源码”检查明确失败，不会把它当作生成完成。

## 环境一致性的范围

| 环节 | 行为 |
|---|---|
| Agent、提示词、工具、素材、Pi | 引用同一份生产 TypeScript 与锁定依赖，默认不替换工具实现 |
| JS 运行时 | 同版本 QuickJS、256 KiB stack，沿用生产堆设置；不是 Node.js Agent 模拟 |
| HTTP、SSE、编辑、语法检查、发布 | 同一原生 C/C++ 实现，分别编译为 macOS 和掌机 ARM |
| 模型与上下文 | 可从掌机读现有生成配置，保留 endpoint/地域/key 匹配；默认生产 500000 token 上下文与自动压缩逻辑 |
| fake-08 | 相同锁定 VM、Lua 方言、图形、输入和音频代码，使用无窗口 I/O host 作自动回放 |
| 多任务隔离 | 每个 Agent 和回放器分别是独立进程，没有共用 Pi 会话或 fake-08 全局状态 |
| ARM 对照 | SSH 临时目录运行同源 ARM 包，通过离线 SSE 对照提示词、工具参数、成功路径和卡带字节 |
| UI、录音、ASR、系统菜单与性能 | 不属于此无界面质量评测；需要掌机测试，Mac 的 CPU 耗时不代表掌机帧率 |

语音识别约定未修改。这里的 `turns` 对应 ASR 完成后的文字需求；没有绕过或替换掌机 ASR 的声称。

构建回执保存实际 native/JS/资源/检查器/回放器 hash，以及 Agent、共享服务、Pi、QuickJS、fake-08、Skill 源码指纹。源码、依赖或产物发生变化时，运行会要求重新构建，不会默默测旧程序。历史报告保留当时的回执。

`--parity-ssh` 使用隔离目录 `/mnt/SDCARD/.userdata/shared/workshop-eval/<唯一ID>`，不部署到 Tools，也不修改真实项目或游戏库。使用 SSH 反向转发连接 Mac 的离线 SSE 服务，结束后清理当次临时目录。默认对照所选案例中的第一个案例；选择 `--case speed-change` 可验证局部编辑路径。Mac/ARM 对照不保证远端模型抽样相同。

## 配置与限制

`--ssh-config nextui-brick` 只读取生成模型相关配置和共享 Key，不打印、复制或写入报告中的语音 Key。也可用 `--config` 指定生产格式本地配置，或使用 `WORKSHOP_EVAL_MODEL`、`WORKSHOP_EVAL_PROVIDER`、`WORKSHOP_EVAL_ENDPOINT` 与 `DEEPSEEK_API_KEY` / `DASHSCOPE_API_KEY`。临时密钥文件权限为 0600，案例结束后删除。不会把 Key 放在命令行参数里。

`--model` 同渠道可以覆盖模型；跨 DeepSeek/百炼渠道必须已有明确的 channel 配置，不猜测地域域名或密钥。当前模型清单来自生产模型配置文件。本轮实际验证了 DeepSeek Flash；百炼真实运行尚未单独验证。

评测输入和工具轨迹使用原生 project 文件与分块写入，沿用 16 MiB 文件上限，避免误用普通 1 MiB 配置文件上限。分段历史独立保存；极大规格或单次评测轨迹仍应拆分。

单案例超时默认 900 秒，可用 `--timeout` 改为 1–3600 秒。回放器另有 30 秒 watchdog；超时会清理整个进程组，包含 native 发起的子进程。中途失败的模型输出、已发布卡带、工具错误都应如实保留，而不是改成通过。

此 Mac 已有 SDL2/SDL2_ttf、pkg-config、Clang、Python 3、固定 Bun、Rust 和源码缓存。新 Mac 需要先按上级 [PocketJS 开发说明](../../README.md) 准备固定构建输入，运行 `npm ci` 安装 workshop 的锁定依赖；ARM 对照还需 Docker/toolchain 与可访问的 SSH 掌机。无需额外 Python 包。
