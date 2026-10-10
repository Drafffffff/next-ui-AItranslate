# AI Game Workshop / 游戏生成器

PocketJS + QuickJS + Pi Agent Core 的首个游戏项目闭环，面向 NextUI / TrimUI Brick。

## 目录和启动入口

```text
SD 卡/
  Tools/tg5040/AI Game Workshop.pak/     工坊应用
  Roms/AI生成游戏 (P8)/
    game-<项目编号>.p8                  每个项目唯一的当前游戏
    map.txt                             固定文件名 → 游戏标题
  .userdata/shared/ai-game-workshop/
    config.json                         模型 / 接口配置（兼容旧 Key）
    last-project.txt                    列表选中项 / 试玩返回的项目编号
    projects/game-<项目编号>.json       会话、草稿、当前版本、最近八个版本
    covers/game-<项目编号>.png          可选游戏封面
    previews/game-<项目编号>.png        无封面时缓存实际游戏首帧
    workshop.log
```

NextUI 根菜单中的 **AI生成游戏** 使用 `(P8)` 路由到已有 P8 运行器。项目标题变更只更新 `map.txt`，不会生成第二个游戏条目。工作文件和历史版本放在 `.userdata`，不会被菜单当作游戏。

工坊中按 **SR（Start）** 试玩：保存项目 → 完整退出 UI、释放 SDL 和手柄 → launcher 调用同一个 P8 运行器 → 游戏结束后重新打开工坊、恢复同一项目。正常退出工坊后，NextUI 自己恢复。试玩冷启动新代码；已有游戏存档不被删除。若运行器未安装完整，工坊显示原因并保留项目。

## 掌机交互

普通启动进入项目列表。已有游戏优先显示 `covers/<id>.png`；没有封面时使用同一 fake-08 core 离屏渲染的真实首帧。修改并发布游戏后会重新生成首帧。首帧只用来做缩略图，不代表玩法测试。所选图片同步到 `Roms/AI生成游戏 (P8)/.media/<id>.png`，与 NextUI 实际游戏封面查找规则一致；发布时更新、删除时清理，应用启动会补齐已有项目。

- 项目列表：上下选择，A 打开，X 新建，SR（Start）试玩，长按 Y 1.2 秒删除选中项目，SELECT 配置生成模型，B/MENU 退出。
- 项目对话：A 按住说话、松开识别；B 返回列表；X 停止录音、识别或 Agent（仅执行中显示）；SR（Start）试玩；SELECT 切换工具详情。
- 方向键上下翻页，左跳到最早记录，右回到最新消息。正在查看旧记录时保留位置，位于底部时跟随新消息。

列表每屏三个项目，留足标题和版本信息的高度。录音时对话区域缩为八行正文的高度，录音区域和底部按键提示分别占用固定空间，互不覆盖。删除进度显示在底部 Y 键的外描边，松开或切换选择会取消；删除同时清理项目会话、版本、唯一游戏、菜单映射和封面缓存，保留其他项目与配置。X 停止只在录音、识别或 Agent 执行时显示。底部固定一行按键提示；没有屏幕键盘；首页 SELECT 打开生成模型选择页。每个项目保存完整用户对话、Agent 回复和读取/模块加载/写入/检查/发布操作；SELECT 详情可以查看工具参数和结果。试玩退出后回到同一项目，正常重新打开应用仍进入列表。

对话正文采用 44px 行高、12px 段间空白。历史只增量建立消息索引，仅对当前可见片段排版；每次原生排版最多处理 2,048 个字符，换行不再受整条消息 1,024 行限制。正文小缓存最多 24 条 / 131,072 字符，另保留一条当前大消息；分页缓存最多 64 个窗口。字形直接索引，只有新字出现时才重建并上传字图。工具错误默认显示简短原因，SE 详情逐页查看完整内容。底部数字是消息块位置与总数。

文字通过真实 SSE 增量显示，流式更新的排版合并为约 180ms 一次，翻页立即响应。工具参数在流中累积，完整响应结束后才执行；中断或取消的残缺工具调用不会执行。已收到的文字保留。兼容返回完整 JSON 的模型端点。首页读取轻量项目索引，打开项目时再读取历史；旧项目首次启动自动建立索引。项目元数据文件上限16MiB，大历史按SHA256页单独保存，模型请求体上限8MiB（工坊 HTTP 响应为 16MiB），本地保存保留完整历史，模型摘要不删除本地消息。

