# BLE v5：RFU 消息无损编码候选

2026-10-08，用户反馈 v4 蓝牙主机流畅、客机持续红标。保留现场日志显示两端约 60 FPS，发送队列未持续增长，RFU 丢弃计数为零；RFU 主机最终 783 个确认，平均 29.167ms、最大 68.796ms，657 个 late_ack，55 次 host_no_reply。角色计数不能直接确定哪台屏幕显示红标；红标也不是底层丢包统计。这支持继续减少交换耗时，而不是放宽红标或等待阈值。

v5 只压缩完整 RFU wire packet 的结构字段：外层类型、完整原始长度、完整 64 位 exchange ID、RFU 类型及完整 32 位 info 使用 canonical unsigned varint；固定 RFU1 前缀解码重建。已有尾零省略仍逐字节还原原长度。原始游戏 payload 不作语义解释；未知类型、分块包或不划算的形态使用原有尾零编码加标识。解码拒绝溢出、非规范 varint、非法类型及长度，分配上限仍为 4096 字节。

保留 v4 的完整 epoch/sequence/CRC、完成 ACK、有界背压、双向窗口和兼容拒绝。默认构建仍为 v3；v5 必须两台同时更新。gpSP 与 frontend 网络逻辑、33.334ms RFU grace、红标显示均不改动。Wi-Fi、热点及三 tab 的交互不变。

合法构造的 22 字节有效数据 RFU host 样本：v4 ATT value 65 字节，v5 41 字节。按 ATT 3 + L2CAP 4、27 字节 ACL 载荷计算为 3 → 2 个分片。0/8/12/16/24/40/92 字节样本亦有对应核算。它不是游戏消息频率、实际连接事件数或固定延迟收益的测量。

本地验证：完整双向 frontend stream 包含 host/client/confirm/broadcast RFU 形态、最大 exchange ID、内部零和尾零，经 20/512 字节传输逐字节校验；随机形态、畸形编码与模糊测试验证边界。显式启用的真实 Mac gpSP 开源 SIO ROM 集成验证通信与 ROM/core/串口拒绝，不代表商业游戏 RFU 比赛已经验收。

```sh
NP_INTEGRATION_RUNNER="$PWD/build/gba-link/core-runner" \
NP_INTEGRATION_CORE="$PWD/build/gba-link/mac-source/gpsp-5819380c2ffb0900219d700a382ee68c464ebb99/gpsp_libretro.dylib" \
NP_INTEGRATION_ROM="$PWD/build/gba-link/link-probe.gba" \
go -C ports/gba-link/bluetooth test -race -ldflags '-X main.bleTransportVersion=5' -v ./...
python3 ports/gba-link/build.py brick --ble-rfu-codec
python3 ports/gba-link/package.py
```

掌机验收由用户进行：相同游戏/角色比较菜单、选角和得分过程，采集平均/p95/p99/max 确认耗时、late_ack/no_reply 与队列；继续监测花屏。若减少分片仍未消除尖峰，再追查本次连接的 HCI 事件与 RFU 同步时序，不删除确认或扩大容忍窗口。任何回归恢复本次部署的原 v4 备份。
