# 可独立部署的游戏分享服务

上传API、实际FAKE-8/z8lua网页运行器和持久卡带存储在同一服务，不调用Vercel或Cloudflare。鉴权、卡带校验和内容地址复用现有Vercel API的实现；上传只包含已发布卡带及标题/摘要，不包含会话或模型凭据。

```sh
node ports/workshop-share/standalone/build.mjs
node ports/workshop-share/standalone/verify.mjs
```

部署产物为 `build/workshop-native-share/`。Docker镜像只包含编译服务器和公开运行器资源。`SHARING_UPLOAD_KEY` 必须在平台秘密配置中设置，不能写入镜像；沿用掌机现有分享密钥即可。原数据不存在于这个产物中。

## Sealos 部署参数

- 容器端口：3000。
- 单实例起步，开启公网访问，记录实际分配的HTTPS地址。
- 持久卷挂载 `/data`，游戏保存在 `/data/games`。容器用户UID/GID1000，挂载卷必须可写；Kubernetes可设置 `fsGroup: 1000`。
- 环境变量 `SHARING_UPLOAD_KEY`：现有随机上传密钥，至少32字符。
- 健康检查 `/health`。
- 资源、持久卷与公网流量会受账号套餐限制，登录后确认试用配额；本次未购买或创建付费资源。

卡带保存采用临时文件、fsync、原子改名和目录fsync；相同内容仍使用同一个64位SHA链接。读取时重新验证文件SHA，损坏文件拒绝返回。单机持久卷适用于当前小规模服务，不能直接改为多实例加各自独立磁盘。

## 验收门槛

2026-10-02 掌机使用应用CA、无代理访问 Sealos 官方公开文档应用 `https://sjrkwxeygluy.bja.sealos.run/en/docs/guides/applaunchpad/add-domain/` 返回 HTTP200，证明该示例入口可连接，不能证明尚未创建的新游戏服务已部署或可用。

必须在自己的实际应用地址上验证健康检查、原生上传、QR解码、手机无代理运行，以及服务重启后卡带仍存在，再切换掌机 shareEndpoint。部署后修改的是同源完整服务地址，因此现有客户端无需把上传URL和游玩URL拆开。旧Vercel/Cloudflare链接不会自动迁移；切换后可重新上传已发布版本生成新链接。

验收后使用配置合并工具切换入口（保留其他模型/ASR设置，备份并校验SHA）：

```sh
python3 ports/pocketjs-brick/workshop/configure-sharing.py --endpoint https://实际应用域名
```
