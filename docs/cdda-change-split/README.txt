CDDA I 改动分类与暂存

原始状态：仓库处于 3c77f77 的 detached HEAD，CDDA G/I 的移植代码未提交；没有可直接迁移的 CDDA 提交历史。本次创建按类别组织的归档提交，不修改原有历史。

归档分支：codex/cdda-i-brick-memory-experiments
UI 分支：codex/cdda-i-steam-deck-ui

UI：上下文手柄映射、动作面板、两行底部提示、虚拟键盘、制作材料与列表翻页、光标快移状态、快捷键装饰隐藏、中英文等宽与完整字形、菜单尺寸、侧栏首次高度与中文裁切。

内存及验证：紧凑地图放置/自适应表示、稀疏和按需地图、大地图数组、字形和压缩缓存限额、监测/分级回收、空闲页归还、地图引用/写盘释放、冷快照、物品队列/精确 RLE、车辆缓存/瞬态快照、保存失败保留与中断恢复、对应基线和失败诊断。车辆瞬态补丁等最新文件按原样归档，不宣称已经验证。

启动性能：审计缓存、预解析资源包、弹药候选、加载热点/阶段排序与诊断。不是 UI，全部留在实验归档，UI 分支不启用。

平台/正确性：Brick 电源/休眠、音频设备恢复、旧 SDL 和 GCC8/固件兼容、UTF-8 转换、SSH 工具，以及 NPC 心率默认值、JSON 边界等通用修正。它们不是纯 UI，归档保存；以后按独立需求选取。

混合文件：原 apply-port.py、build/container/package/launch 脚本和综合报告不能整体移植。patch-blocks.json 逐项记录原 apply-port.py 顶层代码块分类与原始行号；UI 分支重新生成仅含 UI 的入口。

G 版 ports/cdda-brick 全部原样保存在归档分支，标为 legacy-mixed，避免误当成 I 版 UI 或删除历史成果。

inventory.json 记录全部 268 个原始可移植文件的 SHA-256。原始文件排除 __pycache__ / pyc；build 下程序、资源、日志和存档保留本地，不提交。

这是代码分离和暂停归档，不是 Steam Deck 可运行包或新的稳定性验收。

已完成的提取检查：固定 0.I 原始源码归档哈希正确；UI 补丁两次应用结果一致；只修改 15 个已列明的显示/输入/侧栏文件，并增加 5 个 UI 文件。其余原始源码全部逐字节一致。game.cpp 的变更另与独立 sidebar-layout.py 输出比较，确认只增加侧栏测量，不包含保存、加载或回收修改。结果在 ui-replay-results.json。

UI 分支中的 input-guide/input-report/controller-ui-report 和原 HTML 是历史 Brick 交互资料；硬件编号、字体尺寸和安装说明仍需面向 Steam Deck 更新，不是当前平台运行指南。原始 build/cdda-i-port/upstream 是带内存实验的构建树，仍完整保留本地，不能作为 Steam Deck UI 版源树。

完整游戏编译、画面回归、Steam Deck 输入/音频/休眠尚未在本次分离中验证。

提取后的适配器核心：使用原 ARM64 SDK 编译并运行 input-test、input-context-test、input-wait-test，三组通过，覆盖按键、斜向、重复/释放、虚拟键盘、焦点与拔插、上下文边界、材料滚动、提示宽度、事件等待和有界诊断。无 Brick power 对象参与链接。结果在 input-core-results.json；这不代表 Steam Deck 或完整游戏已验收。首次检查的两次测试夹具遗漏保留于 build/cdda-branch-split，没有改生产代码来绕过失败。
