# NextUI GBA Link 实验更新

目标平台：TrimUI Brick / tg5040。研发分支：`feature/gba-bluetooth`（沿用 `feature/gba-netplay` 的 Wi-Fi 与 RFU 修复）。当前交付包含局域网 Wi-Fi、实验性蓝牙与独立热点两人原生 GBA 联机，不是共享按键或两人控制同一个模拟器。每端运行自己的游戏、自己的存档；仅交换 gpSP 的模拟串口 / Wireless Adapter 数据。

2026-10-07：蓝牙版本 `da609b38` 已编译、打包并安装到两台 Brick，安装文件与发布校验一致。真实 ARM gpSP 核心已通过自动搜索加入、双向串口按键计分、交换主客机、重复连接、ROM／核心／模式不一致拒绝；实际主机等待取消也已验证。任天堂商业游戏的蓝牙菜单、选角、比赛流畅度与花屏情况仍待用户验收，不能据此声称蓝牙对战已经完成。安装与验收状态见 `bluetooth-deployment.json`、`bluetooth-validation.json`。热点模式是后续工作，当前尚未提供。

## 当前结论

优先采用现有 minarch + 固定新版 gpSP，接入 libretro `RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE`。不需要把系统换成 RetroArch。已实现 NextUI 的主机/加入菜单、内容校验、断线、暂停与自建中继。现有 AI 翻译入口保留；联机翻译期间双方暂停，前端继续处理网络。AI 自动玩在联机时禁用。

Mac 的真实双核心测试、tg5040 ARM64 交叉编译、实际 ARM64 核心双端串口测试均通过。Mac 回归包含暂停、分片/延迟中继、ROM/模式/核心协议/核心二进制不匹配、突然断线、心跳超时及异常帧；另拒绝 RFU 截断广播、过长客户端数据与非法玩家编号，防止核心接收路径越界。另在一次性容器网络中使用 Linux netem 测试 25ms 延迟和 2% 丢包，确认实际出现丢包后双端仍交换了正确的 GBA 串口数据。实际 ma_saves.c 持久化测试另验证联机禁读写、旧自动恢复隔离、写入失败保留保护及正常单人保存后解除保护。详细数值在更新包 `validation/`。

测试 ROM 是本项目原创 ARM homebrew，它直接操作 GBA SIO 寄存器；不是前端伪造网络数据。附带原创 Link Duel 测试 ROM：A 攻击，红条是自己血量、蓝条是对手血量、绿色标记表示收到对端串口数据。它既验证双向串口，也验证真实 GBA 按键能经串口改变对端血量；不代表商业游戏已完成对战。电脑已有的《马里奥网球 力量之旅》BTME 已用两个独立 Mac gpSP 客户端进入真实无线 Exhibition：双方各自选角色、P2 移动、P1 发球得分并赢下一局、随后 P2 发球得分；两端比分一致。整场比赛结束尚未测到，商业 RFU 在 ARM64/Brick 上也未验收。《高级战争》汉化版仅完成菜单流程。结果与输入记录在更新包 `validation/`，复现步骤见随包 `GAME-VALIDATION.md`。测试游戏的红/蓝条分别表示本机/对手血量，A 每按一次攻击一次；绿色标记表示收到对端数据。测试 ROM 可从包内 `tests/link-probe.gba` 手动拷贝到 `Roms/GBA/`，两端核心 `Link Cable Connectivity` 选择 `Link Cable - Pokemon Gen3 mode`（内部值 mul_poke）再开联机。商业游戏 ROM、Nintendo BIOS 和用户存档不会进入更新包。

2026-10-07 已在两台独立 Brick 上通过直接局域网 Link Duel 串口对战、交换主客位置及双方暂停恢复测试。《马里奥网球》在 NextUI 里完成 RFU 配对和选角，但进入球场开始场景后卡住，尚未确认得分或完整比赛。最新性能改动及证据见 [HARDWARE-VALIDATION.md](HARDWARE-VALIDATION.md)。真实公网、功耗、Wi-Fi 干扰及完整游戏兼容仍需验收。

RFU 交换模型正在按实机记录修复：客机暂存、主机驱动、单包接收与独立确认；底层检查与既有回归通过。历史 32 包实验不能作为当前版本商业游戏验收。当前验证见 [RFU-EXCHANGE-FIX.md](RFU-EXCHANGE-FIX.md)，本地环境见 [LOCAL-TESTING.md](LOCAL-TESTING.md)。