按 SR 试玩前保存已排版的最后一页。退出游戏后优先显示该页，再恢复完整对话，其他项目的列表与封面随后加载；恢复期间暂时禁用语音修改与试玩。页面缓存绑定项目版本、更新时间和布局版本，每次项目保存都会使旧缓存失效；缺失或损坏时正常打开完整项目。缓存只用于显示，不作为 Agent 上下文，不截短原始会话。

每个新建和已有项目的 Agent 默认加载仓库 `skills/pico8-game-dev/SKILL.md`、七工具契约、精灵索引与模块目录。默认提示词大小以构建回执为准；API、玩法修复、3D、像素、音乐和完整示例通过只读 `read_skill` 渐进加载。资源在构建时编入应用，不访问任意设备文件。修改 Skill 后重新构建即可应用到所有项目；完整资源包也随应用保存在 `skills/pico8-game-dev/`。

模块位于 `modules/`：`game-loop.md` 对应状态、输入、碰撞、难度及持续修改，`3d.md` 对应射线和真实三维，`pixel-art.md` 对应精灵制作，`music.md` 对应音乐与音效。API 与社区采用记录位于 `references/`。Agent 先读取实际草稿，再根据影响范围选择模块；不按请求关键词或旧摘要自动塞入模板。模块被压缩出上下文后，可以重新加载。

另有七个完整 Lua 示例：收集、躲避、贪吃蛇、平台、推箱子、第一人称 DDA 迷宫、真实三维线框隧道，通过 `read_skill({module:"example/<id>"})` 加载。推箱子和 raycaster 是社区机制的工坊适配，相关社区源码节选与许可随示例返回。四份未经修改的社区卡带、固定版本、哈希和原许可保存在 `assets/community/`。示例只提供教学结构；已有项目的小改动应定位当前代码，保留其他玩法、精灵与声音。

`write_game` 是唯一的修改入口：新建/重构传完整 `lua`，局部修改传 `edits:[{oldText,newText}]`，两者互斥；标题、摘要和素材字段可独立提交，省略时保留。局部编辑复用 Pi v0.81.1 的 MIT 纯匹配算法（源码及来源留在 `third-party/pi-edit*`），多个替换针对同一原始源码，必须唯一且不重叠；兼容换行、行尾空白及常见引号差异，未触及的行保留。全部代码与素材校验后一次保存，失败不会写入部分结果；保存失败恢复内存草稿。工具返回实际行号差异，SELECT 详情可查看。差异计算有时间/复杂度预算，返回文本最多约 8,000 字符，过大时标记截断；完整源码仍保存。`diff@8.0.4` 与 Pi 使用版本一致，BSD-3-Clause 许可随包分发。原来的完整提交协议继续可用，工具总数为七个。

`read_game` 区分当前草稿与已发布版本；未发布草稿不会替换菜单游戏。每次 `publish_game` 都必须有匹配实际内容SHA与当前校验器版本的成功检查；修改或失败写入使检查失效，恢复会核对保存的证明。检查包含锁定z8lua语法、卡带与声音引用，最终体验由SR试玩和用户反馈确认。

开发端另可执行 `npm run verify:audio-runtime --prefix ports/pocketjs-brick/workshop`，用固定 fake-08 的实际卡带解析器和合成器检查内置/自定义音频、循环、音效共存和淡出，输出两首 WAV 试听文件。音符和 PCM 可用不代表主观音乐质量已验收。

开发端可执行 `npm run verify:examples --prefix ports/pocketjs-brick/workshop`，使用运行器固定版本的 z8lua 检查七份示例的语法、定点数及状态/输入/碰撞/重开逻辑，图形和输入使用模拟 API；这不向掌机 Agent 增加自动玩法测试，也不证明新生成游戏的渲染、性能或玩法正确。

卡带包含 12 个内置精灵，可使用 `spr`/`sspr`。`read_game` 返回素材目录及现有自定义精灵；`write_game` 可选的 `sprite_overrides` 只接受 ID `32..63` 的 8×8 像素覆盖，省略时保留所有已有像素。图像随草稿、版本和未完成发布意图一起保存，反复修改不会重置素材。旧项目仍默认加载 Skill，已有版本没有图像字段时按原来的空白精灵图恢复；下一次写草稿会加入内置素材。内置6个交互音效及2首BGM，包含有明确MIT授权的社区Kajam主题和原创双声部循环。`read_game` 只返回音频概况，`read_skill({module:"music"})` 返回音乐指南、当前完整tracker资源与曲目索引；`write_game` 接受自定义 `sfx_overrides`（16..63）和 `music_overrides`（8..63），省略时保留已有声音。音频随草稿、版本和pending保存，旧无音频版本恢复不改变原卡带；Lua通过sfx/music在游戏中播放。检查只覆盖卡带格式、代码限制和精灵、音频编码及pattern引用，不代表运行成功或玩法测试通过。

