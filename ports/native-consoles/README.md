# 原生创作工具

TrimUI Brick + NextUI 的 Tools 菜单入口：PICO-8、TIC-80。
启动本体控制台和编辑器；现有 Emus 游戏启动器继续保留。

- PICO-8 使用用户自行购买的 Raspberry Pi 64 位运行文件，优先读取 `Emus/tg5040/PICO.pak/pico8/`，其次读取 `Bios/PICO/`。本仓库不提供商业运行文件。
- TIC-80 使用 `../tic80-brick/build.sh` 编译的原生版本，包含编辑器和 Lua，不包含在线 Surf。
- MENU 返回 NextUI，退出前请在编辑器内保存作品。代码输入、精细编辑建议接 USB 键盘和鼠标。
- PICO-8 作品目录：`Roms/Pico-8 (PICO)/`；配置、日志：`.userdata/shared/pico8-studio/`。
- TIC-80 作品、配置、日志：`.userdata/shared/tic80-studio/`。

`menu-exit.c` 只监听 Brick 的 MENU 按键并停止自己启动的子进程，不终止其他应用。编译后与 PICO 启动脚本放入 `Tools/tg5040/PICO-8.pak/`。
TIC 启动脚本、原生执行文件及其许可证放入 `Tools/tg5040/TIC-80.pak/`。
