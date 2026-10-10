# PICO-8 退出黑屏诊断与修复

## 真机复现

掌机：tg5040 / TrimUI Brick；从游戏目录启动 fake-08 libretro，前端为 Minarch。
复现游戏：`PICO-8 (PICO)/必玩精选/Alfonzos Bowling Challenge.p8.png`。

原始日志保存在本机 `build/pico8-exit-review/device-reproduced-exit.log`。

| 退出步骤 | 实测耗时 |
| --- | ---: |
| 存档后的 sync | 2.5 ms |
| SDL_WaitThread(ra_probe) | 27,059.5 ms |
| 成就检测线程结束至进程清理结束 | 643.4 ms |

成就连接检测在启动游戏时创建，退出时必须等待其线程结束。
检测所用同步 HTTP 请求设置 30 秒连接超时，线程的中止标记不能打断已经阻塞的请求。
本次退出的主要延迟来自该网络线程，而不是 fake-08 卸载或 SD 卡存档。

## 修改

1. `workspace/all/minarch/minarch.c`：在启动 RetroAchievements 前先检查 `core.tag`；不支持的系统不创建登录、网络探测或离线账本线程。无成就支持的构建保留原来的 stub 调用。
2. `workspace/all/minarch/ra_consoles.h`：把 P8 和 PICO 显式标记为不支持。旧 P8 对应 `RC_CONSOLE_PICO`，但该常量代表 Sega Pico，而不是 PICO-8。

保留游戏正常存档、退出清理和支持系统的成就初始化；不修改全局成就设置或账号。

## 构建及部署验证

以 38f3 工作树的原生代码作为构建基线，以保持已部署的翻译与凭据诊断功能。
修复前重建产物与掌机安装的 ELF 的 SHA-256 完全一致：
`74179304112a59f852be2cf9616032eaacf108032d6e92f75df205aecd1babea`。

修复后的 Minarch 已部署，SHA-256：
`4fbdc8a6d32faf427372c029395182c1ab7fabc78642caf0635ba0cd795e96a8`。

在掌机运行 ARM64 控制台映射检查：P8/PICO/TIC/PRBOOM/空标签不启用成就；GB/GBC/GBA/SFC/PS/PSX 仍支持；检查通过。
统一 P8 启动器 readiness 检查通过。

旧 ELF 备份：
`/mnt/SDCARD/.userdata/shared/pico8-exit-review/minarch-original-20261003.elf`。

## 复测

修复后完整退出耗时待用户再次操作确认。临时 LD_PRELOAD 仅记录 sync/fsync 和线程等待计时，不记录网络请求或凭据；最终复测后恢复原启动器并移除计时库。
