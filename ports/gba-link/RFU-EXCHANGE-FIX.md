# RFU 主机交换与等待修复

2026-10-07 双 Brick 会话观察到内部队列溢出，以及菜单/选角持续红标。此次修复按 [gba-link-connection 的实机协议记录](https://github.com/afska/gba-link-connection/blob/master/docs/wireless_adapter.md#senddata---0x24) 调整 RFU 的交换模型，不能把现有 32 包 FIFO 的无丢弃当作硬件协议正确。

## 改动

- 客机 `24h/25h` 仅暂存发送数据，直到主机发起交换才发送一次；不再独立向主机连续注入数据。后一次暂存覆盖尚未发送的数据，符合适配器行为。
- 接收使用单包 mailbox，游戏读取后清空。新交换覆盖未读取的旧交换；这属于实机行为，用 `host_replaced/client_replaced` 单独计数，不能将它们称为零数据损失或 FIFO 顺序保证。旧 `*_dropped` 保留兼容诊断，已不再表达 mailbox 替换。
- 主机分别统计每个实际连接的客机的发送和确认次数；可靠有序 TCP 下，全部确认后正常结束等待，无需等待非空客机游戏负载。
- 客机确认在接收和发送暂存 uplink 后生成；客户端数据不再因 switch fallthrough 被当作确认。确认校验设备 ID 与网络 peer ID。
- 未完成的交换仅报告真实收到数据的槽位；移除固定 `0x00000F0F`，避免将所有四个槽位立即虚报为 inactive。实机 inactivity 是约四秒的条件，不能用一次 RF 帧超时替代。
- 零字节主机交换也唤醒客机；配置 timeout/retry=0 采用无限等待语义，前端仍有连接超时和退出机制。

发送/确认计数依赖当前可靠、有序的 TCP，不可直接用于 UDP。该改动需要两端相同核心；现有核心二进制哈希握手会拒绝混装。

## 验证边界

真实 `rfu.c` 的 ASan/UBSan 测试覆盖最近交换、读取清空、客机暂存、单次主机驱动 uplink、ACK 与游戏负载分离、错误 peer ACK、正常确认结束等待、超时实际槽位掩码、零配置和零字节事件、重置。14 项原有 TCP/SIO/暂停/断开/输入/非法 RFU 包回归通过。

商业游戏和 Brick 无线复测须另记录，不以底层测试代替。

## Mac 商业游戏验证

两个独立 gpSP 解释器进程使用实际生产 TCP 代码，在 BTME 配对，完成组合、选角、球场选择并开始 Mario / Luigi 比赛。两端截图一致显示 Mario 15–0。该测试的游戏 P1/P2 分配与 TCP 主客角色相反，也验证了 RFU 角色不应按 TCP 主客硬编码。仅 Mac 局域回环；整场比赛、实际无线延迟及 Brick 红标改善尚未验收。日志、输入帧、SRAM 副本和截图保存在忽略目录 `build/gba-link/exchange-fix/game/`。
