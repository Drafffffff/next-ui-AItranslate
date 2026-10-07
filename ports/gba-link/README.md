# NextUI GBA Link 实验更新

目标平台：TrimUI Brick / tg5040。研发分支：`feature/gba-netplay`。当前交付是 Wi-Fi 两人原生 GBA 联机，不是共享按键或两人控制同一个模拟器。每端运行自己的游戏、自己的存档；仅交换 gpSP 的模拟串口 / Wireless Adapter 数据。

## 当前结论

优先采用现有 minarch + 固定新版 gpSP，接入 libretro `RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE`。不需要把系统换成 RetroArch。已实现 NextUI 的主机/加入菜单、内容校验、断线、暂停与自建中继。现有 AI 翻译入口保留；联机翻译期间双方暂停，前端继续处理网络。AI 自动玩在联机时禁用。

Mac 的真实双核心测试、tg5040 ARM64 交叉编译、实际 ARM64 核心双端串口测试均通过。Mac 回归包含暂停、分片/延迟中继、ROM/模式/核心协议/核心二进制不匹配、突然断线、心跳超时及异常帧；另拒绝 RFU 截断广播、过长客户端数据与非法玩家编号，防止核心接收路径越界。另在一次性容器网络中使用 Linux netem 测试 25ms 延迟和 2% 丢包，确认实际出现丢包后双端仍交换了正确的 GBA 串口数据。实际 ma_saves.c 持久化测试另验证联机禁读写、旧自动恢复隔离、写入失败保留保护及正常单人保存后解除保护。详细数值在更新包 `validation/`。

测试 ROM 是本项目原创 ARM homebrew，它直接操作 GBA SIO 寄存器；不是前端伪造网络数据。附带原创 Link Duel 测试 ROM：A 攻击，红条是自己血量、蓝条是对手血量、绿色标记表示收到对端串口数据。它既验证双向串口，也验证真实 GBA 按键能经串口改变对端血量；不代表商业游戏已完成对战。电脑已有的《马里奥网球 力量之旅》BTME 已用两个独立 Mac gpSP 客户端进入真实无线 Exhibition：双方各自选角色、P2 移动、P1 发球得分并赢下一局、随后 P2 发球得分；两端比分一致。整场比赛结束尚未测到，商业 RFU 在 ARM64/Brick 上也未验收。《高级战争》汉化版仅完成菜单流程。结果与输入记录在更新包 `validation/`，复现步骤见随包 `GAME-VALIDATION.md`。测试游戏的红/蓝条分别表示本机/对手血量，A 每按一次攻击一次；绿色标记表示收到对端数据。测试 ROM 可从包内 `tests/link-probe.gba` 手动拷贝到 `Roms/GBA/`，两端核心 `Link Cable Connectivity` 选择 `Link Cable - Pokemon Gen3 mode`（内部值 mul_poke）再开联机。商业游戏 ROM、Nintendo BIOS 和用户存档不会进入更新包。

2026-10-07 已在两台独立 Brick 上通过直接局域网 Link Duel 串口对战、交换主客位置及双方暂停恢复测试。《马里奥网球》在 NextUI 里完成 RFU 配对和选角，但进入球场开始场景后卡住，尚未确认得分或完整比赛。最新性能改动及证据见 [HARDWARE-VALIDATION.md](HARDWARE-VALIDATION.md)。真实公网、功耗、Wi-Fi 干扰及完整游戏兼容仍需验收。

2026-10-07 后续复测：用户反馈前端优化后稍有改善，但游戏红色连接标志仍反复出现。该次两台日志显示约 60.2–60.3 FPS，RFU 持续收发、TCP 未报告断开；因此不能把前端帧率当作游戏同步已通过的证据。Mac 已复现原核心 8 包突发时 ACK 8 包却只交付 4 包的问题，并新增校验固定的 RFU 队列补丁：双向 32 包按序交付，容量超限和等待超时都有统计。该补丁的商业抖动测试和新核心掌机验收仍需完成。本地环境见 [LOCAL-TESTING.md](LOCAL-TESTING.md)。

## 蓝牙是否更合适

