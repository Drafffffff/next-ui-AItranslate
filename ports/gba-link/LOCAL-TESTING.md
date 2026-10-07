# Mac 上的真实 gpSP 双机环境

这套环境运行两个独立模拟器进程，使用与 Brick 相同的固定 gpSP 源码、RFU 补丁和 `ma_netplay.c`。Mac 用解释器，Brick 用 ARM64 dynarec；它可以复现数据顺序、队列溢出与游戏流程，不能替代真实无线硬件和掌机性能验收。

## 构建与回归

```sh
python3 ports/gba-link/build.py mac
python3 ports/gba-link/tests/run-rfu-queue.py
python3 ports/gba-link/tests/run-tests.py
```

需要 Python、Apple clang、Homebrew OpenSSL 与 SDL2。第一条命令校验上游源码压缩包和本地 RFU 补丁的 SHA256；仅接受已知原始源码或完整补丁后的源码，拒绝其他缓存修改。RFU 测试使用真实 `rfu.c`，以 ASan / UBSan 检查双向突发、顺序、排空、容量边界和重置；外部 CPU 寄存器与网络回调为测试桩。

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
