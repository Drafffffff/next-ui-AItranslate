# Brick Mic

适用于 **TrimUI Brick + NextUI**。内置麦克风通过蓝牙把音频传到 Mac 或 Bazzite，电脑调用百炼语音识别并填入文字。

这是语音转文字工具；当前版本不注册 macOS 系统麦克风设备。

Bazzite 后台接收端与全局 **L1 + R1「连接电脑」** 操作见 [Linux 安装和使用说明](../../docs/BRICK-MIC-LINUX.md)。MENU 仍退出应用。

## 安装和使用

1. 在 [Release](https://github.com/Drafffffff/next-ui-AItranslate/releases/latest) 下载 `Brick-Mic-TrimUI-Brick.zip`，解压后将 `Brick Mic.pak` 放到 SD 卡 `Tools/tg5040/`。
2. Mac（Apple Silicon，macOS 13+）下载 `Brick-Mic-macOS-AppleSilicon.zip`，将 `Brick Mic.app` 拷到「应用程序」，从 `/Applications/Brick Mic.app` 打开并允许蓝牙。在设置填写 [百炼 API Key](https://bailian.console.aliyun.com/?apiKey=1) 并保存；已有 `~/.zshrc` 的 `DASHSCOPE_API_KEY` 会自动读取。保存直接写入该文件，不读取钥匙串。发布版使用固定本地证书签名，未经过 Apple 公证；首次被系统阻止时，在「系统设置 → 隐私与安全性」手动允许打开。
3. Brick 打开 **工具 → Brick Mic**，同时按 **L1+R1** 打开「连接电脑」，选择「查找电脑」，再选择自己的 Mac。以后记住上次选择，不会自动换到另一台电脑。
4. 按住 **A** 说话，松开结束；录音或识别中 **B** 取消，平时 **B** 删除 Mac 输入框文字；**MENU** 返回 NextUI。
5. Mac 开启「自动输入」，在「系统设置 → 隐私与安全性 → 辅助功能」添加并开启 `/Applications/Brick Mic.app`，再点目标软件的文本框录音。设置中的「测试输入」会等待 3 秒，方便先点击目标文本框验证。授权后仍提示未生效时，先正常退出并重新打开正式应用再检查。

Mac 需要联网，Brick 无需 Wi-Fi。本项目及后续语音输入默认使用 **`qwen-audio-3.1-asr-flash-message`**。默认北京地址为 `wss://dashscope.aliyuncs.com/api-ws/v1/inference`，也支持百炼提供的 workspace 域名；地域与 Key 必须匹配。升级会迁移旧模型及 realtime 路径，保留原域名和 Key。详见[官方客户端协议](https://help.aliyun.com/zh/model-studio/qwen-asr-message-client-events)。

Key 也可从 `~/.bashrc`、`~/.config/fish/config.fish` 或进程环境变量读取，兼容 `BAILIAN_API_KEY`；保存时统一更新 `~/.zshrc` 的 `DASHSCOPE_API_KEY`，保留其他 shell 配置。只读取字面赋值，不执行 shell 文件。

普通输入模式不会发送消息，录音期间切换前台应用时会保留结果供复制。Codex 模式只有在掌机按 **X** 才会发送；任务控制需要另行安装下方的钩子，并提供官方任务元数据快照。

掌机界面跟随 NextUI 系统设置里的主题色与字体。更换主题后重新打开 Brick Mic 即可生效。

界面使用 PocketJS + Solid + QuickJS。先显示 Loading，再在后台打开蓝牙、准备语音服务、连接 Mac；初始化失败会留在界面内提示。

服务异常时自动重试，仍未恢复可按 **A** 重新连接，无需退出应用。

Mac 先在本地保留前 0.5 秒音频，不足 0.5 秒的录音直接忽略，不调用百炼。达到门槛后完整上传已缓存的开头，并继续流式上传；松开 A 后主动结束识别，一次显示完整结果。误触不会清掉 Mac 上一次的识别结果。门槛按音频实际时长判断，蓝牙传输慢不会把短录音误判成长录音。连接时优先使用上次的 Brick；已连接时停止扫描，找不到设备时扫描 8 秒、暂停 12 秒后重试。

## Mac 光标和 Codex

- 底部同一行左对齐显示说话/删除/发送/Y 任务/SE 编辑/LR 阅读/ST 普通或 Codex；按当前状态隐藏不可用动作。
- 十字键移动 Mac 文本光标，长按连续移动；按住任意肩键同时按方向键选择文字。
- B 删除选中的文字或退格，长按连续退格。录音/识别中仍用于取消。
- SE 只处理文字：撤销、重做、复制、粘贴、全选、换行、查看最近口述。首页十字键编辑 Mac 光标，阅读页十字键滚动正文。掌机不维护可编辑草稿。
- 选中 Codex 任务后，首页直接显示最新 AI 回复，单按 L/R 前后翻阅；按住 A 随时说话，录音显示实际音量波形，松开识别后自动展示本次口述结果，按 X 才发送；结果保留到新 AI 回复到达。SE 可查看口述/AI 回复，菜单首项为全选，并提供删除文字。详情隐藏 B 删除提示，但保留快捷删除；X 仅在 Mac 任务输入框有待发送文字时显示，L/R 仅在正文需要翻阅时显示。Y 仅在 Codex 模式首页打开任务中心：直接列出 Codex 任务，未读数量在对应任务行显示气泡；待确认的操作置顶。普通输入首页隐藏并禁用 Y，只有 ST 可进入 Codex；收到提醒后进入 Codex 查看。只保存启用钩子后每个任务最近一段，过长时明确标注。
- 任务请求权限时，Y →「待确认的操作」查看操作。默认选中拒绝，左右选择，A 确认当前这一条；录音/识别中不处理审批。90 秒未决、断连或请求撤回后交回 Codex 原审批界面。敏感或无法完整展示的操作只能在 Mac 允许。首次钩子信任及 macOS 权限仍需在 Mac 完成，不能由掌机代签。
- L3 输入空格，R3 在普通模式按回车、Codex 模式换行；Codex 仍用 X 发送。首页和编辑菜单可用，录音/识别及其他子页不输入。
- ST 是唯一的模式切换键，在首页切换普通输入和 Codex，底部显示切换目标；ST/Y/SE/X 在子页不生效，子页按 A 确认、B 返回。普通模式依赖你点击目标文本框；Codex 模式绑定任务，主动打开并确认任务输入框。Mac 没打开 Codex 时隐藏相关入口。
- Codex 模式下 Y 打开任务列表，左右翻页，A 选择；X 单独发送，识别、重连、唤醒均不自动发送。
- 掌机顶部与 Mac 窗口显示 Brick 电量；断连时 Mac 标注上次读数。读取不到电量显示 —%，不显示 0%。
- 新增遥控能力只向已信任的 Brick 开放。升级沿用此前语音输入的设备；换新设备后需在 Mac 设置中允许该 Brick 控制输入。当前 BLE 尚未强制系统加密配对，设备识别不等同于加密认证。

Codex 提醒配置：

```sh
python3 ports/brick-mic/codex-hooks/install.py
```

然后在 Codex 的 `/hooks` 中审核并信任通知脚本。正常轮次完成时闪主题色顶灯并短震两次；等待确认时闪黄灯并短震一次。息屏也能提醒，录音时延后震动；打开应用或重连时静默同步未读提醒并确认送达，不因历史同步震动或闪灯；保持连接时到达的新提醒仍正常震动、闪灯。各任务的未读计数和最新 AI 回复继续保留，已失效的审批不补提醒；不重放编辑或发送命令。Y 打开任务中心，按 A 直接进入任务；打开任务阅读回复后清除该任务的未读气泡。新回复到达会更新当前任务页，即使这轮消息从 Mac 发起、或者回复文字与上一轮相同。查看菜单或录音期间不抢走当前操作。

现有桌面任务列表由本机任务快照初始化，之后依赖已信任的生命周期 hooks 更新；不把独立 CLI 服务的任务冒充桌面任务。任务身份与标题以官方桌面元数据快照为准；缺少确认来源时不允许发送。输入框位置必须通过系统辅助功能确认；无法确认时保留文字。该桌面适配仍需实机验收，当前 Stop 接入不覆盖所有异常退出。详见 [接入说明](codex-hooks/README.md)。

## Mac 界面

- 主窗口显示连接或识别状态、掌机电量、识别文字和「自动输入 / 复制文字」。录音时显示时长和音量，结束后一次显示完整文字；长文字可以滚动阅读。
- 点击菜单栏的掌机麦克风图标打开或收起主窗口。右键图标或点窗口右上角「…」，可重新连接掌机或退出应用；关闭窗口会继续在后台接收。
- 应用只运行一个实例。再次打开会显示已有窗口；从构建目录打开另一份副本也不会重复启动蓝牙和语音服务。
- 点击齿轮打开设置：填写百炼 Key、查看输入权限或测试输入。模型与服务地址收进「高级设置」，保存结果直接显示在设置窗口。
- 应用图标和菜单栏图标由同一套掌机麦克风矢量图形生成，界面跟随 macOS 的浅色 / 深色外观。帧数和收尾耗时仅保留在诊断日志中。

## 休眠与开机恢复

- 短按电源键进入快速待机，再短按唤醒；也可直接按住 **A** 唤醒并开始录音，同一次松开结束。A 唤醒只在 Brick Mic 内生效，不修改 NextUI 菜单或游戏的按键。屏幕关闭、录音取消，界面保留，已连接时蓝牙持续保留，亮屏后可直接使用。
- 沿用 NextUI 自动息屏时间。息屏后连续 5 分钟没有蓝牙连接，则进入 NextUI 深度休眠；重连或亮屏取消计时，连接状态不明时不会贸然深休。深度休眠只能用电源键唤醒，期间收不到即时提醒；连回 Mac 后补收。长按电源关机；其他 NextUI 界面不受影响。
- 长按电源键约 1 秒关机。如果停留在 Brick Mic，下次开机会重新打开它；识别正文和录音不保存。
- 按 MENU 正常退出后，下次开机回到 NextUI。应用第一次启动会安装独立启动钩子 `.userdata/tg5040/.hooks/boot.d/60-brick-mic.sync.sh`。
- 开机恢复标记会在启动前消耗，应用启动失败时继续进入 NextUI。

## 编译

需要 macOS、Swift/Xcode Command Line Tools、Go 1.23+ 和 Docker。

```sh
./ports/brick-mic/build-brick.sh
./ports/brick-mic/build-mac.sh
```

产物位于 `build/brick-mic/`。Brick 编译复用项目已固定版本的 tg5040 工具链。

Mac 本地开发时，每次修改后更新正式安装版：

```sh
./ports/brick-mic/build-mac.sh --install
```

Mac 源码构建需要你自己的固定代码签名证书。首次在本机确认现有证书后，执行 `python3 ports/brick-mic/mac-signing.py init --identity "你的证书名称"`；之后始终复用同一身份。下载发布版无需此步骤。

安装会自动备份旧应用、更新 `/Applications/Brick Mic.app` 并重新启动。若已编译，可单独运行 `./ports/brick-mic/install-mac.sh` 安装。

首次编译会通过 `ports/pocketjs-brick/build-app.sh` 准备固定版本 PocketJS、QuickJS、Bun 和 Rust 工具链。

## Mac 上开发掌机界面

```sh
./ports/brick-mic/build-ui.sh mac
python3 ports/brick-mic/ui/preview.py
```

同一份 PocketJS 布局和渲染，使用本地模拟麦克风，不调用百炼。Enter / 空格对应 A，Backspace 对应 B，Esc 退出，P 模拟息屏和唤醒；S 对应 ST（START），X / Y 对应掌机 X / Y，Tab 对应 SE（SELECT），Q / E 对应肩键。方向键编辑 Mac 光标；从 SE 菜单选择「查看最近口述」后，上下键阅读回显。

```sh
python3 ports/brick-mic/ui/verify.py
python3 ports/brick-mic/ui/verify-resume.py
python3 ports/brick-mic/ui/verify-startup.py
python3 ports/brick-mic/ui/verify-recovery.py
```

Mac 界面可独立预览，不连接掌机、不访问百炼、不读取或保存 Key。先正常退出已运行的 Brick Mic；预览同样只允许一个实例，结束后重新打开正式安装版：

```sh
"build/brick-mic/Brick Mic.app/Contents/MacOS/BrickMic" --ui-preview=result --dark
```

状态可选 `ready`、`waiting`、`recording`、`processing`、`result`、`permission`、`settings`；`--long-text` 在结果预览中生成长文字，用于检查滚动。未加 `--dark` 时使用浅色。

## 开发与诊断

实现分为三个部分：静态 Go 蓝牙服务、PocketJS 掌机界面、Swift Mac 接收程序。SDL2 只负责窗口、按键和像素呈现。

- BLE GATT：Brick 外设，Mac 中心；连接握手协商分片大小。
- Tina Linux 4.9 默认会把连接间隔调到 60ms，导致语音积压。应用运行期间请求 15ms，退出时恢复原参数；每 10 帧回报接收进度，实际未收齐音频超过 2 秒时取消本次输入。
- 16kHz 单声道 PCM 采集，独立 IMA ADPCM 帧降低蓝牙流量。每帧携带序号和解码状态；丢失帧会取消本次输入。
- Mac 使用百炼 WebSocket 实时接口，手动提交模式关闭服务端 VAD 等待；松键后发送 `input_audio_buffer.commit` 和 `session.finish`，收到完成事件后显示最终文字。取消或断线不提交临时文字。
- 最长单次录音 60 秒；网络上传缓存上限 3 秒，蓝牙积压另按 Mac 的实际接收进度限制。
- 正常启动只允许掌机 A 键开启录音，远端蓝牙不能主动开始录音。
- 不保存正常录音；诊断模式才显式导出测试音频。日志不记录 Key 或正常识别正文。

服务 UUID：`BA1C0000-7E89-4C31-A2D0-4F923CB1A100`。协议和验证记录见 [实施计划](../../docs/BRICK-MIC-PLAN.md)。

当前 BLE 连接不要求系统配对。请在可信环境使用，后续增加屏幕确认与加密配对。每次只接受一个电脑接收端，只有本机按键才能开始录音。

参考：[百炼实时识别协议](https://help.aliyun.com/zh/model-studio/qwen-asr-message-client-events)、[CoreBluetooth](https://developer.apple.com/documentation/corebluetooth)、[BlueZ GATT](https://github.com/bluez/bluez/blob/master/doc/org.bluez.GattCharacteristic.rst)。

普通输入模式由你选择前台输入位置，口述及十字键/删除等操作直接作为键盘输入，不要求辅助功能能读取或确认输入框。仍需macOS自动输入权限；录音期间切换到另一应用会保留文字供复制，避免误填。Codex模式继续校验选中的任务与对应输入框。

Mac菜单栏始终显示与应用图标一致的“Brick掌机＋麦克风”轮廓：旁边的叉号表示未连接，勾号表示已连接，声波表示正在说话，三点表示识别处理中。连接时同时显示掌机电池图标和百分比，充电时显示闪电；没有有效电量读数时显示—%，断开后隐藏电量。图标随macOS浅/深色菜单栏变化，运行时不加载网络图标，也不增加扫描或定时轮询。
