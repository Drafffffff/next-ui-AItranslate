# Cloudflare 分享入口

此 Worker 把已部署的 Vercel 服务放在固定上游 `https://workshop-game-share.vercel.app` 后面。掌机和网页的上传、卡带读取、HTML、JS、CSS、WASM 均使用 Cloudflare 同源入口，不跳转回 Vercel。现有 Blob 数据和上传密钥保持不变；无需把密钥另存进 Worker。

仅允许分享服务实际使用的路径。上传认证交给现有 Vercel API，授权头只转发给上传路径，Cookies 和其他客户端头不转发；上传接收预算 6MiB（上游仍限制卡带 3MiB），上游请求超时 25 秒，失败返回应用能重试的 503。默认关闭 Worker 调用日志。

## 验证与部署

```sh
node ports/workshop-share/cloudflare/verify.mjs
npm exec --yes --package=wrangler@4.146.0 -- wrangler deploy --dry-run --config ports/workshop-share/cloudflare/wrangler.toml
npm exec --yes --package=wrangler@4.146.0 -- wrangler login --device --browser=false --scopes account:read user:read workers:write workers_scripts:write workers_routes:write
npm exec --yes --package=wrangler@4.146.0 -- wrangler deploy --config ports/workshop-share/cloudflare/wrangler.toml
```

部署后使用命令输出的完整 HTTPS `workers.dev` 地址验证：

```sh
WORKSHOP_SHARE_KEY_FILE=build/workshop-sharing-upload-key node ports/workshop-share/cloudflare/verify-live.mjs https://实际入口.workers.dev
```

省略地址时，脚本通过本地 Worker 函数访问实际 Vercel，不能证明 Cloudflare 已部署或掌机可连接。脚本只重复上传公开的测试卡带，绝不读取用户项目或会话；凭据不会打印。真实连接的有限重试次数会写入验证回执。

2026-10-02 已通过手机设备授权登录并部署：https://workshop-game-share-gateway.drafff.workers.dev。部署版本 `a2bacd13-133c-4433-b757-cace7e40cade`。完整生产路由、已存卡带读取、WASM SHA256 和鉴权测试卡带上传通过，回执见 `build/workshop-cloudflare/live-verification.json`。

掌机 DNS 把域名解析至错误地址；分别指定可信 DoH 返回的两个正确 A 记录，使用应用 CA 且不关闭证书验证，HTTPS 仍连接超时。另测 8443 端口 TLS 失败。DeepSeek 同一应用 CA 返回 HTTP 401，说明设备有其他可用的 HTTPS 链路。详见 `build/workshop-cloudflare/device-network-verification.json`。没有修改系统 DNS 或掌机配置，仍使用原 Vercel 分享入口。只有实际直连、原生上传和扫码游戏链路通过后，才切换掌机分享地址。

## DNS 与其他 Cloudflare 服务复查（2026-10-02 18:29–18:35）

回执：`build/workshop-cloudflare/dns-service-investigation.json`。所有掌机 HTTPS 请求均禁用代理、使用应用 CA 并验证证书；没有修改系统 DNS、hosts 或分享配置。

- 当前 Worker 的可信 A 记录仍为 `104.21.69.103`、`172.67.207.106`。本次指定正确 IP 后 TCP 已连通，但 TLS 握手失败；另试两个 Cloudflare 公共站点 IP，同一 Worker 主机名仍失败。这与早先连接超时的样本不同，说明修正 DNS 并不足以让现有 `workers.dev` 入口可用。
- `www.cloudflare.com` 在同一 `104.16.123.96` 地址、同一掌机 curl 和 CA 下返回 200，而 Worker 主机名失败。Cloudflare API 入口也能完成 TLS。不能概括为整个 Cloudflare 网络不可达；现象与域名或服务入口相关，但这些请求不足以确定失败由哪个网络设备或策略引起。
- 掌机只有 IPv6 链路本地地址，没有公网 IPv6 默认路由，暂不能通过 AAAA 记录切换解决。
- 官方文档链接的 Pages 示例 `formspree-example-cloudflare-html.pages.dev`：两个可信 A 记录分别指定后均返回 200；随后直接使用域名再次请求也返回 200，耗时约 0.93 秒。首次直接域名请求曾解析失败，因此只证明当前样本可达，不能保证 DNS 始终稳定。
- 掌机直连阿里 DoH `https://dns.alidns.com/resolve` 可以取得该 Pages 示例的正确 A 记录；同一 DoH 对当前 Worker 却返回与可信解析不一致的地址，说明仅加密解析不保证上游答案正确。Google/Cloudflare DoH 本次在掌机分别超时/握手失败。
- 回执中的两个 `nslookup` 子测试没有运行：设备缺少 `timeout` 命令，退出 127；它们不是网络失败证据。

