# 商业游戏玩法验证

2026-10-04 在 Mac 上运行两个独立 gpSP 核心，使用与 NextUI 相同的 `ma_netplay.c`。网络是本机回环 TCP，不是真实 LAN、公网或两台 Brick。核心固定于 `versions.json`，Mac 为解释器构建。每端使用独立普通 SRAM 文件；未修改用户原存档，不加载前端即时存档。

## Mario Tennis Power Tour

ROM header：BTME。SHA256：`bffa41f7ca1bd0c26920c752fc08b01f754a89f6411b16e17fe375bacc595082`。文件虽名为中文游戏，实际测试 ROM 是英文版。两端必须相同文件。

首次运行需要初始化 Power Tour 存档，并经过角色/难度/名字选择与开场剧情。已在两份测试存档里走完；自由移动时按 Start → Suspend → Save，游戏保存并返回 Main Menu。这里的 Suspend 是游戏自己的保存功能，与 NextUI 的即时存档不同。测试存档与 ROM 不放入更新包。

在 Main Menu 选 Link（两台 GBA 图标），两端等待无线配对。前端先完成 Host/Join，核心 serial 设置 auto（BTME 自动选 RFU），两台设备出现在游戏联机画面。按 A 继续；本轮分别选择 P1 Mario、P2 Peach，Hard 球场，Power Shot On / 1 Set / 2 Games，Start。

实际观察：

- 两端独立选择角色，另一端显示相同角色。
- 比赛初始 0–0；仅 P1 输入 A 抛球、再次 A 击球，两端都显示 Mario 15–0。
- 仅 P2 右移，两个画面都看到 Peach 移到另一侧。
- P1 后续发球得分，赢下一局，两端显示局数 1–0。
- 换 P2 发球，仅 P2 输入 A、随后 A+Right；两端都显示 Peach 得分 15–0，Service Aces 1。
- 正常结束测试客户端，各自 SRAM 写出。未继续测整场结束、长时间稳定性或 ARM64 商业 RFU。

菜单定位只在启动前 2200 帧使用 `NP_FAST_UNTIL=2200`；配对、选角和比赛都按正常帧间隔运行。Mac 解释器可能低于真实 60fps，不能把它当作 Brick 性能基准。数据包计数及实际输入帧在包内 `validation/game-validation.json`、`tennis-wireless-*.log`、`tennis-wireless-*-input.txt`。

## 桌面复查

先 `python3 ports/gba-link/build.py mac`，需要 Homebrew SDL2 才生成窗口测试前端。使用自己的合法 ROM，在两个终端分别执行：

```sh
NP_WINDOW=1 NP_GAME=1 NP_SAVE=/tmp/tennis-host.sav NP_CAPTURE=/tmp/tennis-host.bmp build/gba-link/core-runner-window CORE ROM host 127.0.0.1 55435 auto 60000
NP_WINDOW=1 NP_GAME=1 NP_SAVE=/tmp/tennis-join.sav NP_CAPTURE=/tmp/tennis-join.bmp build/gba-link/core-runner-window CORE ROM join 127.0.0.1 55435 auto 60000
```

CORE 为 `build/gba-link/mac-source/gpsp-<固定提交>/gpsp_libretro.dylib`，ROM 为实际路径（有空格时加引号）。用独立测试 SRAM 路径，避免覆盖原存档。X=A、Z=B、Return=Start、方向键移动；关闭测试窗口正常保存，F12 截图。程序返回 0 仅确认有正常画面，商业联机通过必须另看实际配对、双方输入、游戏计分与数据包记录。

## 其他游戏及待验收

Advance Wars AWRE 汉化 ROM 已进入 Field Training，未完成联机对战。59 个候选 ID 只是固定核心源码声明，不是逐款实测结果。

2026-10-07 已接入两台真机：直接 LAN 的原创 Link Duel 对战与双方暂停恢复通过；BTME 在 NextUI 完成配对和选角，进入球场开始场景后卡住，用户反馈两端卡顿。尚未验证实际得分或整场比赛。随后新增空收包退避和日志，并安排用户手动复测；见 [HARDWARE-VALIDATION.md](HARDWARE-VALIDATION.md)。真实公网、Bluetooth、商业游戏完整比赛及功耗仍待验收。
