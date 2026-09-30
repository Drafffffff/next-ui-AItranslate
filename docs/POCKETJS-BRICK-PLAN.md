# PocketJS / NextUI / TrimUI Brick 阶段计划

目标：让 PocketJS 应用通过 NextUI 的 Tools 菜单在 TrimUI Brick 上运行，后续应用主要使用 TypeScript 开发。现有游戏内 AI 翻译独立维护。

首次记录：2026-10-01。目标平台为本项目的 `tg5040` 工具链及 NextUI v6.14.0（20260719）。

## 阶段与验收

| 阶段 | 工作 | 验收标准 | 状态 |
| --- | --- | --- | --- |
| 1. 底层可行性 | 固定上游版本；检查 Rust、QuickJS、交叉编译与系统库；接入显示输出 | Brick 能启动、显示测试画面、正常退出 | 进行中 |
| 2. 最小应用 | 接入方向键、A/B、MENU；中文列表、按钮、滚动；Tools `.pak` 打包 | 从 NextUI 启动，操作正常，退出返回主界面 | 待开始 |
| 3. 应用能力 | 中文字体、文件与配置、异步网络、加载及错误反馈 | 中文无缺字，配置持久化，网络请求不阻塞操作 | 待开始 |
| 4. 实际应用 | 用 TypeScript 开发 AI 聊天工具 | 配置 Key、请求回复、阅读长文本、保存记录 | 待开始 |
| 5. 优化与开放 | 测量启动、内存、滚动；示例、构建、安装文档 | 其他开发者可照教程构建和安装应用 | 待开始 |

每阶段以真机验证决定是否继续；交叉编译成功不等同于真机验收通过。没有掌机或 SD 卡时，推进源码适配、构建和自动验证，并明确留下真机待验项目。

## 第一阶段实施顺序

1. 固定 PocketJS 上游提交，记录核心与宿主依赖。
2. 优先复用其可移植 UI 核心及软件渲染，避免把桌面窗口系统作为掌机的前置条件。
3. 使用本项目的 tg5040 sysroot 交叉编译，检查 ELF 架构、动态加载器及共享库要求。
4. 制作最小显示验证程序和可安装的 Tools 包，处理退出、日志与运行环境。
5. 在 Brick 验证显示、按键、退出与首次性能基线，再确定后续工期与优化目标。

## 开发边界

- 适配源码放在 `ports/pocketjs-brick/`，下载的上游源码和构建产物放在被 Git 忽略的 `build/pocketjs-port/`。
- 不改动已有 AI 翻译功能，不自动替换掌机系统文件。
- 优先完成显示与运行器，再接入 TypeScript/QuickJS；原生核心测试画面不能标记为完整 PocketJS 应用。
- 从最小应用阶段记录启动时间、按键反馈、长列表滚动、内存，以及网络请求期间的操作响应。

## 进度记录

- 2026-10-01：保存阶段计划，开始上游源码及工具链检查。
- 固定 PocketJS 提交 `ed2d84af39fc0adc6a688cf0a1bef801a3084e26`，采用其 `engine/ui-cabi` 的 C 接口与软件渲染。上游桌面宿主不是本次移植的基础。
- 原生核心已用 tg5040 GCC 和上游固定 Rust nightly 构建成 ARM64 ELF。直接 GLIBC 符号要求为 2.17，依赖 NextUI 已使用的 SDL2 / SDL2_ttf 及系统库；实际卡上库兼容仍需真机检查。
- 已实现 `ports/pocketjs-brick/`：固定版本构建、色块测试界面、Brick 按键映射、退出清理、性能日志和 Tools 启动包。
- 目标 sysroot 内的真实核心检查通过：1024×768 帧缓冲、RGB 顺序、增量更新、方向键/A/MENU、120 次状态更新与退出后重新初始化。
- 同一 ARM64 程序在桌面 SDL dummy 驱动下完成窗口创建、纹理上传、呈现与正常退出。目标 SDL 不提供 dummy 驱动，该结果不证明 Brick 显示后端通过。
- **阶段 1 尚未完成真机验收**：当前未连接 Brick / SD 卡；TypeScript/QuickJS 尚未接入。下一步是安装测试包验证显示、输入与返回 NextUI，再进入阶段 2。

上游：[PocketJS](https://github.com/pocket-nexus/pocketjs)。

## 当前构建与验证入口

```bash
./ports/pocketjs-brick/build.sh
./ports/pocketjs-brick/verify.sh
```

生成 `build/pocketjs-port/PocketJS Smoke.pak/`。将整个目录复制到卡上的 `Tools/tg5040/`；详细步骤见 [适配目录说明](../ports/pocketjs-brick/README.md)。

原始构建、验证日志和截图保留在被忽略的 `build/pocketjs-port/`，不作为源码提交。
