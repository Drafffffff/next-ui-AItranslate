# PocketJS / NextUI / TrimUI Brick：第一阶段

本目录是 **PocketJS 原生 UI 核心 + SDL2 显示验证程序**。仅面向运行 NextUI 的 TrimUI Brick（`tg5040`）。当前没有接入 TypeScript/QuickJS 应用，也不是上游已支持的正式目标。

[阶段计划](../../docs/POCKETJS-BRICK-PLAN.md)。

## 构建

在项目根目录运行：

```bash
./ports/pocketjs-brick/build.sh
```

主机需要 Git、Docker 和网络。脚本固定 PocketJS 提交及 tg5040 镜像摘要，Rust 使用上游 C ABI 固定的 `nightly-2026-07-02`。工具链与依赖保存在 `build/pocketjs-port/`，不会安装到主机的全局 Rust 环境。

产物：

```text
build/pocketjs-port/PocketJS Smoke.pak/
  launch.sh
  pocketjs-smoke.elf
  README.md
  POCKETJS-LICENSE.txt
```

`build/pocketjs-port/build-receipt.txt` 记录输入版本、产物校验值、共享库依赖和 GLIBC 符号要求。

## 自动验证

构建后执行：

```bash
./ports/pocketjs-brick/verify.sh
```

使用真实 PocketJS 静态库和目标 sysroot 的动态加载器运行 ARM64 程序，检查像素尺寸、RGB 顺序、增量更新、按键和重新初始化；目标 SDL 库没有 dummy 驱动，因此窗口、纹理上传和退出使用另一个容器中的 ARM64 桌面 SDL dummy 后端验证；该步骤需要下载桌面 SDL 包。两种容器检查都不代表真机显示已经通过。

## Brick 真机安装与验收

将整个 `PocketJS Smoke.pak` 文件夹复制到 SD 卡：

```text
Tools/tg5040/PocketJS Smoke.pak/
```

开机，在 NextUI 的 Tools 中启动 **PocketJS Smoke**。方向键切换三个色块，A 改变选中色块，B 或 MENU 退出。底部红、绿、蓝、白色条用于检查颜色是否正常。

文字说明由宿主 SDL_ttf 绘制，使用卡上的 `.system/res/font1.ttf`；PocketJS 自身字体与动态中文能力会在后续阶段接入。

日志位于 SD 卡：

```text
.userdata/shared/pocketjs-brick/smoke.log
```

2026-10-01：用户已在 TrimUI Brick 真机确认显示、颜色、按键和退出正常。启动耗时、渲染耗时和内存峰值尚未读取真机日志。

复测项目：

- 屏幕没有旋转、拉伸、黑屏或异常颜色。
- 方向键、A、B、MENU 正常；退出后返回 NextUI，按键不残留。
- 检查日志中的首次显示耗时、渲染最大耗时和内存峰值。

程序最多按 30 Hz 显示，优先验证底层兼容。日志中的容器性能不能作为 Brick 真机性能结论。当前测试未处理休眠、亮度/音量快捷键及热插拔，不能替代日常应用。

## 适配机制

- 复用上游 `engine/ui-cabi`，开启 `bare-platform,software-only`，通过 C ABI 使用真实布局与软件渲染核心。
- 使用 C 分配器及 tg5040 的 C 链接器，避免链接现代 Rust 标准库对新 GLIBC 的要求。
- 用兼容函数补足 LLVM 生成的 C23 数值 min/max 符号，保持 NaN 处理语义。
- Pixel buffer 是 BGRA 字节 / ARGB32 数值；SDL 使用 `SDL_PIXELFORMAT_ARGB8888`。
- 输入映射采用 NextUI tg5040 定义：A=1、B=0、MENU=8，方向键来自 SDL hat。

新增适配文件按本仓库许可证分发；PocketJS 上游为 MIT，构建包附上其许可证。
