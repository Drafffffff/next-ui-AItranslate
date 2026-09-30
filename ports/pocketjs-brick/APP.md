# PocketJS App：中文应用验证

适用于运行 NextUI 的 TrimUI Brick。使用 Solid TypeScript + QuickJS 驱动界面，PocketJS 自己绘制中文字体；不依赖 SDL_ttf 绘制标签。

## 构建与安装

```bash
./ports/pocketjs-brick/build-app.sh
./ports/pocketjs-brick/verify-app.sh
```

需要 Git、Docker、unzip，以及 gh 或 curl。固定版本的 Bun 下载到被忽略的构建目录，也可通过 `POCKETJS_BUN` 指定相同版本的已有 Bun。上游依赖按其 lockfile 安装。

将 `build/pocketjs-port/PocketJS App.pak/` 整个目录复制到 SD 卡的 `Tools/tg5040/`。在 NextUI Tools 中打开 **PocketJS App**。

- 上下方向键移动；长按约 0.4 秒开始重复，列表自动滚动，让当前项保持可见。
- A 选择当前项，底部显示选中内容和选择次数；持续按住只触发一次。
- B 或 MENU 退出并返回 NextUI。
- 日志：`.userdata/shared/pocketjs-brick/app.log`。

## 当前边界

这是实验性的 Brick 宿主，使用上游面向自定义宿主的无 build plan 编译入口。宿主标识 `brick-experimental`、本地 ABI 1、密度 1，按 60 Hz 的虚拟时间推进。没有宣称上游已正式支持 Brick。实际呈现速度受设备渲染和 vsync 耗时影响，日志不能作为 60 FPS 验收证据。

列表只挂载可见区域及前后缓冲行。字体从项目的 `fonts/font1.ttf` 按当前源代码里的字符烘焙，标题 54 px、正文 36 px、辅助文字 24 px。任意动态中文、配置持久化和异步网络属于后续阶段。

自动检查运行真实 ARM64 QuickJS、应用 bundle 和 PocketJS 核心，覆盖导航、滚动、选择与按键边沿。桌面 SDL dummy 后端只用于窗口呈现验证。Brick 显示、中文外观、持续滚动响应和真机性能需要设备确认。
