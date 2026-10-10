# 蓝牙短消息候选：本地研究与交付

本阶段只在 Mac 上研究、测试和编译。掌机关闭；没有 SSH、部署或游戏/存档操作。已安装版本仍为 94e0c736，默认构建继续使用 BLE v3。候选构建需显式 `--ble-compact`，使用 v4，必须两台同时更新才能测试。

## 证据与瓶颈判断

已有 4328c156 十分钟蓝牙连续交换通过：数据完整、发送队列无持续增长，104 字节零填充探针中位 RTT 22.734ms、p99 38.205ms、最大 74.944ms。94e0c736 的补测由用户要求提前停止，最后完整进度约 205 秒、8,209 次交换、最大 RTT 123.006ms；这次不能算十分钟通过。两项均为传输探针，不是网球帧率或比赛验收。

已有记录协商 ATT MTU 517、连接间隔 7.5ms。大 MTU 不等于大链路包：已知 Brick 控制器记录为 27 字节 ACL 载荷，不能用 MTU 517 推断一条通知只占一个无线包。队列峰值小且不增长，支持先排查无线分片及事件调度；无法证明不存在进程调度尖峰或游戏 RFU 的同步等待。游戏红标也不是底层丢包计数。

现有代码的应用 ACK 用于确认消息进入有界消费者队列。发送已有四消息窗口，并非每条消息都阻塞到确认才允许下一条；dispatcher 的 100ms ticker 只做超时检查，不是发送周期。发送 backlog 的 1ms 重试、启动/退出的等待不能解释所有稳态尖峰。当前没有证据支持放大窗口或删除 ACK。

## 开源实现的启示

[BTstack LE streamer](https://raw.githubusercontent.com/bluekitchen/btstack/master/example/le_streamer_client.c) 使用 write-without-response、通知及可发送回调推进双向传输。本项目已采用无 ATT 请求响应的写入路径；吞吐样例的发送节奏不能直接替代 GBA 的因果依赖和背压。

[BlueZ 5.78 GATT server](https://raw.githubusercontent.com/bluez/bluez/5.78/src/shared/gatt-server.c) 区分普通通知与多通知缓存；[ATT 调度](https://raw.githubusercontent.com/bluez/bluez/5.78/src/shared/att.c) 将写命令/通知与等待响应的请求分开。当前 raw ATT 客机不声明多通知支持，已有代码避免相应缓存路径；重复绕过同一 timer 不是新优化。

[Zephyr 架构说明](https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/bluetooth-arch.html) 将链路层调度和可靠传输与主机协议分开。[Linux v4.9 L2CAP socket](https://raw.githubusercontent.com/torvalds/linux/v4.9/net/bluetooth/l2cap_sock.c) 对 LE PSM 选择信用流控，说明 CoC 值得作为后续独立路径评估；不能据此宣称 Brick 的厂商内核、固件和双端集成已经支持。未复制这些项目的代码或替换系统蓝牙栈。

## 候选实现与对照

v4 对能放入一个协商 ATT payload 的消息使用 13 字节封装：1 字节标识、完整 32 位 epoch、完整 32 位 sequence、CRC32。消息长度来自 ATT 数据报边界，单片 offset 固定为零，因此可省掉旧格式的长度和分片 offset。多片/大消息仍使用原有格式。首条版本协商消息保持旧分片格式；版本不同在 frontend identity/核心启动前拒绝。消费者队列、完成 ACK、背压、取消和兼容护栏保持。

旧格式每条单片消息开销为 16 字节帧头加 8 字节长度/CRC；新格式为 13 字节，共少 11 字节。一个零填充 104 字节示例在已有尾零编码后是 34 字节，ATT value 从 58 到 47 字节。假定链路载荷 27 字节，加 ATT 3 字节与 L2CAP 4 字节，则分片计数从 ceil(65/27)=3 到 ceil(54/27)=2。

这是字节/分片核算，不是连接事件或延迟模型。一个事件可发送多少包取决于实际控制器调度。不能把少一个包换算成固定 7.5ms 收益。真实协议-3 RFU 包还包含 8 字节交换编号和 12 字节 RFU1 前缀；不同有效数据长度可能减少分片，也可能不减少。`TestCompactTaggedRFUWireCosts` 对 0/4/8/12/16/24/40/92 字节内容逐字节恢复，并输出对照表；这些是合法形态的构造样本，不是实测网球消息频率分布。

队列预算保守沿用旧格式计算；日志 queued_bytes 是预算计数，不可当作候选实际空口字节数。没有隐藏红标、改变 RFU 等待阈值、丢弃数据、预测游戏状态或改动 gpSP 二进制。

## 本地复现

```sh
NP_INTEGRATION_RUNNER="$PWD/build/gba-link/core-runner" \
NP_INTEGRATION_CORE="$PWD/build/gba-link/mac-source/gpsp-5819380c2ffb0900219d700a382ee68c464ebb99/gpsp_libretro.dylib" \
NP_INTEGRATION_ROM="$PWD/build/gba-link/link-probe.gba" \
go -C ports/gba-link/bluetooth test -race -ldflags '-X main.bleTransportVersion=4' -v ./...

go -C ports/gba-link/bluetooth test -race ./...
python3 ports/gba-link/build.py brick --ble-compact
python3 ports/gba-link/package.py
```

核心集成用开源 SIO ROM 与两独立 Mac gpSP，验证原始数据、计分及 ROM/核心/串口拒绝。它不覆盖商业游戏 RFU 比赛。缺少集成环境会 SKIP，不能算通过。针对性测试覆盖协商 payload 边界、CRC/epoch/sequence/截断、引导、双向分片、最大消息、慢消费者、取消、对端退出和前端身份拒绝时的异步清理。

## 下一步优先级和硬件验收

1. 首先只测 v4 短封装：同设备/相同负载先比较 v3 与 v4 的 padded/dense、两角色、延迟分布、队列、取消和兼容失败；同步抓本次连接的 HCI 数据，确认真正减少的是链路包还是事件。不得重置服务或断开无关连接。
2. 用户再测试网球菜单、选角、比赛至少一次完整得分过程，检查持续红标、按键响应和花屏；和 Wi-Fi/热点做同条件对照。没有实际收益或出现回归就按包内 install.py 恢复备份。
3. 若无线包数下降而尖峰不变，下一步追查本次连接的事件/主机调度和 RFU 因果等待；再评估 LE CoC 的厂商内核支持。没有证据前不移除完成 ACK、不扩大队列、不更改默认模式。

蓝牙有连接事件、老控制器包长和共享无线电带来的潜在上限，现有证据无法确定其最终流畅度上限。Wi-Fi 既有同条件连续小包中位约 4.3ms；热点可免路由器、免地址输入，但仍要用实际游戏验收。候选不是已验证的“爆红修复版”。
