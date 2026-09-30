# TIC-80 Lab / Brick 物质沙盒

将开源 [TIC-80](https://github.com/nesbox/TIC-80) SDL/Lua 引擎移植到 NextUI / TrimUI Brick，启动自制的可交互沙、水、石头、火焰沙盒。与 PocketJS 独立，用于比较成熟原生引擎的交互和性能。TIC-80 固定提交 `4dba5bc2640d9cde650fb0b427c9be6aab598de9`，MIT 许可。

- 方向键移动笔刷；A 绘制、B 擦除；X 切换物质、Y 切换笔刷大小。
- Select 暂停模拟，Start 重置；MENU 退出回到 NextUI。
- 模拟区域 120×62，每秒更新 30 次，画面按 TIC-80 的 240×136 输出并等比放大。当前沙盒不保存作品。
- 内置编辑器保留，但尚未为无键盘的 Brick 适配完整编辑操作。此包用于沙盒和 Lua 游戏试验。

先准备 PocketJS 的 tg5040 固定工具链及 Docker；运行 `./ports/tic80-brick/build.sh`。产物为 `build/creative-lab/TIC-80 Lab.pak/`，复制到卡的 `Tools/tg5040/`。启动脚本将配置与日志隔离到 `.userdata/shared/tic80-brick/`。

仅启用 Lua、SDL 和文本工程读取（TIC-80 画面由 CPU 生成，Brick 优先使用 Mali SDL 呈现，失败则尝试软件呈现）；没有接入在线商店/浏览功能或 GPU 3D/CRT 着色器。上游补丁为 `brick-input.patch`：TRIMUI 原始手柄映射、MENU 退出、SDL 呈现回退及有限帧截图测试入口。打包附带上游与参与编译的依赖许可说明。

自动测试以真实 ARM64 程序和桌面 SDL dummy 驱动运行沙盒；该结果不等于 Brick 的 Mali 显示及实际手柄操作已验收。