近距离两台 Brick，蓝牙作为不依赖路由器的连接方式有价值。已有项目真机记录显示 BlueZ 5.78、HCI 4.1、central/peripheral 角色，并已有 Brick Mic 的 BLE GATT 链路，这属于硬件/系统可行性证据，不是 Brick 到 Brick 对战证据。

蓝牙仍需实现可靠、有序、分帧的双向传输，并验证连接间隔、MTU、拥塞、暂停与断线。经典蓝牙 RFCOMM 可考虑复用现有字节流协议，但必须先核实 Brick 的 BR/EDR/Profile/内核支持；BLE GATT 则需要明确的分片和流控，不能直接把 TCP socket 改成通知就认为完成。Mac 上两个软件进程无法模拟两颗实际蓝牙控制器，因此相关无线验收需两台机器。

建议先用已经有实际核心证据的 Wi-Fi 完成流程，再沿用核心回调、握手和存档护栏增加蓝牙传输。蓝牙用于身边对战；公网继续使用 Wi-Fi/IP。当前包尚未实现蓝牙。

官方 API： [BlueZ ProfileManager](https://github.com/bluez/bluez/blob/master/doc/org.bluez.ProfileManager.rst)、[Adapter](https://github.com/bluez/bluez/blob/master/doc/org.bluez.Adapter.rst)。本机硬件记录见项目 `docs/BRICK-MIC-PLAN.md`。

## 开源路线比较

| 路线 | 原生 GBA 联机范围 | ARM64 / 移植判断 | 许可证与公网情况 |
|---|---|---|---|
| gpSP + minarch | Wireless Adapter；另有 Pokemon Gen3、AW1、AW2 专用串口协议，不是通用联机线 | 已构建 ARM64 dynarec；前端改动可控，采用此路线 | gpSP GPL-2.0-or-later；本项目 NextUI GPLv3；网络前端自行实现 |
| RetroArch + gpSP | 同一 gpSP 网络接口，不自动扩展到所有 GBA | 功能多，但需替换/新增前端并重新集成菜单、翻译 | RetroArch GPLv3；有直连/中继，但与当前自定义协议不兼容 |
| mGBA | 同机多个实例的联机线；网络联机线仍列为计划 | 可以作桌面参考，不能将同机联机当作 Brick 网络实现 | MPL-2.0；不是现成的网络联机线替换核心 |
| VBA-M | TCP 联机线/RFU 实现存在 | 需适配 SFML/网络/UI 与当前核心，集成成本高 | GPL；可作为协议参考与后备路线 |
| Tango | 针对特定洛克人 EXE 游戏的回滚联网 | 非通用 GBA 系统；需要每游戏适配、补丁及独立前端 | GPLv3；不适合作为本轮通用 gpSP 替换 |

证据固定于本轮获取的官方源代码/文档：
- [gpSP 固定提交](https://github.com/libretro/gpsp/tree/5819380c2ffb0900219d700a382ee68c464ebb99)；[串口实现](https://github.com/libretro/gpsp/blob/5819380c2ffb0900219d700a382ee68c464ebb99/serial_proto.c)；[游戏 override 表](https://github.com/libretro/gpsp/blob/5819380c2ffb0900219d700a382ee68c464ebb99/gba_over.h)；[核心选项](https://github.com/libretro/gpsp/blob/5819380c2ffb0900219d700a382ee68c464ebb99/libretro/libretro_core_options.h)。gpSP README 的通用 link TODO 不应覆盖源码里已经实现的专用串口模式。
- [mGBA README](https://github.com/mgba-emu/mgba/blob/master/README.md)。
- [VBA-M gbaLink.cpp](https://github.com/visualboyadvance-m/visualboyadvance-m/blob/master/src/core/gba/gbaLink.cpp)。
- [Tango 官方仓库](https://github.com/tangobattle/tango)。
- [RetroArch Netplay FAQ](https://docs.libretro.com/guides/netplay-faq/)。文档的 GBA 概括与 gpSP 的新增能力可能不同，按具体核心源码判断。

## 游戏范围

完整候选表见包内 `compatibility.md` 或源码 `ports/gba-link/compatibility.md`，59 个 ROM ID 来自固定 gpSP 的专用串口/RFU flags。前端拒绝不在候选表中的 ROM；ID 在表里仅表示源码路线存在，不保证汉化/改版兼容。

| 游戏 | 选择方式 | 本轮证据 |
|---|---|---|
| 原创 NLNK 串口测试 ROM | `gpsp_serial=mul_poke` | Mac/ARM64 双端真实 SIO 数据通过 |
| Advance Wars 1：AWRE / AWRP | auto / mul_aw1 | 源码有专用协议；已有 AWRE 汉化 ROM 游戏流程验证，完整对战状态见记录 |
| Advance Wars 2：AW2E / AW2P | auto / mul_aw2 | 源码支持；尚未验证此 ID 的实际游戏 |
| Pokemon Ruby/Sapphire | auto / mul_poke | 源码支持；未验证商业交换/对战 |
| Pokemon Emerald / FireRed / LeafGreen | 按 ROM 的 auto override；Wireless Adapter 玩法通常为 RFU | 源码支持；不能把有线柜台的所有玩法当作 RFU 支持 |
| Mario Tennis Power Tour：BTME | auto / rfu | Mac 真实双端配对、角色选择、双方操作及计分通过；整场结束/真机待验收 |
| 其他表内 RFU 游戏 | auto / rfu | 源码候选，逐款玩法验证待执行 |

NAS 上名为“高级战争2”的文件实际 header 是 BGWJ（日版 1+2 合集），不是 AW2E/AW2P；不能只按中文文件名当作 AW2 支持证据。两端必须使用逐字节相同的 ROM，包含补丁版本。

## 无 SSH 安装：Mac + SD 卡

先正常关机，将 SD 卡插入 Mac。解压本包；勿将整个研发工作目录或私有测试 ROM 拷贝到卡上。安装器只替换以下两项：

```
.system/tg5040/bin/minarch.elf
.system/tg5040/cores/gpsp_libretro.so
```

它要求卡上已有对应 NextUI 文件，不是完整固件刷机工具。minarch 是其他模拟器也共用的前端，因此需要验收一次其他平台单人游戏。此包基于当前项目代码，保留 AI 翻译等现有功能；如果掌机的 NextUI 版本与本项目不一致，先检查版本差异。

在解压目录运行，替换实际卡名，路径加引号：

```sh
python3 install.py "/Volumes/你的SD卡" install
```

安装器先验证包内 SHA256，再备份现有两个文件，校验备份，创建事务记录，最后逐项原子替换；异常时回滚。它拒绝目标路径中的符号链接；保留 `.gba-link-backups/`，不要只拷贝裸 payload 覆盖。安装成功后会输出明确的备份目录名。ROM、Saves、Bios、用户配置、API Key 和系统权限均不修改。

恢复命令使用安装时打印的目录名：

```sh
python3 install.py "/Volumes/你的SD卡" rollback --backup "实际备份目录名"
```

断电/拔卡导致中断后，重新接卡并按日志指定的备份名执行 rollback，再重新安装。不要覆盖或删除原备份。两台 Brick 安装同一个包，安全弹出卡后再开机。

## 在 NextUI 中联机

1. 两台开启 Wi-Fi 并连接同一局域网。启动相同 GBA 游戏，用 gpSP；核心 `Link Cable Connectivity` 初次建议 auto。
2. 第一台打开游戏菜单 → Options → GBA Link → Host Link。菜单显示本机 IP 和固定 TCP 55435 端口。
3. 第二台在 Host / Relay Address 里用十字键设置主机 IPv4，然后 Join Link。连接后双方继续在游戏自身的联机菜单操作。前端连上不等于游戏配对完成。
4. 菜单或翻译会暂停双方；退出菜单恢复。联机中禁止快进、倒带、AI 自动玩、核心重置与修改核心选项。
5. 一旦该次游戏运行启动过联机，直到退出并重新打开游戏都禁止即时存档/读档及自动恢复写入。联机状态不能完整序列化；之前的 resume 文件保留，但增加持久标记阻止下一次自动加载旧 resume，避免旧 RAM 覆盖联机后的本地进度；后续正常单人自动存档成功才清除此标记。手动读旧即时存档仍按用户选择执行。断线后请退出并重新打开游戏，再开始新会话。
6. 正常游戏 SRAM 仍保存各自本地进度。每次开始连接前，在 `Saves/GBA/` 生成 `.gba-link-<ROM SHA前8位>-before-<时间>-<随机值>` 原始 SRAM 备份，不同步或覆盖对方存档。恢复 SRAM 需退出游戏后进行；若原配置使用压缩 SRM，先转换原始备份为相应格式，不能直接用原始字节冒充压缩 SRM。
7. 睡眠/退出会结束联机；网络超时为约 10 秒，等待中继另一端约 120 秒。掉线后可继续退出本地游戏，不自动重连或恢复不完整会话。

## 公网路径

已提供自建 TCP 中继原型，两端都向服务器主动连接。它解决连接拓扑问题，不是自动 NAT 穿透；没有部署或真实互联网对战记录，也不能接入 RetroArch 的公共中继。

在自己控制的可达主机运行：

```sh
python3 ports/gba-link/server/relay.py --bind 0.0.0.0 --port 55435
```

两台选择 Connection = Public Relay，设置相同服务器 IPv4、六位 Room Code，一台 Host Link、一台 Join Link。服务器需允许 TCP 55435 入站；掌机只需出站。房间号用于配对，不是认证；此实验协议未增加 TLS、账号或访问控制。默认服务器只监听回环地址。当前适用于受控试验；公开服务的认证、加密与运行治理尚未实现。

## 复现构建与验证

固定提交、源代码 SHA256、SDK 镜像 digest 和依赖提交在 `versions.json`。用 Homebrew 的 OpenSSL 3、可选 SDL2、Apple clang 与 Python3；Docker/Colima 用于 Brick 交叉编译及 ARM64 测试。

```sh
python3 ports/gba-link/build.py mac
python3 ports/gba-link/tests/run-tests.py
python3 ports/gba-link/build.py brick
python3 ports/gba-link/tests/run-arm.py
python3 ports/gba-link/tests/run-save-guard.py
python3 ports/gba-link/tests/run-arm.py --netem
python3 ports/gba-link/package.py
```

netem 仅修改一次性 Docker 容器的网络命名空间，需要 NET_ADMIN；脚本在该容器内安装 iproute2 测试工具，不修改 Mac 网络。Mac gpSP 用解释器，Brick core 用 ARM64 dynarec；ARM64 测试使用目标 SDK 的 glibc 2.28 loader，避免宿主 libc 混用。包生成要求源码工作区干净、已提交；包含 GPL 源码、依赖版本、许可证、校验值和验证日志。

可选 SDL 桌面测试窗口：`NP_WINDOW=1 NP_GAME=1 NP_CAPTURE=/tmp/host.bmp build/gba-link/core-runner-window CORE ROM host 127.0.0.1 55435 auto 3600`，另一进程用 join。X=A、Z=B、Return=Start，方向键移动，F12截图。无窗口调试可用 `NP_CONTROL` 指定文本文件，每行写 `命令序号 按键位图 持续帧数`；`NP_INPUT_LOG` 记录实际执行帧，便于重放。`NP_CAPTURE_EVERY` 配合 `NP_LIVE_CAPTURE=1` 原子更新当前截图。商业 ROM 的 `NP_GAME` 返回成功仅表示运行了画面，不能据此报告对战通过。`NP_FAST` 仅用于菜单定位；玩法验收须使用正常帧速。

## 掌机 ready 后验收

- 两卡安装、开机、GBA 单人启动，确认 AI 翻译/原存档/设置；检查其他核心单人运行。
- 两端相同 ROM/核心，主机加入，完成游戏内配对；至少完成一局/交换，并确认各自存档。
- 打开菜单、长时间翻译，另一端暂停；双方继续；禁用功能有提示。
- 断 Wi-Fi、对端退出、睡眠，确认有界断线；退出重开可以重新配对。
- 连续 15 分钟游戏，记录 fps、音频、无线稳定性、CPU/温度和按键/显示。
- 经外部网络实际中继试验；蓝牙另行用两颗控制器验证连接、延迟、流控与断线。
- 通过 SD 卡 rollback，确认两项文件哈希恢复，ROM/存档/Key 保留。
