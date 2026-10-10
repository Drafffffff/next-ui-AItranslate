# 游戏工坊在线游玩

生产分享地址：https://games.dong-xi.cn ，由 Cloudflare 自有域名转发到 Vercel 源站 https://workshop-game-share.vercel.app 。掌机项目列表按 R，上传已发布版本后显示二维码；草稿和完整会话不上传。相同卡带使用 SHA-256 内容地址，重复分享可使用本地缓存二维码。

网页使用锁定 FAKE-8 `814991a2571ad3970e386cef48f3b148aa1c27b9` / z8lua `e6928578d46b61fd5ea30cfcf547e855a30a0553`，支持触控方向键与 O/X、键盘、手柄、暂停、重开、全屏和音乐/音效。声音需要先点开始或开启声音，以满足浏览器手势要求。卡带在 Worker 内解释，生成的 Lua 不转成网页 JS，也不执行模型生成 HTML。

游玩页采用 PICO-8 配色、硬边框和像素按钮，按 100dvh 和安全区适配一屏高度。只显示游戏画面与必要的触控、暂停、重开、声音和全屏按钮；手机竖屏操作区在底部，横屏将方向键和 O/X 放在两侧。画面保持正方形，按剩余宽高缩放，不依靠截断隐藏按钮；桌面大屏使用键盘或手柄。标题仅保留在浏览器标题与画面无障碍名称中，介绍、版本、品牌页头、操作说明、页脚均不显示。启动完成后先静音绘制首帧，点击开始才开启声音。短触控和键盘点击会保留到下一次 VM 更新，避免按下/松开恰好发生在两帧之间而丢失操作；暂停与重开清空待处理输入。没有持续装饰动画或外部字体请求。

构建时将样式和播放器脚本输出为带 SHA-256 前 16 位的文件名，并更新 HTML 引用。浏览器可以缓存资源，发布新版本时也不会沿用旧 CSS/JS；Cloudflare 和独立服务仅允许格式严格匹配的这两类版本资源。

中文像素字体源自 [Fusion Pixel Font 2026.09.25](https://github.com/TakWolf/fusion-pixel-font/releases/tag/2026.09.25) 的 12px proportional zh_hans WOFF2。字体通过 fontTools 子集化为 ASCII、方向符号、界面文字和 GB2312 一级常用汉字，并更名 Workshop Pixel，产物 103880 字节；缺字由系统字体补足，font-display:swap 避免阻塞页面。手柄图标使用 Iconify 的 `pixelarticons:gamepad`。字体来源及各上游许可、图标 MIT 许可均随站点保存在 `public/fonts/LICENSE.txt`。

## 构建与部署

```sh
npm ci --ignore-scripts
npm run build
npm run verify
```

运行器构建脚本 `build-runtime.sh` 使用 emsdk 4.0.15 和固定源码；产物及第三方许可证在 `public/runtime/`。`verify.mjs` 使用实际 WASM 检查画面、输入、重开、PCM 和语法错误，不能证明任意游戏的玩法正确。

Vercel 项目需要启用 Blob，配置生产环境变量：

- `BLOB_READ_WRITE_TOKEN`：Blob 存储访问凭据。
- `SHARING_UPLOAD_KEY`：随机上传密钥，至少32字符。

掌机 `config.json` 只增加 `shareEndpoint`（上面的生产域名）和 `shareKey`（同一上传密钥），保留模型渠道、ASR和其他设置。凭据不应提交仓库或写入截图/报告。

本次通过 Vercel CLI 的已登录账号进行手动部署，发布目录没有 `.git`，只包含服务源码、依赖清单与公开运行器；没有伪造 Git 作者，也没有上传设备项目、会话和凭据文件。Vercel源目录已恢复为 `ports/workshop-share`；今后的手动发布目录也应保留这个相对层级。生产部署 ID：`dpl_5tT1hjfZBiyodinas2SNiXVdvfwD`。

## 服务边界和验收

`POST /api/upload` 校验鉴权、3MiB请求预算与P8卡带结构，保存标题、摘要、版本和卡带。`GET /api/game?id=<64位SHA>` 返回持久内容；无效ID、未找到和未授权请求分别返回明确错误。密钥常量时间比较，卡带链接使用内容地址。默认 CSP 限制同源 Worker、脚本及网络。

已验证生产部署、Blob读写、手机竖屏/横屏控制、实际VM画面与声音、原生上传，以及掌机截图中二维码的独立解码。证据目录：`build/workshop-terminal/`。

早期掌机网络把 `vercel.app` 解析到错误地址，指定正确地址后 TLS 仍被重置，原生上传首次验收使用临时 SSH 测试代理。2026-10-02 用户要求检查 Cloudflare 账号后，发现已有激活域名 `dong-xi.cn`；现已绑定 `games.dong-xi.cn`。掌机默认 DNS、无代理、证书验证开启时，原生上传、二维码独立解码及上传内容回读通过，一次超时由自动重试恢复。设备分享配置已切至新域名，不依赖临时代理。新域名的当前手机无代理播放仍待用户实际网络反馈。

## Cloudflare 入口

已部署 https://games.dong-xi.cn，完整转发游戏页面、运行器和鉴权API，复用现有Blob；Wrangler 配置保留 custom domain。原 `https://workshop-game-share-gateway.drafff.workers.dev` 仍保留，其域名在当前掌机网络上握手失败，自有域名则已通过直连上传检查。代码与命令见 `cloudflare/README.md`，当前证据在 `build/workshop-cloudflare/custom-domain-*.json`。
