# PocketJS App：口袋聊天测试版

只适用于 **NextUI + TrimUI Brick**。在掌机上使用 DeepSeek 聊天，支持中文回复、长文滚动和本地聊天记录。现有游戏翻译功能独立保留。

## 安装与使用

1. 将编译好的 `PocketJS App.pak` 整个文件夹复制到 SD 卡的 `Tools/tg5040/`。
2. 启动一次 **Tools → PocketJS App**，程序会创建设置文件。退出后在电脑上打开：

   ```text
   .userdata/shared/pocketjs-brick/app/config.json
   ```

3. 在 SD 卡 `.userdata/shared/ai-keys.txt` 填入 `DEEPSEEK_API_KEY=你的Key`（[获取 Key](https://platform.deepseek.com/api_keys)），其他设置保持默认。与 AI 翻译共用；已有公共配置无需重复填写。旧 `config.json` 的 `key` 仅作兼容回退。Key 不写入日志或编译包。
4. 连上 Wi-Fi，再打开应用。选择快捷问题，或者在 `prompt.txt` 中写好中文问题后选择“发送 prompt.txt”。文件与设置位于同一目录。

默认使用 `deepseek-flash`，关闭思考模式并限制回复长度，降低等待时间。接口参数见 [DeepSeek 官方文档](https://api-docs.deepseek.com/api/create-chat-completion/)。

默认设置：

```json
{
  "version": 1,
  "key": "",
  "baseUrl": "https://api.deepseek.com",
  "model": "deepseek-flash",
  "timeoutMs": 30000,
  "savedCount": 0
}
```

上下移动，长按连续移动；A 确认。B 返回上一页，在菜单中退出；X 返回菜单，请求中按 X 取消；MENU 随时退出。

提供英文 / 数字屏幕键盘；中文问题可用快捷提问或 `prompt.txt`。没有接入中文输入法。最近回复和历史记录可以上下滚动阅读。聊天上下文保留最近 8 条，并限制长度；完整记录按容量保留最近消息对。只有收到有效回复后才保存到 `history.json`，清空记录需要再次确认。

## 构建与验证

```bash
./ports/pocketjs-brick/build-app.sh
./ports/pocketjs-brick/verify-app.sh
```

需要 Git、Docker、unzip，以及 gh 或 curl。Bun 版本固定，可通过 `POCKETJS_BUN` 指定同版本程序。构建使用系统公共 CA 证书包，可通过 `POCKETJS_CA_SOURCE` 指定 PEM 证书包；证书 SHA-256 写入构建记录。

产物：`build/pocketjs-port/PocketJS App.pak/`。该程序通过系统 `curl` 请求 HTTPS，验证证书，不关闭 TLS 校验。Key 经 stdin 配置传递，不放在进程参数中；请求体通过匿名文件描述符传递。日志为 `.userdata/shared/pocketjs-brick/app.log`。

配置和记录通过后台线程写临时文件、同步并重命名；网络在独立线程运行，超时、取消、体积限制和退出均有边界。主线程只处理输入、完成结果和绘制。字体优先使用卡上 `.system/res/font1.ttf`，缺字时使用包内的 Noto Sans SC 补字，附 SIL Open Font License。

界面标题 54 px、列表和正文 36 px、辅助文字 24 px。动态正文使用独立字体槽，后台生成缺少的字形并缓存，最多 4096 个字形；主线程加载图集后才显示文本。支持字体覆盖的 BMP 中文，表情等超出范围的字符替换为 `?`；两种字体都未覆盖的字形会提示错误。长文本按字形宽度排成行并使用虚拟列表，避免挂载全部行。

宿主仍为实验性的 `brick-experimental` ABI 1、密度 1，使用自定义宿主编译入口。源码中的导航示例保留在 `app/navigation-smoke.tsx`。按 60 Hz 虚拟时间推进不代表已经验证真机达到 60 FPS。

自动检查运行真实 ARM64 ELF、目标 sysroot、QuickJS 和应用包；本地 HTTP 服务覆盖 POST 请求、转义、错误码、超时、取消、响应限制、证书拒绝、动态中文、持久化和重启。不会调用外部付费 API。桌面 SDL dummy 检查只验证呈现代码；完整应用的 Wi-Fi、真实 DeepSeek 响应、设备字形、长回复与响应时间集中做一次真机验收。

## Mac UI 开发环境

首次准备源码与构建工具后安装本地显示依赖：

```bash
brew install sdl2_ttf pkgconf
```

构建并打开原生窗口：

```bash
./ports/pocketjs-brick/build-mac.sh
python3 ports/pocketjs-brick/preview-mac.py
```

首次构建需要已运行过 `build-app.sh` 下载固定版本源码和 Bun。Mac Rust 工具链独立保存在被忽略的构建目录，不修改全局 Rust 环境。修改应用后重新执行这两个命令。

复用同一份应用 TS、QuickJS、PocketJS 软件渲染、字体和后台服务。窗口使用 1024×768 的逻辑尺寸，可缩放；方向键移动，Enter / 空格对应 A，Esc / Backspace 对应 B，X 取消，关闭窗口退出。默认连接本地模拟服务，提供延迟的中文长回复，不需要真实 Key、不调用付费 API。

预览配置和记录独立放在 `build/pocketjs-port/mac-preview/data/`，不读取 SD 卡设置；模拟聊天记录可跨重启保存。模拟服务只监听随机端口的 `127.0.0.1`，窗口退出后关闭服务。Mac 自动验收：

```bash
python3 ports/pocketjs-brick/verify-chat.py
```

2026-10-01：原生 Cocoa 窗口实际验证了菜单、模拟聊天、中文长回复、滚动、返回和屏幕键盘；Mac 自动验收及 Brick 目标构建自动验收通过。修复了同一帧内按下并松开可能漏掉输入的问题，添加快速按键检查。Mac 预览用于 UI 开发，设备显示驱动、实际 Wi-Fi 和真实 API 响应仍需在 Brick 集中验收。
