# Brick Creative：像素工坊

探索 NextUI / TrimUI Brick 上的 PocketJS 上限。TypeScript / Solid + QuickJS 负责交互和作品，C 宿主负责像素绘制与硬件反馈。独立于 AI 翻译及聊天应用。

## 操作

- Select：画布、动画、性能与硬件、3D 透视四页切换。
- 画布：方向键移动，A 绘画（按住可连续绘画），X 切换铅笔/填充/橡皮，L/R 换色，B 撤销，Y 重做。
- L+A 保存，R+A 导出 SVG；L+B 复制帧，R+B 删除帧；L+方向键调整播放速度。
- 动画页：左右切帧，A 切换洋葱皮，X 旋转所有帧。
- Start：播放/暂停；MENU 退出。Mac：Enter/Space=A、Esc=B、X=X、Z=Y、Q=L、E=R、Tab=Select、P=Start。
- 性能页：左右切换 64/256/1024/4096 个 PocketJS UI 节点，A 请求灯光与短震动，X 新建 16/32/64 画布（可撤销）。
- 3D 页：左右旋转立方体，Start 自动旋转。此页为软件渲染的 UI 透视变换，不是 Pocket3D/wgpu 移植。

作品与 SVG 放在卡的 `.userdata/shared/pocketjs-brick/creative/`。最多 8 帧、24 次撤销，2—24 FPS。作品需手动保存，退出不自动保存。

硬件读取在后台线程执行，震动最长 60 ms；灯光第一次请求前记录原始帧和效果，退出时恢复。Mac 不写硬件接口。UI 绘制耗时包含 guest、软件绘制与纹理上传，呈现耗时单列；FPS 是实际 SDL 呈现间隔，不能当作完整按键延迟。静止节点和持续变化场景必须分别测量。

## 构建与预览

先按 APP.md 准备基础依赖及固定上游源码。

```bash
./ports/pocketjs-brick/creative/build.sh
./ports/pocketjs-brick/creative/preview.sh
```

Brick 包：`build/pocketjs-port/Brick Creative.pak/`，复制至卡的 `Tools/tg5040/`。Mac 数据独立于 SD 卡。

## 验证与当前性能

Mac 构建后运行 `python3 ports/pocketjs-brick/creative/verify.py`；ARM 验收在 `nextui-pocketjs-verify:local` 容器中运行同一脚本。`verify-model.ts` 用固定 Bun 运行，检查完整 64×64 填充、越界保护、损坏作品拒绝及最密集 SVG 不超出 128 KiB 写入上限。

2026-10-01 真机测量：每个场景预热 10 次、连续 240 次更新；每帧改变全部节点颜色。测试与 SNES 游戏同时运行，属于当前负载下的参考值，不能视为空闲设备极限，也不包含 SDL 上传、呈现或真实按键延迟。

| 场景 | guest + 软件绘制平均 | 峰值 |
| --- | ---: | ---: |
| 64 个动态节点 | 9.34 ms | 17.50 ms |
| 256 个动态节点 | 22.53 ms | 28.17 ms |
| 1024 个动态节点 | 77.46 ms | 89.11 ms |
| 4096 个动态节点 | 314.52 ms | 329.34 ms |
| 自动旋转透视立方体 | 5.44 ms | 8.95 ms |

因此像素画布通过原生接口合并绘制，UI 节点留给控件。4096 节点动态页是压力测试，会明显变慢。性能页实时 FPS 才包含屏幕呈现间隔；切换场景后需等采样窗口稳定。

Mac / ARM 自动验收通过填充、撤销、原子保存、SVG 与重启恢复；真机后台验收也通过。灯光和马达写入、遥测读取及正常退出恢复已在真机核对。当前未打断正在运行的游戏，最新版的真机显示与手柄交互待集中验收。
