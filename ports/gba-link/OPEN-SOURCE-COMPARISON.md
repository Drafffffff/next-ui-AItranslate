# GBA 联机卡顿：开源方案比较与本地原型

2026-10-07，分支 `feature/gba-netplay`。用户确认卡顿发生在《马里奥网球 力量之旅》的游戏菜单和选角界面。当前掌机仍为 `30d685bd`；本轮没有更新掌机，也没有代按掌机按键。

## 结论

扩大 RFU 队列解决了一个真实的丢包缺陷，但没有达到菜单响应和动作游戏的体验要求。下一条值得验证的路线是每个设备运行两份 GBA，串口通信留在本机，网络按帧交换按键。这样可以避免把游戏内密集的串口交换变成密集的网络等待；仍需承担双核心执行、确定性校验和输入缓冲的成本。

优先研究 GBAdhoc 的 gpSP 双实例调度和本地线缆实现，mGBA Wi-Fi Link 作为协议与状态校验参考。两者都不是可以直接覆盖 Brick 核心的成品。本轮已有 mGBA 的 Mac / ARM64 编译产物及 Mac 串口测试结果，但没有可宣称网球对战通过的新版本。

## 掌机证据

两台 Wi-Fi 省电关闭，信号约 −49 dBm。日志中前端约 60.2–60.3 FPS，RFU 队列丢弃为零，峰值分别为 15 / 11。采样末段主机未及时回应累计数保持 41，客机等待超时保持零，收发持续增长。12 次掌机之间 ICMP 采样无丢包，往返最小 / 平均 / 最大为 3.321 / 8.470 / 27.845ms。

这些数据说明原有丢包修复生效，不能证明游戏每帧都完成了有效的通信或输入处理。没有测到按键至菜单变化的实际延迟，也没有测到每个游戏包的应用层往返时间；“同步时序问题”是排查方向，不是已经排他证明的根因。短时间 ICMP 无丢包不能排除实际 TCP 流的抖动。

## 源码比较

