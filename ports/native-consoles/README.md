# 原生创作工具

TrimUI Brick + NextUI 的 Tools 菜单入口：PICO-8、TIC-80。
PICO-8 默认进入 Splore，TIC-80 默认进入 Surf 图形游戏浏览器，可用方向键和 A/B 浏览、下载和运行游戏。现有 Emus 游戏启动器继续保留。

- PICO-8 使用用户自行购买的 Raspberry Pi 64 位运行文件，优先读取 `Emus/tg5040/PICO.pak/pico8/`，其次读取 `Bios/PICO/`。本仓库不提供商业运行文件。
- Splore 使用已安装 PICO 游戏包中的 GNU Wget 下载 HTTPS 内容；启动器会设置专用 PATH 和 `use_wget 1`。根证书来自 [Mozilla / curl CA bundle](https://curl.se/docs/caextract.html)，存放在 `certs/cacert.pem`，保持 HTTPS 证书验证开启。
- 在 Splore 收藏游戏后，退出 PICO-8 会将已下载的收藏复制到 `Roms/Pico-8 (PICO)/Favourites/`，并添加卡带预览图。只同步收藏，跳过未完成的下载；重复退出不会重复复制或覆盖已有 ROM。取消收藏不会删除已经复制的游戏。同步日志在 `.userdata/shared/pico8-studio/favourites-sync.log`。
- 游戏目录里的 `Splore.p8` 通过 `pico8-emu-launch.sh` 调用同一个 Tools 启动器，共享联网配置和收藏同步。安装桥接脚本到 `Emus/tg5040/PICO.pak/launch.sh` 前，将原启动器保存为 `launch-vendor.sh`；普通游戏继续由原启动器运行。重新安装社区 PICO 游戏包后需重新安装桥接脚本。
- TIC-80 使用 `../tic80-brick/build.sh` 编译的原生版本，包含编辑器、Lua 和在线 Surf。联网使用 Brick 固件的 libcurl，通过同一份根证书验证 HTTPS；Surf 的 `tic80.com` 目录是在线游戏库。
- MENU 返回 NextUI，退出前请在编辑器内保存作品。代码输入、精细编辑建议接 USB 键盘和鼠标。
- PICO-8 作品目录：`Roms/Pico-8 (PICO)/`；配置、日志：`.userdata/shared/pico8-studio/`。
- TIC-80 作品、配置、日志：`.userdata/shared/tic80-studio/`。

`menu-exit.c` 只监听 Brick 的 MENU 按键，通过兼容库向自己启动的程序发送 SDL_QUIT，让程序正常清理显示和音频资源。无响应时才升级为进程终止，不终止其他应用。两款工具均使用这个退出处理。
`sdl-nosensor.c` 保留机器原有 SDL，只移除 PICO-8 初始化时请求的未编译传感器子系统，并负责在应用事件循环里交付正常关闭请求。
运行 `bash ports/native-consoles/build.sh` 生成两个安装包；PICO 包不包含付费运行文件。
TIC 启动脚本、原生执行文件及其许可证放入 `Tools/tg5040/TIC-80.pak/`。

运行 `bash ports/native-consoles/verify.sh` 可检查 TIC-80 的 SDL 正常退出；可选参数是本地合法 PICO-8 Raspberry Pi 文件夹，增加 PICO-8 检查。这项检查使用桌面 SDL，不替代 Brick 的显示和实体按键实测。