## 蓝牙连接与限制

蓝牙模式不需要路由器、MAC 地址或配对码。进入游戏菜单 → Options → GBA Link → Mode → Bluetooth：第一台选择 Create Room，第二台选择 Join Room。一个附近房间会自动加入；多个房间才显示选择列表。主机等待界面与房间列表使用相同的短名称（例如 GBA Link 9321），方便区分设备。搜索、连接和内容校验自动进行，等待时按 B 取消；只有双向通道和 ROM／核心／串口模式校验完成后才显示已连接。随后仍需在游戏自身的联机入口配对。

当前使用 BLE GATT、协商 MTU、可靠有序分片与有界双向队列，辅助进程通过私有文件描述符向现有联机协议转发数据。搜索和无线收发不在游戏或菜单线程执行；取消只清理本次辅助进程及其连接。选房后直接连接该房间，避免再次搜索时重复过滤导致等待。核心与存档护栏沿用 Wi-Fi 版本。

蓝牙仍是实验模式。此前同一对 Brick 的 104 字节连续交换中，Wi-Fi 中位往返约 4.3ms、经典蓝牙约 33.8ms、优化后 BLE 原始 ATT 约 30ms；BLE 的十分钟有界队列测试通过，但仍测到长延迟尖峰。这些传输测试不是游戏帧率或商业对战验收。游戏内的红色无线标志也不能直接作为底层丢包计数，应结合延迟、队列和实际断连日志判断。保持 Wi-Fi 可选，不承诺蓝牙已达到 Wi-Fi 的流畅度。

两台需开启系统蓝牙。若更新时保留离线游戏运行，磁盘文件会原子替换，已运行的游戏仍使用旧版前端；正常退出并重新打开后才加载新菜单。掉线或结束已使用的联机会话后也需正常重新打开游戏，不能恢复未完整序列化的联机状态。公网仍走 Wi-Fi 中继。热点模式不依赖路由器，使用专用 Wi-Fi 会话，退出后恢复原有 Wi-Fi 配置。

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

先正常关机，将 SD 卡插入 Mac。解压本包；勿将整个研发工作目录或私有测试 ROM 拷贝到卡上。安装器只更新以下三项：

```
.system/tg5040/bin/minarch.elf
.system/tg5040/cores/gpsp_libretro.so
.system/tg5040/bin/nextui-gba-bluetooth.elf
```

它要求卡上已有 minarch 与 gpSP；蓝牙辅助程序可以首次新增。此安装器不是完整固件刷机工具。minarch 是其他模拟器也共用的前端，因此需要验收一次其他平台单人游戏。此包基于当前项目代码，保留 AI 翻译等现有功能；如果掌机的 NextUI 版本与本项目不一致，先检查版本差异。

在解压目录运行，替换实际卡名，路径加引号：

```sh
python3 install.py "/Volumes/你的SD卡" install
```

安装器先验证包内 SHA256，再备份现有文件（记录蓝牙程序原先是否存在），校验备份，创建事务记录，最后逐项原子替换；异常时回滚；首次新增的蓝牙程序在回滚时删除，已有蓝牙程序则恢复原文件。它拒绝目标路径中的符号链接；保留 `.gba-link-backups/`，不要只拷贝裸 payload 覆盖。安装成功后会输出明确的备份目录名。ROM、Saves、Bios、用户配置、API Key 和系统权限均不修改。

恢复命令使用安装时打印的目录名：

```sh
python3 install.py "/Volumes/你的SD卡" rollback --backup "实际备份目录名"
```

断电/拔卡导致中断后，重新接卡并按日志指定的备份名执行 rollback，再重新安装。不要覆盖或删除原备份。两台 Brick 安装同一个包，安全弹出卡后再开机。

本次 SSH 更新的实际备份目录如下，仍由同一安装器识别：

- 第一台（192.168.50.70）：`20261007-230908-da609b38`
- 第二台（192.168.50.91）：`20261007-230932-da609b38`

需回滚时先正常退出游戏并关机，将对应卡接入 Mac，再用该卡的目录名执行上述命令。两台备份名不同，不能交换使用。当前安装时第二台保留离线游戏运行以防休眠；更新采用原子替换，实测原进程 PID 和旧可执行映像保持不变。重新打开游戏才加载新版。

## 在 NextUI 中联机

