# PICO-8 统一启动器

游戏工坊与游戏菜单中的普通 PICO-8 卡带优先使用同一个 P8 启动器中的 fake-08；Tools 中的 PICO-8 使用官方运行文件，默认进入 Splore。官方 Tools 启动器也接受绝对 `.p8` / `.p8.png` 路径，以 `-run` 运行卡带，供 fake-08 缺失时回退。官方运行器的 MENU 通过专用 helper 和 SDL 兼容库让当前应用正常退出；两秒没有响应才发送 SIGTERM，再等两秒才使用 SIGKILL。

用户需要自行购买并安装 Raspberry Pi 64 位运行文件。默认读取 `Emus/$PLATFORM/PICO.pak/pico8/`，其次是 `$BIOS_PATH/PICO/`，同时要求 `pico8_64` 可执行且 `pico8.dat` 可读。本仓库及构建产物不包含商业运行文件。`launch.sh --check` 只读检查运行文件、MENU helper、SDL 兼容库和初始配置，不创建目录、不联网、不运行 PICO-8。

启动器遵循 `SDCARD_PATH`、`PLATFORM`、`SYSTEM_PATH`、`BIOS_PATH` 和 `SHARED_USERDATA_PATH`，可选 `PICO8_RUNTIME_PATH` 指定已安装运行文件位置，`PICO8_ROMS_PATH` 指定作品根目录。默认优先使用已经存在的 `Roms/PICO-8 (PICO)`，兼容 `Roms/Pico-8 (PICO)` 和其他 `(PICO)` 目录。

官方运行器的各入口共用 `.userdata/shared/pico8-studio/` 中的配置、控制器映射、Splore 收藏和缓存。首次运行才写入初始配置和控制器映射；启动器保留已有设置以及 `cdata_path`。新配置的官方卡带存档位置为 `.userdata/shared/pico8-studio/cdata/`。旧社区启动器可能使用 `Saves/PICO/`，这里不会自动移动、覆盖或合并这两个位置，也不会将官方存档转换成 fake-08 存档。PICO-8 自身仍会在退出时更新其配置。

只有 Splore 退出才同步下载完整的收藏到作品目录中的 `Favourites/`，并写入 `.media/` 预览及 `map.txt` 游戏名称。现代收藏记录的第6字段（从零计数，带前导 `|` 时为 awk `$7`）提供真实标题；空标题或超出 NextUI 256 字节行缓冲的标题回退到卡带 ID，避免截断中文。已有用户名称映射和非收藏映射均保留。已经同步的卡带也会补齐缺失封面，优先使用自身完整 PNG，必要时使用有效缓存；卡带和已有封面永不覆盖。新增卡带、封面和名称映射先写同一文件系统中的临时文件再改名，失败时清理临时文件。Linux `flock` 防止并发同步，SIGKILL 后内核自动释放锁，旧版本残留的目录锁不再阻塞同步。重复同步不会重复复制，取消收藏不会删除已发布的文件。普通卡带启动不会运行收藏同步或网络探测。Splore 使用社区 PICO 包现有的 GNU wget 下载 HTTPS，SDL 和 wget 的兼容库分别设置。根证书来自 [curl/Mozilla CA bundle](https://curl.se/docs/caextract.html)，保持 TLS 验证开启。

`pico8-emu-launch.sh` 安装为 `Emus/$PLATFORM/PICO.pak/launch.sh`。安装前只保存一次现有社区脚本到 `launch-vendor.sh`；既有 vendor 备份应保留。普通卡带先检查统一 P8 启动器并调用 fake-08 优先的入口。`Splore.p8` / `Splore.p8.png` 则始终调用官方 Tools，无参数也进入 Splore。P8 不可用时，普通卡带回退到官方 Tools；官方运行文件或 helper 也不可用时才调用保存的社区脚本。`--check` 前必须先验证已知启动器标记，避免旧版本启动器把探测当作打开 Splore。桥接脚本不调用自身，也不把桥接脚本误当作 vendor 回退。

原始 PICO 适配源码、模板和收藏同步脚本从同项目工作树 `38f3/ports/native-consoles/` 移植；本目录提供 PICO 专用构建，不依赖 TIC-80。保留原有 `PICO-WRAPPER-LICENSE.txt`。项目自有 helper 与改动遵循仓库 GPL-3.0 许可证。

```sh
bash ports/native-consoles/build-pico8.sh
python3 ports/native-consoles/verify-launchers.py
bash ports/native-consoles/verify-native.sh /path/to/lawful/pico8-runtime
sh ports/native-consoles/verify-favourites.sh ports/native-consoles/sync-pico8-favourites.sh /path/to/complete-cart.p8.png
```

构建产物位于 `build/native-consoles/PICO-8.pak/`。shell fixture 检查参数引用、目录大小写、只读探测、普通卡带/Splore 路由、缺失运行文件、退出码、MENU helper 入口和用户配置保留。可选原生检查将用户合法拥有的运行文件只读挂载到 ARM64 容器，使用 dummy SDL 验证编辑器和卡带正常关闭，不访问用户配置和存档；真实屏幕、音频和实体 MENU 按键需要在 Brick 上验证。