| 项目 | 实际做法 | 对当前问题的价值与限制 |
| --- | --- | --- |
| [NextUI Netplay](https://github.com/mohammadsyuhada/nextui-netplay/tree/482d86ef1af3e3eca459565e6d10814901cc96b7) | gpSP Netpacket 经 TCP 传递；NODELAY、立即发送、轮询批量收包；RFU 队列从 4 扩到 16 | 与现有方案有相同的队列修复方向；不会自动消除游戏与网络的时序耦合。源码及 ROM 能力表不能代替网球实测 |
| [VBA-M gbaLink.cpp](https://github.com/visualboyadvance-m/visualboyadvance-m/blob/master/src/core/gba/gbaLink.cpp) | 专门维护 RFU 命令状态、传输时间、待接收数据，区分普通发送与重要发送及等待；IPC 与网络路径分别处理 | 值得参考 RFU 状态机。不是把任意包透传就能等价于无线适配器；本轮仅审阅源码，未编译或进行商业游戏测试 |
| [mGBA Wi-Fi Link](https://github.com/Aelvryx/mgba-wifi-link/tree/9e919b0cfbb93af7d1171570dfc6745d00eeebab) | 每端运行 P0/P1 两份 GBA，本机 lockstep 连线；每帧交换输入，校准缓冲长度，定期比较两份状态摘要 | 可复用 Netpacket 合约；默认至少两帧输入缓冲。项目限定两人 Multi-Pak / MULTI，没有宣称 RFU 或一般 NORMAL8/32 网络支持，也未列出网球验收 |
| [GBAdhoc](https://github.com/ShoshinFauteux/GBAdhoc/tree/1964d8c06f71039a5c712a624b33aabdb9bac444) | gpSP 编译成两个拥有独立全局变量的实例，共享本地串口总线与调度器；网络交换输入，比较状态摘要 | 与现有 gpSP 更接近，并有作者记录的网球 PPSSPP 联机矩阵。但运行平台是 PSP/MIPS 与 Linux x86；ARM64 动态重编译的联机暂停/恢复钩子需要移植 |

上游 [mGBA README](https://github.com/mgba-emu/mgba/blob/master/README.md) 仍将网络线缆与无线适配器列为计划功能；这里的 mGBA Wi-Fi Link 是独立实验分支。[RetroArch 的普通回滚联机](https://github.com/libretro/docs/blob/master/docs/development/retroarch/netplay.md) 同步同一模拟系统的输入，不能直接给当前两个独立 gpSP RFU 核心补上回滚。

## GBAdhoc 中与网球直接相关的实现

[GBA-LINK-MIRROR.md](https://github.com/ShoshinFauteux/GBAdhoc/blob/1964d8c06f71039a5c712a624b33aabdb9bac444/gpsp/docs/GBA-LINK-MIRROR.md) 的网球测试记录使用真正的线缆 MULTI 模式，每帧约 15 次串口交换。进入 Multiplayer 前还会进行 NORMAL32 无线适配器探测；本地线缆实现也处理该探测。作者记录了无抖动、15ms 加减 5ms 抖动、3% 丢包三组 PPSSPP 测试及比分截图。这是作者证据，本轮没有自行重跑该矩阵，也不是 Brick 的性能证据。

需要借鉴的是完整机器调度：在 gpSP 原有事件入口暂停和恢复执行，保持周期与串口 IRQ 顺序；两份机器拥有独立 RAM、CPU、JIT 缓存与存档。不能用每帧一次普通存档切换去代替细粒度调度。第二份机器关闭画面输出、避免不必要的音频合成；ROM 共享还必须保留每个实例的可变 GPIO / RTC 页。

审阅 ARM64 stub / emitter 未见对应的 GBA_LINK 钩子，桌面原型 Makefile 明确限定 Linux / WSL x86。现有 ARM64 发射器文件的存在不能视为双实例 ARM64 路线已实现。

## 已完成的 Mac / 编译验证

mGBA Wi-Fi Link 固定在 `9e919b0cfbb93af7d1171570dfc6745d00eeebab`，未修改上游源码。Mac libretro 核心和固定 tg5040 SDK 的 ARM64 Linux 核心均编译完成。使用当前生产 `ma_netplay.c` 连接两个独立 Mac 进程，测试项目自己的连续串口 ROM：两端成功安装复制机器、协商两帧缓冲；完成数百次本地串口交换，状态摘要匹配，测试 ROM 的错误和超时均为零。结束时先退出的一端触发另一端 peer-detached，属于本轮测试终止，不能算掉线恢复玩法验收。

普通 16.7ms 定时运行时，核心报告的模拟推进约 46–51 FPS，尚未达到验收目标。去掉正常限速的容量测试约 440 FPS；这是 Mac、原创测试 ROM 的吞吐，不是网球速度，也不是 Brick 性能。尝试 Mac 交互 QoS 与取消 mGBA 收包退避均未恢复正常速度；QoS 改动已撤销，退避实验只保留在忽略目录，生产网络代码不改。尚不能把正常定时下的慢速归因于核心算力。

网球原型只运行了片头 / 标题和独立复制机器，12,178 个复制帧完成 202 次状态核对，没有完成进入比赛，串口对战计数为零。菜单按键响应与兼容性尚未通过，不能将“网络已连接”或复制状态一致当成网球可玩。原始 SRAM 使用副本，两端退出正常，未修改掌机存档。

本轮给 Mac 测试器增加可选 `NP_CORE_LOG=link`，记录候选核心的联机状态、模拟帧推进、等待与校验，避免仅看前端 FPS。默认测试器行为不变，未改 NextUI 的运行版本。

## 后续实现与验收顺序

1. 优先在独立原型实现 gpSP 双实例的 ARM64 / Mac 执行暂停与恢复，先验证相同输入下 CPU、RAM 与串口时序保持确定性。保留现有 RFU 核心作为对照。
2. 接入本地 NORMAL8/32、MULTI 和 GPIO 线缆，覆盖网球的适配器探测；以游戏入口、双方独立选角、实际得分为兼容性证据。
3. 网络只发送带帧号的按键；协商输入缓冲，周期状态校验，异常时停止双方推进；保存每人的原始 SRAM，影子存档不持久化，不把未知状态写回用户存档。
4. 在 Mac 注入持续小延迟、抖动、断线和暂停，测模拟推进、输入等待比例、等待尾延迟与菜单按键至画面变化。完成正常速度比赛后再测 Brick 的实际 CPU / 音频预算。
5. 独立候选核心经备份安装，由用户操作两台掌机验证菜单、选角、第一分及整场比赛。没有实际结果前，不声称红标或卡顿已解决。

源码克隆、编译产物、日志和汇总 `open-source-research.json` 均在本机忽略目录 `build/gba-link/`。商业 ROM、存档和游戏截图不纳入源码或公开包。
