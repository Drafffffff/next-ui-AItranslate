# Brick Mic：Bazzite 接收端

掌机仍是 BLE 外设，电脑是接收端。Linux 端没有窗口；录音上传、百炼识别和键盘控制由后台服务完成。Mac 端继续使用正式安装版。

## 掌机操作

- **L1 + R1 同时按**：全局打开「连接电脑」。录音、识别期间先完成或取消本次输入。
- **上下 / A**：选择电脑；**B** 返回。**MENU** 始终退出应用。
- 当前已加入 Mac mini 和 `bazzite`。选择会保存到 SD 卡 `.userdata/shared/brick-mic/receivers.json`，重启后仍连接所选电脑；所选电脑不在线时不会切换到其他电脑。打开连接菜单时光标停在保存的电脑上，未连接时首页也显示等待的电脑名称；单纯打开菜单或另一台电脑尝试连接不会覆盖偏好。
- 添加另一台电脑：在该电脑运行接收端，在连接菜单选择「查找电脑」。查找最多 30 秒，期间中断当前蓝牙连接；候选电脑不能接收录音或控制输入，必须在掌机选择后才能使用。
- 单独 L1/R1 仍用于阅读与选字；L3 空格、R3 回车保持原行为。Linux 不显示 Codex 任务功能。

每个接收端有固定实例 ID。Linux 首次成功连接后也记住那台 Brick 的蓝牙地址，另一台 Brick 不会自动抢占；需要更换掌机时，停止服务并修改配置中的 `brick_address`，再启动。实例 ID 用于路由选择，不是加密身份认证；当前协议不声称能阻止有意伪造实例 ID 的设备。

## 文件与服务

安装目录：`~/.local/share/brick-mic/linux/`，独立 Python 环境：`~/.local/share/brick-mic/venv/`。

`~/.config/brick-mic/receiver.json` 保存实例 ID、名称、已绑定掌机地址、百炼 WebSocket 地址。不要为升级重新生成 ID；地区和 workspace 域名必须与 Key 匹配。

Key 写在 **`~/.config/brick-mic/credentials.env`**（权限 0600）：

```text
DASHSCOPE_API_KEY=你的百炼Key
```

也可由服务环境变量 `DASHSCOPE_API_KEY` 提供。文件存在时使用文件值，程序不执行 shell 配置。模型固定为 `qwen-audio-3.1-asr-flash-message`，接口 `/api-ws/v1/inference`。

在 Linux 上首次安装：

```sh
mkdir -p ~/.local/share/brick-mic/linux ~/.config/brick-mic
# 下载 Release 的 Brick-Mic-Linux.tar.gz，解压到上述 linux 目录：
tar -xzf Brick-Mic-Linux.tar.gz -C ~/.local/share/brick-mic/linux
# 先创建上述 credentials.env，填写百炼 Key，并设置 chmod 600。
bash ~/.local/share/brick-mic/linux/install.sh
sudo loginctl enable-linger "$USER"
# 保证启动时蓝牙控制器开启：
sudo cp ~/.local/share/brick-mic/linux/brick-mic-bluetooth.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now brick-mic-bluetooth.service
```

安装需要 Python 3、venv/pip、C 编译器及 BlueZ 的 `bluetoothctl`。当前 Bazzite 自带这些环境；源代码构建也可将 `ports/brick-mic/linux/` 复制到同一安装目录。首次打开掌机应用，按 L1+R1 →「查找电脑」，再选择对应电脑。

Bazzite 使用用户目录与 venv 安装，不修改不可变系统的软件包层。虚拟键盘使用系统 Linux 头文件编译的小型 uinput helper，不依赖 Python 开发头文件。后台只以普通用户运行；键盘需要该用户可以写 `/dev/uinput`。当前 Bazzite 已有登录用户 ACL，不新增宽泛的设备写权限。

检查与维护：

```sh
systemctl --user status brick-mic
systemctl --user restart brick-mic
~/.local/share/brick-mic/venv/bin/python ~/.local/share/brick-mic/linux/receiver.py --status
journalctl --user -u brick-mic -n 30
```

接收服务运行期间，发现蓝牙控制器处于关闭状态会通过 BlueZ 正常打开蓝牙，再连接已绑定掌机；这是为处理桌面启动后覆盖早期蓝牙状态的情况。不会重置控制器、删除配对、解除 rfkill 或覆盖系统权限拒绝。需要手动保持电脑蓝牙关闭时，先停止 `brick-mic` 服务。 对应接口见 [BlueZ power 命令](https://github.com/bluez/bluez/blob/master/doc/bluetoothctl.rst) 和 [Bleak 可用性错误分类](https://bleak.readthedocs.io/en/latest/_modules/bleak/exc.html)。

开机自启通过 user systemd + linger 完成；不用等待桌面登录即可接收蓝牙，文字填入则需要可用的桌面会话。关闭 Linux 服务用 `systemctl --user disable --now brick-mic`；关闭专用蓝牙启动单元用 `sudo systemctl disable --now brick-mic-bluetooth`（该单元停止不会关闭正在使用的系统蓝牙）。

## 输入与延迟

录音开始记录 X11 的输入目标；识别结束后目标不变才通过 UTF-8 剪贴板和 Ctrl+V 填入，不自动回车发送。目标变化或键盘不可用时，文字仍返回掌机。中文剪贴板仅保存在进程内存，普通录音不保存文件，日志只记录状态、帧数和延迟，不记录 Key 或口述内容。

支持当前 Bazzite 游戏模式的 X11 输入以及 X11 桌面。原生 Wayland 应用的目标识别和剪贴板需另行适配；不能把 XWayland 检查结果当成原生 Wayland 已验证。

方向键、删除、选字和编辑快捷键在有 X11 输入目标时通过 XTEST 发送，避开 Gamescope/HHD 对虚拟键盘设备的事件处理；没有 X11 目标时使用 uinput。选字结束或指令取消都会释放按键，避免 Shift 等修饰键卡住。Steam 虚拟键盘的实际操作仍需在对应界面验收，后台发送成功不代表文本框已经执行。

小于 0.5 秒的误触不调用识别服务；其他输入在录音期间持续上传 PCM，结束后立即发送 `finish-task`，只返回完整最终文本。收到 `task-finished` 后立即填入，WebSocket 的关闭握手由原识别任务在后台完成；蓝牙断开或服务退出时仍会回收这些任务。连接期间停止扫描；未找到掌机时最多扫描 8 秒、暂停 12 秒再试，曾找到设备后的断开或重试间隔为 3 秒。切换电脑可能需要等候接收端下一次重连，不保证零等待。

接收端只有完成握手和初始状态发送后才报告 `connection=connected`、`phase=ready`。诊断状态包括接收、投递、拒绝的编辑指令数量及上一段识别的分阶段耗时，不包含录音或正文；蓝牙日志只记录初始化阶段及协议错误编号。掌机按服务端实际 ATT MTU 决定通知分片容量，不依赖 Linux 客户端可能未更新的容量值。

## 验证

```sh
cd ports/brick-mic/linux
python3 -m unittest -v test_receiver
cd ../brick
go test -race ./...
```

包含分片乱序、ADPCM 边界、音频上传结束顺序、最终句子合并、任务与控制 token 校验、输入失败保留文字、新录音不被旧结果取消、编辑输入后端选择与取消时释放按键，以及接收电脑偏好与重启恢复检查。
