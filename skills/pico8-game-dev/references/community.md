# 社区实例与公开 Skill：来源和采用记录

核查日期：2026-10-02。本表记录实际审阅的固定版本；不是对互联网上所有 Skill 的穷尽清单。源码、许可、hash 和固定提交保存在 `assets/community/sources.json`。所有外部指令按参考资料处理，不安装它们的工具或执行流程。

## 社区原作

| 原作 | 来源与许可 | 借鉴点与适配边界 |
| --- | --- | --- |
| Pico8 raycaster / 9th Kajam | [M2tias](https://github.com/M2tias/pico8-raycaster/tree/c230dbc302b29378bf43282d36c6626e104b37b4)，MIT | 方向＋相机平面、DDA、垂直投影深度与侧面明暗；原作有纹理、地图旗标、音频、门与内存配置。适配例保留射线思想，使用 Lua 地图、有限 DDA 和零分量保护；原作音乐的6段结构及SFX 4..9也适配为可直接播放的内置BGM，编号重映射、音量限制与来源见 `assets/audio.json`。不能直接把原卡带交给当前发布工具 |
| Cornell Box / pico8-shadows | [freds72](https://github.com/freds72/pico8-shadows/tree/64a2fae6c8c4583d8436d8304b714f8fd4119d19)，MIT | 真实三维顶点、相机空间变换、投影前裁剪、面深度排序和扫描线填充。保存原作作为更进阶的多边形参考；当前线框短例没有面遮挡和阴影，不直接复制原作模型解包、内存配置、调试 flip 或 Blender 工作流 |
| Snak | [megabyte112](https://github.com/megabyte112/Snak/tree/9428455f3236238dda4dcf0d79c58a8f5a16d118)，GPL-3.0 | 将下一个蛇头位置与蛇身比较，注意离开的尾部、输入节拍和不同状态。原作的音乐、内存操作与特殊输入不属于工坊子集；递归随机生成食物在接近满盘时风险增大，适配例改为空格枚举 |
| Pico8 Sokoban Framework | [Woj-Rak](https://github.com/Woj-Rak/Pico8-Sokoban-Framework/tree/ec3f873ad326c51afd9e0b4a86501fd939db1ade)，MIT | 箱子身份/位置、箱子后方先判断能否进入、全部目标的完成判断；原作依赖地图和旗标。适配例把棋盘、箱子和目标分开存在 Lua 里，加入有限撤销历史 |

原始卡带分别保存在 `assets/community/<id>/original.p8`，保持原字节与各自 LICENSE。它们是离线教学资料，默认不进入游戏菜单，也不把整份图像、音频或 20KB 源码灌入上下文。模型得到与当前玩法相关的源码节选、来源标记和适配提示。

`assets/examples/` 的短例是工坊兼容示例，不冒充社区原作。raycaster、推箱子注明社区算法/结构参考；其他短例为原创。借鉴社区源码要保留作者和许可，不把“源码公开”自动解释成可以复用所有素材。新生成的用户游戏需要满足其实际使用的第三方代码许可。

## 公开 Skill 与评价指南

| 审阅来源 | 采用到本 Skill 的部分 | 当前工坊的调整 |
| --- | --- | --- |
| [nutshot2000/pico8-dev](https://github.com/nutshot2000/pico-8-mcp/blob/61537fee997db58ccba9b45c40078933a3924fd9/skills/pico8-dev/SKILL.md)，MIT | 写入后的验证循环；数字溢出、全局初始化、按住重复输入、对象上限；16×16 精灵占四格 | 采用 read/write/check/publish；没有 simulate、capture、MCP 音频或性能数据，结束反馈必须留给用户试玩。原作某个自动射击游戏的升级/敌人/Boss 时长不变成通用规则 |
| [Loupax/implement](https://github.com/loupax/coffee-pool/blob/0efbb3633e0df3868aed13a225d31faab5e75f1d/.claude/skills/implement/SKILL.md)，MIT | 修改前分析物理、计分、状态、UI 等受影响范围；实现后对照目标复查；考虑预算并报告事实 | 合并为单个掌机 Agent 的短流程。没有开发/QA 子 Agent，不生成虚假的独立 QA PASS；社区示例不影响当前项目身份 |
| [willibrandon/Pixel Art Professional](https://github.com/willibrandon/pixel-plugin/blob/dee350645b705916655c013f208bf5580ecb9317/skills/pixel-art-professional/SKILL.md)，MIT | 固定调色板、少量明暗层、明确光源、前后景对比、只在适合的位置使用抖色 | 直接生成 8×8 颜色索引；不调用 Aseprite、不更换硬件调色板，不把 RGB/PNG 当作精灵参数。小人物优先轮廓和可读性而不是堆纹理 |
| [YinsenW/retro-game-mechanics](https://github.com/YinsenW/retro-game-mechanics/blob/d975ec4c48e8142109829f6abac6d3c3bec3bb7c/SKILL.md)，未声明许可 | 思路参考：区别源码观察、玩家反馈和设计推断；理解更新顺序、状态与规则之间的依赖 | 用原创表述融入诊断；不打包原文或脚本，不把静态理解当运行证据；不引入 ROM 反编译工作流 |
| [louiese-mady/pico8-skill-eval](https://github.com/louiese-mady/pico8-skill-eval/blob/cb3e2ccf139ead6f7ef49a651a2c30ecc4ed55a9/docs/QA_GUIDELINE_AND_PROMPTS.md)，MIT | 选择可读且有真实玩法的例子；按完整语义单元阅读源码，保留初始化和数据定义；每项判断落到具体机制 | 固定保存社区原作，只注入相关小节；`read_game` 仍返回完整当前 Lua。不采用其研究语料的最低复杂度、分数或代码数量要求作为用户游戏规范 |

本 Skill 的正文和场景参考已经包含适用规则；这份记录负责追溯来源，不作为另一个要求模型执行的工具手册。

## 本机游戏库研究

2026-10-02通过只读SSH提取18款掌机卡带，17款完成指定机制的源码审阅，Driftmania仅初步识别。卡带hash逐一与本机实际文件核对；作者、原发布页、提取Lua hash、LF行号、函数和观察收录在 [library-cases.json](library-cases.json)。这不是全库审计、游戏试玩评分或模型质量评测。

方法归纳入口为 `library-patterns`，按需读取平台、网格回合、赛车物理、战斗与关卡模块；3D、像素和声音细节继续保留在各自模块。只发布原创归纳和来源元数据，不将此次提取的原作源码/卡带/素材纳入Skill包。已带明确许可的既有社区节选和完整参考继续保留。
