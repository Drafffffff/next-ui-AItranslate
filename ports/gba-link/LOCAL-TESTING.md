# Mac 上的真实 gpSP 双机环境

这套环境运行两个独立模拟器进程，使用与 Brick 相同的固定 gpSP 源码、RFU 补丁和 `ma_netplay.c`。Mac 用解释器，Brick 用 ARM64 dynarec；它可以复现数据顺序、队列溢出与游戏流程，不能替代真实无线硬件和掌机性能验收。

## 构建与回归

```sh
python3 ports/gba-link/build.py mac
python3 ports/gba-link/tests/run-rfu-queue.py
python3 ports/gba-link/tests/run-tests.py
```

需要 Python、Apple clang、Homebrew OpenSSL 与 SDL2。第一条命令校验上游源码压缩包和本地 RFU 补丁的 SHA256；仅接受已知原始源码或完整补丁后的源码，拒绝其他缓存修改。RFU 测试使用真实 `rfu.c`，以 ASan / UBSan 检查主机驱动交换、单包接收、确认、超时和重置；外部 CPU 寄存器与网络回调为测试桩。

## 一条命令打开双实例

```sh
python3 ports/gba-link/tests/launch-local.py ROM --save-host HOST_SRAM --save-join JOIN_SRAM
```

启动器先复制两份 SRAM 到独立运行目录，再打开两个 gpSP 窗口和仅监听本机的网络代理。不会改传入的存档。目录里保留按键帧号、双方截图、网络日志及 `jitter.txt`；向该文件写 `150` 开启抖动，写 `0` 恢复，关闭任一窗口或 Ctrl-C 会结束测试。`--headless` 可供自动化运行；各端 `.control` 文件接受“递增序号 按键位掩码 持续帧数”，日志记录实际应用的帧号。初始化存档需先按游戏自己的流程保存到 Main Menu。

## 可操作的游戏窗口

自行指定合法 ROM 和两份独立的 SRAM 测试副本。不要直接指向掌机原存档。两个终端分别运行：

```sh
NP_WINDOW=1 NP_GAME=1 NP_REALTIME=1 NP_SAVE=/tmp/tennis-host.sav NP_CAPTURE=/tmp/tennis-host.bmp build/gba-link/core-runner-window CORE ROM host 127.0.0.1 55437 auto 60000
NP_WINDOW=1 NP_GAME=1 NP_REALTIME=1 NP_SAVE=/tmp/tennis-join.sav NP_CAPTURE=/tmp/tennis-join.bmp build/gba-link/core-runner-window CORE ROM join 127.0.0.1 55437 auto 60000
```

`CORE` 是 `build/gba-link/mac-source/gpsp-<versions.json 的提交>/gpsp_libretro.dylib`，`ROM` 是实际路径，有空格时加引号。X=A、Z=B、Return=Start、方向键移动；F12 截图，关闭窗口正常保存测试 SRAM。`NP_REALTIME=1` 将解释器耗时计入帧间隔，避免在运行每帧后额外睡眠 16.7ms；执行超过预算时不追帧。

在游戏 Main Menu 选 Link，再按 A 配对、选择角色和比赛设置。首次新存档的剧情需要初始化；可以在 Power Tour 中通过游戏自己的 Start → Suspend → Save 返回 Main Menu。前端 TCP 连接成功仅证明通道建立，商业联机必须另核对双方画面、独立按键和实际得分。

## 本地网络抖动

先保持主机端口 `55437`，在第三个终端开启仅监听本机的代理：

```sh
python3 ports/gba-link/tests/jitter-proxy.py --port 55438 --target-port 55437 --control /tmp/gba-jitter.txt
```

客机连接代理端口 `55438`。向控制文件写 `0` 为正常转发；写 `150` 会每两秒将每个方向暂缓至多 150ms，再按原顺序放行积压数据。代理保留全部字节，模拟延迟和成批到达，不伪造丢包。退出测试后停止代理。

gpSP 日志的 `GBA Link RFU queue` 显示队列峰值、实际丢弃次数、主机未及时收到回应及客机等待超时次数；前端日志记录收发数量和待发送字节。这些统计用于定位问题，不能把网络包收到了等同于游戏已消费或已进入比赛。

比较其他 Netpacket 核心时，可为测试器设置 `NP_CORE_LOG=link` 接受 Libretro 日志接口，保留联机记录与警告；设为 `1` 记录全部 INFO 以上日志。候选架构、编译与性能限制见 [OPEN-SOURCE-COMPARISON.md](OPEN-SOURCE-COMPARISON.md)。

## 2026-10-07 本轮实测

两个独立 Mac gpSP 解释器采用 `7b0acfd` 的 RFU 补丁和生产网络代码，在 BTME 中配对、分别选择 Mario / Luigi，双方一致显示 Mario 30–0，P2 独立移动在两端可见。代理每两秒暂缓转发至多 150ms 后，两端实际队列峰值均为 9，队列丢弃计数均为 0。停止抖动后双方继续到 Mario 40–0。RFU 主机未及时收到回应的次数仍会增加；队列修复不能消除网络延迟，也不能据此声称所有红标已解决。

完整比赛结束未验证。14 项既有回归、RFU 队列 ASan / UBSan 检查和双实例启动器的正常退出 / 原始 SRAM 保持检查均通过。tg5040 新核心和前端编译完成。随后两台 SSH 恢复，均已安装 `30d685bde7e7139868a589c662e6fe71479819ca`，目标文件 SHA256 和 `APPLIED` 安装日志均通过核对；掌机商业联机复测仍由用户操作，尚未验证。原始输入帧、截图、代理转发统计和 `rfu-local-validation.json` 留在本机 `build/gba-link/`，ROM 与 SRAM 不进更新包。

## RFU 交换模型修复

后续修复已将旧 FIFO 改为实机的单包接收模型，并修正主机驱动和确认等待。`host_replaced/client_replaced` 表示未读取的旧交换被新交换替换，不能按旧 FIFO 的零丢弃标准解释。历史 32 包实验保留作为历史证据，当前实现与验收边界见 [RFU-EXCHANGE-FIX.md](RFU-EXCHANGE-FIX.md)。