优先候选为 **Pages Functions advanced mode**：复用现有固定上游网关作为 `_worker.js`，让掌机上传及手机的 HTML/JS/WASM/卡带读取均走新 `pages.dev` 同源入口。Vercel 保留为上游和存储，无需自有域名或常驻本地转发进程。官方支持从 Worker 迁移，但此处尚未部署自己的 Pages 项目，也未证明该项目的上传/播放闭环可达。当前 OAuth 凭据读取 Pages API 返回 403，需补 Pages 发布权限后才能部署验证，不能把公开示例的 200 写成我们的服务验收通过。

其他路线：自有域名绑定 Worker 可以改变入口域名，仍需实际验证；Tunnel 连接源站与 Cloudflare，访问者仍需连上 Cloudflare 公网入口，Quick Tunnel 还需常驻进程且仅适合开发测试；China Network 是企业套餐的单独订阅并要求域名 ICP，不是免费 Worker 的开关。当前 Worker 的 HTTPS DNS 记录提供 ECH 配置，但掌机旧 curl/OpenSSL 不具备相应客户端能力，ECH 路线尚未测试。

官方依据：[Pages advanced mode](https://developers.cloudflare.com/pages/functions/advanced-mode/)、[Pages 示例](https://developers.cloudflare.com/pages/tutorials/add-an-html-form-with-formspree/)、[Worker 自有域名](https://developers.cloudflare.com/workers/configuration/routing/custom-domains/)、[Tunnel](https://developers.cloudflare.com/tunnel/get-started/)、[Quick Tunnel](https://developers.cloudflare.com/tunnel/get-started/quick-tunnels/)、[China Network](https://developers.cloudflare.com/china-network/)、[ECH](https://developers.cloudflare.com/ssl/edge-certificates/ech/)。

## 当前生产入口：自有域名（2026-10-02 18:41）

用户要求检查账号域名后，Cloudflare Zones API 查到已激活的 `dong-xi.cn`。`games.dong-xi.cn` 创建前公开 DNS 为 NXDOMAIN，Worker domains changeset 无更新、删除或冲突；绑定时禁止覆盖现有 DNS 或其他源站。现有网关已绑定 **https://games.dong-xi.cn**，Wrangler 配置保留对应 custom domain 路由，没有购买域名或启用付费套餐。

掌机无需代理、无需改 DNS 即可完成 HTTPS health 200。新域名实际生产页面/JS/CSS/WASM SHA 和鉴权上传通过。原生应用在独立临时测试目录中直接上传公开脚手架，生成二维码；独立 zxing 解码通过，掌机直接回读上传结果，原始 JSON 的 SHA256 与内容地址一致，Lua/gfx/sfx/music 与公开测试卡带对应部分一致。一次 curl 28 上传超时由现有自动重试成功恢复，不宣称网络零波动。未上传用户项目、会话或草稿，测试目录已清理。

生产配置已通过 `configure-sharing.py --endpoint https://games.dong-xi.cn` 备份、合并并核对 SHA；模型/ASR 配置保留。后续默认配置命令也采用新入口。应用重新打开即可读取；已有旧域名的二维码在下次按 R 分享时因入口不同而重新生成。没有删除旧服务。

证据：`build/workshop-cloudflare/custom-domain-preflight.json`、`custom-domain-deployment.json`、`custom-domain-device-verification.json`、`custom-domain-live-verification.json`、`custom-domain-native-share-verification.json`、`custom-domain-device-config-verification.json`。浏览器也加载了原生上传的游戏并进入“准备好了”状态，控制按钮可用，无控制台错误；这不是当前手机无代理播放的实测证明。后续如需跨运营商确认，应使用实际手机无代理访问新域名。