模型页五个选项按每屏四行滚动显示，当前模型标记“已启用”，尚未保存的选择标记“待保存”。首页三个项目卡片与当前模型入口、状态和底部快捷键分别占用固定空间。

底部按键栏沿用 Brick Mic 的紧凑布局：42px 高的按键胶囊、12px 水平内边距、8px 组内及组间间距，文字按内容宽度排列，所有状态保持同一行。

## Agent 头像

项目页顶部使用 80×80 的 bloub 头像，回复与错误消息只保留文字。等待为 `idle`（眼神漂移与眨眼），录音为 `wide`；识别、模型请求与工具执行都使用 `orbit`（三角形与环绕），不再显示三个点。原始轨道序列每 3.4 秒通过当前姿态平滑重播，长请求期间持续播放；从模型请求进入工具执行不重启动画。真正完成发布后为 `wink`（1.6 秒），错误为 `alert`（2.4 秒）。用户取消回到等待，不播放成功动效。文本状态仍保留，头像不代替错误原因或发布结果。

复用 [jeremy-prt/bloub](https://github.com/jeremy-prt/bloub) 的原始 TypeScript `BotEngine`、形变、眼部与装饰几何，固定 commit `b4bb3c1b5f93c7b87a2e8d620f667c4093d97749`（MIT）。八个引擎文件保持原样并保存哈希；眼部校准只适配当前使用的默认轮廓 `shape=null`，返回与上游相同的零偏移，跳过自定义轮廓校准，原算法保留为 `eyefit.upstream.ts`。来源在 `third-party/bloub/SOURCE.md`；安装包保留 `BLOUB-LICENSE.txt` 与来源说明。

PocketJS 适配在 `avatar.ts`，受限路径的软件渲染在 `workshop-avatar.c`：64×64、2 倍超采样，身体与眼洞沿用原始几何，轨道改用系统 Accent2，身体使用 Main、眼洞使用 Background；不使用上游固定黑白或彩虹配色。运行时保留一个共享静态纹理和一个活动纹理，替换时释放旧纹理；动画最多 24Hz，离开项目页即停止采样。没有引入 Vue、WebView、网络资源或独立计时器。

`config.json` 可设 `"reducedMotion":true`：每次状态改变只显示一帧，不持续播放。界面 SR 标签仍对应实际 Start 键。

源码大小不设固定字节上限：完整写入与局部 `edits` 均取消长度门槛。卡带发布与首帧预览支持超过原先 80,000 字节的卡带；首帧读取及发布预算为 8 MiB。格式、ASCII、工具权限和宿主传输/存储校验继续独立执行。每次用户请求默认最多 1,000 轮模型调用，摘要调用不占开发轮数，可按 X 取消。

## Markdown 对话渲染

项目对话页原生渲染 Markdown：标题、粗体、强调、删除线、行内代码、围栏/缩进代码块、编号/项目/任务列表、引用、分隔线、链接和简单表格。链接显示名称与地址；表格按文本行换行以适配掌机宽度。样式沿用系统颜色变量，行距与段间距保持紧凑。工具详情和代码差异保持原文，不解释其中的 Markdown。只格式化可见消息，流式回复和长代码块均支持上下分页，项目里的原始消息保持完整。

## 自动摘要与上下文压缩

每个项目默认启用 Pi 0.81.1 的上下文压缩。按用户指定的 500,000 token 上下文预算运行，预留至少 65,536 token；估算超过约 434,464 token 时，用当前选择的生成模型总结较早的对话，再保留摘要和约 12,000 token 的近期记录继续开发。估算同时计入固定 Skill、工具声明和中文文本；阈值不是精确的模型计费 token 数。Skill 每次游戏开发请求都加载，当前源码和素材通过 `read_game` 获取，不依赖摘要复述。

摘要记录作为项目 JSON 的 `compactions` 字段保存，最多保留最近八次摘要；必须保存成功才采用新的摘要。重新打开项目会继续使用最近有效摘要，后续压缩会更新已有摘要。完整 `messages` 原文保留在本地，对话页面仍可翻阅全部消息和工具操作；压缩只减少发送给模型的上下文。旧项目无需迁移；损坏的可选摘要记录会被忽略并回退到完整历史。

界面整理对话时可按 X 停止。摘要也使用流式响应并启用思考，输出上限为 16,384 token；截断时提高额度重试一次，最多 32,768 token。暂时性网络错误按下述有界规则重试，鉴权、参数和空摘要不重试；每次等待均可按 X 取消。完整历史和拆分回合都使用同一精简规则，目标 1,200 汉字以内，合并旧信息并省略已过时的调试过程，正常整理每次需求最多触发两次压缩；导入的过窗口历史使用有界分批，每批先保存检查点，再继续。Pi拆分长回合时一次压缩可能有两个摘要请求。摘要失败、被截断、取消或保存失败均不会替换原始记录，也不会继续提交过大的开发请求。若压缩后上下文仍超过预算，提示拆分需求。摘要不占用正常开发的 1,000 轮模型请求预算。

可选配置字段为 `contextWindowTokens`、`contextReserveTokens`、`contextKeepRecentTokens`；通常无需设置。大历史保存在不可变SHA256页中，不受单份16MiB快照容量限制；项目元数据和单个原生请求仍有独立预算，本功能不删除本地记录。

## 应用内语音

语音使用 **`qwen-audio-3.1-asr-flash-message`**，WebSocket **`/api-ws/v1/inference`**，遵守用户指定的 AGENTS.md。掌机本机 `arecord` 采集 16kHz、单声道 PCM16，独立静态 ARM64 helper 完成 TLS、`run-task`、二进制音频上传和 `finish-task`。无需 Mac 或蓝牙转发。

按住 A 开始，松开 A 后提交已录音频，收到 `task-finished` 后聚合最终句子并自动发送给当前项目的 Agent。X 可取消，已取消会话的迟到结果不会提交。录音最长 60 秒，等待最终结果最多 15 秒；断网、录音失败或上传过慢显示错误并允许重试。正常使用只在内存中处理音频。

在 SD 卡 `.userdata/shared/ai-keys.txt` 填入公共 Key（其他 AI 应用也使用此文件）：

```ini
DEEPSEEK_API_KEY=你的DeepSeekKey
DASHSCOPE_API_KEY=你的百炼Key
```

在现有 `config.json` 中可配置模型与地址：

```json
{
  "asrModel": "qwen-audio-3.1-asr-flash-message",
  "asrEndpoint": "wss://dashscope.aliyuncs.com/api-ws/v1/inference"
}
```

优先读取公共 `DASHSCOPE_API_KEY`；未填写时兼容应用 `asrKey`，然后依次读取系统 `voice-ai.txt` 的 `voiceAsrKey`、`ai-translate.txt` 的 `aiBailianKey`。未指定地址时可复用 `voiceAsrEndpoint` 中的 WebSocket 地址，最后使用上面的默认地址。配置地域、workspace 域名和 Key 必须匹配；迁移旧 WebSocket 地址仅改 `/realtime` 路径为 `/inference`、移除旧 `model` 查询参数，保留主机。不接受其他 ASR 模型。Key 不写入会话、日志或命令行。

## 生成模型配置

首页显示当前生成模型，按 **SELECT** 进入模型页：上下选择、**A 保存**、**B 返回**（取消未保存的选择）。配置全局生效，新一轮游戏开发及自动摘要均使用当前模型，已有项目与完整会话继续保留。菜单只读取固定选项，不联网获取模型目录、不发送测试请求、不影响语音识别模型。

开发时于 **2026-10-02** 核对官方目录，应用固定内置以下五项：

| 渠道 | 类型 | 显示名称 | API Model ID |
| --- | --- | --- | --- |
| DeepSeek | 旗舰 | DeepSeek V4 Pro | `deepseek-v4-pro` |
| DeepSeek | Flash | DeepSeek V4.1 Flash | `deepseek-flash` |
| 百炼 | 旗舰 | Qwen3.8 Max | `qwen3.8-max` |
| 百炼 | Flash | Qwen3.8 Flash | `qwen3.8-flash` |
| 百炼 | Flash | DeepSeek V4.1 Flash | `deepseek-v4.1-flash` |
| TeamoRouter | 轻量 | Claude Haiku 5.5 | `claude-haiku-5-5` |

来源：[百炼 DeepSeek V4.1 Flash](https://help.aliyun.com/zh/model-studio/deepseek-v4-1-flash)、[DeepSeek 更新记录](https://api-docs.deepseek.com/updates/)、[Qwen3.8 Max](https://help.aliyun.com/zh/model-studio/qwen3-8-max)、[Qwen3.8 Flash](https://help.aliyun.com/zh/model-studio/qwen3-8-flash)。DeepSeek 的最新 Flash 使用 `deepseek-flash`，旧 `deepseek-v4-flash` 是兼容别名。模型目录在代码 `generation-models.ts` 中维护；未来增加或升级选项需要重新构建。默认仍是 DeepSeek Flash。

首次启动创建 `config.json`：

```json
{
  "provider": "deepseek",
  "key": "",
  "endpoint": "https://api.deepseek.com",
  "model": "deepseek-flash",
  "timeoutMs": 120000
}
```

DeepSeek 默认地址为 `https://api.deepseek.com`，百炼默认地址为 `https://dashscope.aliyuncs.com/compatible-mode/v1`（北京）。请求时优先读取公共 `DEEPSEEK_API_KEY` 或 `DASHSCOPE_API_KEY`，缺失时回退当前渠道的应用 `key`；切换渠道不会把 DeepSeek Key 发给百炼。公共 Key 不回写到应用 JSON，修改后下一次请求生效。首次创建旧式 DeepSeek 配置时兼容系统 `voiceChatKey` 或 `aiDeepseekKey`。

`generationChannels` 分别保存已使用渠道的地址与旧式 Key；切回时恢复该渠道配置。保存会重新读取文件并保留 `asrModel`、`asrEndpoint`、`asrKey`、上下文预算和其他字段。已有自定义地址/模型配置不会在启动时被替换；主动保存内置选项后采用所选模型。已有百炼地域/workspace 地址保留，公共 Key 支持官方 DashScope 和六个官方地域的 workspace 域名；更换地域需让 Key 与地址匹配，可在配置文件预设 `generationChannels.bailian.endpoint`、`key`。界面不输入密钥。

Haiku 使用本机 Pi 配置中登记的 `claude-haiku-5-5` 和 TeamoRouter 的 OpenAI 兼容接口，独立设置 `generationChannels.teamo.endpoint`、`key`，不会复用 DeepSeek 或百炼的 Key。地址默认为 `https://api.teamorouter.cn/v1`；此模型名称取自用户配置，服务可用性以该渠道为准。添加选项不改变当前模型；主页按 SELECT 打开生成模型列表，选择 Claude Haiku 5.5，按 A 保存。

两个渠道的生成与摘要均使用原生 HTTP 桥的 OpenAI Chat Completions SSE 流式接口，兼容完整 JSON 响应，并启用思考：DeepSeek `thinking.type=enabled`，百炼 `enable_thinking=true`，Pi `thinkingLevel=medium`。历史工具调用保留实际 `reasoning_content`。百炼 Qwen3 使用官方推荐的 `max_completion_tokens` 统一计算思考与答案，避免旧 `max_tokens` 在思考模式下的32,768参数上限；摘要单独设4,096思考预算，生成继续使用模型默认思考深度。开发输出预算为 65,536 token（包含思考）；小于默认的自定义上下文窗口会相应缩小输出预算，500,000 token 默认上下文不变。Key 不写入项目会话、请求日志或命令行。

连接、DNS、传输中断、超时与 HTTP 408/429/500/502/503/504 最多重试三次，约 1/2/4 秒退避并加入抖动，界面显示次数且可按 X 停止。重试发送同一请求，不重新执行已完成的工具；丢弃失败尝试的文字/思考/残缺参数，避免与新响应混合。证书、鉴权、参数、容量和主动取消不重试。已发布但最终回复失败时，显示“已发布，可试玩”；相同内容重新检查和发布不增加版本。默认首字及无数据超时为120秒，持续流式响应总时长最多300秒；显式更短的测试/配置超时保留。

## 系统颜色

使用 `.userdata/shared/minuisettings.txt` 的 `color1..color7`，分别为 Main、Accent、Accent2、ListText、SelectedText、Hint、Background。默认值直接引用 NextUI 的 `CFG_DEFAULT_COLOR1..7`，保留 RGBA alpha 并转换为 PocketJS CSS 颜色。没有独立工坊配色。系统配色变更后，重开工坊即可生效。

## 构建与验证

```sh
./ports/pocketjs-brick/workshop/build.sh
npm run verify --prefix ports/pocketjs-brick/workshop
npm run verify:quickjs --prefix ports/pocketjs-brick/workshop
node ports/pocketjs-brick/workshop/verify-models.mjs
POCKETJS_APP_SOURCE="$PWD/ports/pocketjs-brick/workshop/app.tsx" \
  POCKETJS_MAC_DIR=workshop-mac ./ports/pocketjs-brick/build-mac.sh
python3 ports/pocketjs-brick/workshop/verify.py \
  --host build/pocketjs-port/workshop-mac/pocketjs-app \
  --output build/pocketjs-port/workshop-verification --recover
```

在上述 `verify.py` 命令加 `--large-source`，验证超过 2,000 行的游戏源码：包含实际的 2,000 条关卡数据和绘制逻辑，覆盖完整写入、局部修改、重开、发布与恢复；不靠空行或重复注释增加行数。

工具链、QuickJS 和 PocketJS 版本固定在 `pins.env`；Pi Agent Core/AI 固定为 PocketPi 使用的 0.81.1，npm lockfile 随源码保存。构建需 Node/npm、Go 1.23 或更新版、Docker 及 Bun 1.4.2；Bun 在构建脚本中获取。Mac 预览另外需要 SDL2/SDL2_ttf。

输出 `build/pocketjs-port/AI Game Workshop.pak`。复制完整目录到 SD 的 `Tools/tg5040/`；现有 NextUI 还需安装完整的 `Emus/tg5040/P8.pak`（`launch.sh`、`fake08_libretro.so` 和 `default.cfg`）。使用系统内置 P8 时也支持 `.system/tg5040/paks/Emus/P8.pak`。UI 与 NextUI 一致优先选择 SD 上的包；若它缺 core，先修复该包。

工坊试玩与游戏菜单优先使用同一个 fake-08 / minarch 运行器。机器已有的 `PICO.pak` 普通游戏入口也通过桥接脚本调用 P8；Tools 的 `PICO-8.pak` 及 Splore 使用用户已安装的官方 `pico8_64` + `pico8.dat`，不随本仓库分发。只有 fake-08 不可用时才回退官方卡带启动；游戏本身报错不会偷偷换运行器。官方入口复用其已有配置、控制器、收藏与存档，MENU 返回系统。AI 游戏启动前清除一次性的 resume 请求，重新读取最新 `.p8`；其他游戏继续支持原有恢复方式，旧存档文件保留。封面生成的 fake-08 core 独立于游戏试玩 readiness。

构建还输出 `AI-Game-Workshop-Brick.zip`，包含工坊及固定版本的 fake-08 P8 包。可以使用已有 SSH 连接部署，无需挂载 SD 卡：

ZIP 的 P8 和 Tools 包可直接复制；机器如果已有社区 `PICO.pak` 游戏菜单入口，还需使用下面的 SSH 部署来备份其 vendor 脚本并安装桥接。直接解压不会改写第三方 PICO 启动脚本。

```sh
./ports/pocketjs-brick/workshop/deploy.sh nextui-brick
```

部署脚本备份已有工坊，保留现有 P8 core 和配置，并刷新共享启动脚本；只有缺少运行器时才安装包内版本。随后在设备后台执行 `--workshop-ready`，检查真实运行库、系统字体、系统主题、配置和项目初始化；不会抢占显示或手柄，也不会调用付费模型。初始化日志位于 `bootstrap.log`，画面为 `bootstrap.ppm`。

只更新启动链路可执行 `bash ports/native-consoles/build-pico8.sh` 后运行 `ports/pocketjs-brick/workshop/deploy-launchers.sh nextui-brick`。它备份并更新 P8/PICO/Tools/工坊启动器，保留机器上的商业运行文件、游戏、项目与用户配置，不替换正在运行的工坊主程序。重新打开应用后使用新 wrapper。独立回归：`python3 ports/pocketjs-brick/workshop/verify-launchers.py`、`python3 ports/native-consoles/verify-launchers.py`。

本地模拟验收使用真实 Pi、PocketJS、QuickJS 与 native 文件/HTTP 服务，由本地 HTTP 服务返回工具调用。验证每次游戏开发请求默认包含完整 Skill 和必要参考、创建、发布、重启后修改、唯一 ROM、菜单映射、试玩请求、中断发布恢复及未发布草稿保留；launcher 验证使用占位模拟器，不运行生成的游戏。`npm run verify` 另验证内置素材编码、自定义精灵的局部修改/重启/历史版本/发布恢复、非法像素拒绝和旧项目兼容；另验证自动摘要、连续压缩、保存/重开恢复、取消、摘要截断和保存失败。QuickJS 回归实际触发压缩，使用 256 KiB 栈和 32 MiB 堆限制；不会调用真实付费模型。

已在 `nextui-brick` 上使用真实 DeepSeek 完成验收：创建 revision 1 → 重新启动并恢复同一项目 → 修改为 revision 2 → 保存试玩请求，全部通过。真机真实模型使用 `--workshop-live`，生成和修改各允许等待 780 秒；固定 30 秒的 `--workshop-test` 用于本地模拟回归。建议把 `POCKETJS_DATA` 和 `POCKETJS_ROMS` 指向独立验证目录，保留日常项目与游戏目录。

`--workshop-ready` 只验证真机初始化；`--workshop-live` 已验证真实模型与保存链路。掌机物理手柄和收音效果仍需人工体验。

语音协议与交互回归：

```sh
(cd ports/pocketjs-brick/workshop/voice && go test -race ./...)
python3 ports/pocketjs-brick/workshop/verify-experience.py \
  --host build/pocketjs-port/workshop-mac/pocketjs-app \
  --project-fixture build/pocketjs-port/workshop-verification \
  --output build/pocketjs-port/workshop-experience-verification
```

头像回归（使用独立输出目录）：

```sh
node ports/pocketjs-brick/workshop/verify-avatar.mjs
python3 ports/pocketjs-brick/workshop/verify-avatar-ui.py \
  --host build/pocketjs-port/workshop-mac/pocketjs-app \
  --project-fixture build/pocketjs-port/workshop-verification \
  --output build/pocketjs-port/workshop-avatar-verification
# 另加 --reduced-motion 可验证静态替代模式。
```

`verify:quickjs` 还在 32 MiB 堆 / 256 KiB 栈的真实 QuickJS 中连续采样 5,000 个 bloub 形变帧，并检查静态替代模式。`--workshop-avatar-test` 验证六种业务状态（思考与工作共用轨道动画）、30 秒等待时的循环、仅一个顶部头像、离页停止与每帧耗时；验证目录必须与日常项目隔离，不调用真实生成或语音接口。

交互回归使用模拟 ASR 和模型接口，验证封面优先/首帧回退、列表启动、消息与工具翻页、工具详情、迟到识别取消、按 A 松开后自动提交给真实 Pi 工具。它不验证真实麦克风收音或云端识别准确率。

发布先保存 durable pending intent，再替换当前卡带和标题映射，最后完成版本记录；打开项目时可重复执行未完成发布。项目文件采用同目录原子替换及 fsync。会话保存完整工具记录；文件上限 1 MiB，超过时明确返回保存失败。

## 来源

PocketJS 宿主/构建复用本仓库已提交的 `4dafc7aa` 基础设施；Pi JS 嵌入方式参考 [PocketPi](https://github.com/pocket-nexus/pocket-pi) 的 [嵌入桥](https://github.com/pocket-nexus/pocket-pi/tree/0c7e67df4a3035b8709f7692bb7cbe0db41894d7/crates/pocket-pi-embedded/js)。新增端口按仓库 GPL-3.0 许可；包中保留 PocketJS、QuickJS、Solid、Pi 和 TypeBox 的上游许可。

语音 WebSocket 依赖 Gorilla WebSocket 1.5.3；PNG 解码器来自上面固定 fake-08 源码中的 LodePNG，相关许可随安装包附带。

界面图标使用 Iconify 的 `lucide:gamepad-2` 和 `lucide:folder-plus`，内置 SVG 派生的 alpha mask，运行时使用系统颜色。来源、ISC 许可和生成脚本位于 `icons/`；不依赖掌机联网加载图标。

长对话与流式原生验收（隔离数据，不使用付费模型）：

```sh
npm run verify:transcript --prefix ports/pocketjs-brick/workshop
npm run verify:stream --prefix ports/pocketjs-brick/workshop
python3 ports/pocketjs-brick/workshop/verify-long-conversation.py --host build/pocketjs-port/workshop-mac/pocketjs-app --project-fixture build/pocketjs-port/workshop-verification --output build/pocketjs-port/workshop-long-check
python3 ports/pocketjs-brick/workshop/verify-stream-ui.py --host build/pocketjs-port/workshop-mac/pocketjs-app --output build/pocketjs-port/workshop-stream-check
```

## 玩法共创、等待回答与脚手架

新建项目立即持久化并发布宿主基础脚手架为第0版，包括标题、移动、结束、重开、循环背景音乐及操作音效；可从列表或项目页按SR试玩。首个Agent发布仍是第1版，固定项目ID与ROM。初始化也采用持久提交意图，断电可以恢复；不覆盖已有未发布草稿。

`ask_user_question({question,options?:[{label,description?}]})`暂停真实Pi工具循环，等待实际回答。A短按选择，长按语音自由回答，↑↓选择，B跳过，SE查看完整问题/参数；执行中X取消。待回答问题与已收到但尚未提交的回答一起保存，重开项目可继续，不伪造用户选择。纯讨论无需发布版本。核心Skill引导模型帮助用户完善核心动作、目标、风险取舍与反馈，复杂构思通过`read_skill({module:"gameplay-design"})`渐进加载；音乐继续保留独立模块。

首页与对话页使用同一bloub头像，等待回答时回到idle。所有颜色来自系统主题；屏幕没有键盘，移除了旧空态保存说明。

宿主采用有界资源预算：项目16 MiB、请求/卡带8 MiB、HTTP响应16 MiB、工具参数8 MiB、字形缓存8192；HTTP缓冲按需增长而非每轮预分配。生成输出65,536 token，摘要16,384、截断重试最多32,768。默认首字/无数据超时120秒，持续响应总时长最多300秒（配置范围100..300,000ms）；X可中断。原始历史始终保留，显示窗口和缓存有界。压力验证详情见`docs/WORKSHOP-HARNESS-EXPERIENCE.md`。

## 本轮 Harness 优化

完成消息与工具结果保留完整本地历史。保存改为64Ki字符分块序列化、帧间让出、原生缓冲装配后原子写入；UI不再拼接和复制整份项目JSON。完成消息的JSON编码以弱引用缓存，后续只编码新增消息，卸载项目后可回收；排队时只保留冻结消息引用；原生装配仍受16MiB总预算约束，异常时释放缓冲并保留原文件。用户输入及修改工具结果保存一次，只读工具轨迹由下一次修改或任务结束一并保存；草稿/发布仍等待自己的持久检查点。压缩后的请求只处理保留消息，缓存不可变消息的 token 估算；错误给模型保留简短原因，完整参数留在本地详情。

`check_game` 使用锁定 fake-08 的 z8lua 解析器，只编译不运行 Lua，输入预算8MiB，独立进程32MiB内存/3秒预算。字面量 `sfx/music` 引用空槽会拒绝；缺少音乐或操作音效、动态编号和摄像机重置给出检查提示。Skill 默认要求背景音乐与操作音效，音乐资源仍在独立渐进模块。语法与资源检查不证明玩法正确，存量游戏不会被自动改写。

实测与边界见 `docs/WORKSHOP-OPTIMIZATION-2026-10-02.md`。离线原生生成回归：`python3 ports/pocketjs-brick/workshop/verify-generation.py --help`；重试/上下文/持久化/声音检查随 `npm run verify` 执行。

## 本轮新增能力与边界

项目列表按R分享已发布游戏，显示二维码；联网失败自动重试，X可取消，已有同版链接可离线显示。网页服务为 https://workshop-game-share.vercel.app ，手机支持方向键/O/X、BGM、音效、暂停和重开。分享不上传完整会话或未发布草稿。当前掌机网络直连Vercel域名仍有问题，临时SSH代理只用于验收。

会话按L打开最近版本，↑↓选、A恢复、B返回；恢复仍使用同一ROM并产生一个新版本。

Agent新增 `terminal`：独立QuickJS运行同步JS辅助脚本，可使用print/console.log、readText、writeText、listFiles；当前Lua与临时文件存在内存中。它不是Linux shell或Node，不支持导入、网络、子进程和系统写入。64MiB堆、3秒解释器时限、16KiB脚本/输出与16个临时文件预算；超时或X取消停止。设备没有可用seccomp/namespaces，因此使用解释器能力隔离与降权，不宣称Linux文件沙箱。

可选择读取的系统快照限于运行器启动脚本/README/构建记录、安装的PICO8 Skill模块、固定应用日志尾部及/proc诊断别名；每份64KiB以内，凭据行遮盖，不接受任意路径或配置/密钥文件。

长历史保存为SHA256验证的不可变页与原子清单，追加不重写旧页。Pi摘要可分批并保存检查点；原文不删除，模型窗口仍为500000 token。过时源码仅从模型请求投影去重，本地详情完整。最新验收范围与未解决项见 `docs/WORKSHOP-ITERATION-PLAN.md`。
