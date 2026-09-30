# 原生创作工具

TrimUI Brick + NextUI 的 Tools 菜单入口：PICO-8、TIC-80。
启动本体控制台和编辑器；现有 Emus 游戏启动器继续保留。

- PICO-8 使用用户自行购买的 Raspberry Pi 64 位运行文件，优先读取 `Emus/tg5040/PICO.pak/pico8/`，其次读取 `Bios/PICO/`。本仓库不提供商业运行文件。
- TIC-80 使用 `../tic80-brick/build.sh` 编译的原生版本，包含编辑器和 Lua，不包含在线 Surf。
- MENU 返回 NextUI，退出前请在编辑器内保存作品。代码输入、精细编辑建议接 USB 键盘和鼠标。
- PICO-8 作品目录：`Roms/Pico-8 (PICO)/`；配置、日志：`.userdata/shared/pico8-studio/`。
- TIC-80 作品、配置、日志：`.userdata/shared/tic80-studio/`。

`menu-exit.c` 只监听 Brick 的 MENU 按键，通过兼容库向自己启动的程序发送 SDL_QUIT，让程序正常清理显示和音频资源。无响应时才升级为进程终止，不终止其他应用。两款工具均使用这个退出处理。
`sdl-nosensor.c` 保留机器原有 SDL，只移除 PICO-8 初始化时请求的未编译传感器子系统，并负责在应用事件循环里交付正常关闭请求。
运行 `bash ports/native-consoles/build.sh` 生成两个安装包；PICO 包不包含付费运行文件。
TIC 启动脚本、原生执行文件及其许可证放入 `Tools/tg5040/TIC-80.pak/`。

运行 `bash ports/native-consoles/verify.sh` 可检查 TIC-80 的 SDL 正常退出；可选参数是本地合法 PICO-8 Raspberry Pi 文件夹，增加 PICO-8 检查。这项检查使用桌面 SDL，不替代 Brick 的显示和实体按键实测。