1. 两台开启 Wi-Fi 并连接同一局域网。启动相同 GBA 游戏，用 gpSP；核心 `Link Cable Connectivity` 初次建议 auto。
2. 第一台打开游戏菜单 → Options → GBA Link，在顶部选择 Wi-Fi，再选择 Create Room。创建后保留等待界面，持续显示本机 IP 和固定 TCP 55435 端口，直到另一台完成连接；按 B 可取消。
3. 第二台同样选择 Wi-Fi，在 Advanced → Host / Relay Address 里设置主机 IPv4，然后返回选择 Join Room。连接后双方继续在游戏自身的联机菜单操作。前端连上不等于游戏配对完成。
4. 菜单或翻译会暂停双方；退出菜单恢复。联机中禁止快进、倒带、AI 自动玩、核心重置与修改核心选项。
5. 一旦该次游戏运行启动过联机，直到退出并重新打开游戏都禁止即时存档/读档及自动恢复写入。联机状态不能完整序列化；之前的 resume 文件保留，但增加持久标记阻止下一次自动加载旧 resume，避免旧 RAM 覆盖联机后的本地进度；后续正常单人自动存档成功才清除此标记。手动读旧即时存档仍按用户选择执行。断线后请退出并重新打开游戏，再开始新会话。
6. 正常游戏 SRAM 仍保存各自本地进度。每次开始连接前，在 `Saves/GBA/` 生成 `.gba-link-<ROM SHA前8位>-before-<时间>-<随机值>` 原始 SRAM 备份，不同步或覆盖对方存档。恢复 SRAM 需退出游戏后进行；若原配置使用压缩 SRM，先转换原始备份为相应格式，不能直接用原始字节冒充压缩 SRM。
7. 睡眠/退出会结束联机；网络超时为约 10 秒，等待中继另一端约 120 秒。掉线后可继续退出本地游戏，不自动重连或恢复不完整会话。

## 三种连接模式

GBA Link 顶部并列显示 Bluetooth / Wi-Fi / Hotspot；方向键左右或 L/R 切换，A 选择 Create Room / Join Room，B 返回或取消等待。只有 Wi-Fi 显示 Advanced 地址与中继设置。

热点：两台系统 Wi-Fi 保持开启，无需同一局域网。主机选择 Hotspot → Create Room，客机选择 Hotspot → Join Room；仅一个房间时自动选择，多个房间时显示列表。房间名称为 `NextUI-GBA-` 加 AP 地址后四位。WPA2 密码固定为 `NextUI-GBA-Link`，由程序自动填写，界面不要求输入密码或 IP。使用标准 Wi-Fi，非开放热点。

热点会专用无线电进行双机传输，期间原路由器连接暂停。AP/STA 并发实测存在明显丢包，因此当前方案采用独立热点。退出、取消或超时后删除本次临时网络及地址，恢复原有网络选择、启用状态和省电设置；不写入保存的 Wi-Fi 配置、不重启系统 supplicant、不停止其他蓝牙连接。首次连接需等待无线握手和游戏身份检查，等待不表示已经配对成功。蓝牙及热点房间自动搜索不能替代游戏自身的联机入口。

## 公网路径

已提供自建 TCP 中继原型，两端都向服务器主动连接。它解决连接拓扑问题，不是自动 NAT 穿透；没有部署或真实互联网对战记录，也不能接入 RetroArch 的公共中继。

在自己控制的可达主机运行：

```sh
python3 ports/gba-link/server/relay.py --bind 0.0.0.0 --port 55435
```

两台先在顶部选择 Wi-Fi，再进入 Advanced，设置 Connection = Public Relay、相同服务器 IPv4 和六位 Room Code；返回后一台 Create Room、一台 Join Room。服务器需允许 TCP 55435 入站；掌机只需出站。房间号用于配对，不是认证；此实验协议未增加 TLS、账号或访问控制。默认服务器只监听回环地址。当前适用于受控试验；公开服务的认证、加密与运行治理尚未实现。

## 复现构建与验证

固定提交、源代码 SHA256、SDK 镜像 digest 和依赖提交在 `versions.json`。用 Go（蓝牙依赖固定于 go.mod／go.sum）、Homebrew 的 OpenSSL 3、可选 SDL2、Apple clang 与 Python3；Docker/Colima 用于 Brick 交叉编译及 ARM64 测试。

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
