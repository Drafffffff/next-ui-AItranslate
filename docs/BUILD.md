# 完整编译教程

本教程从源码编译 AI 翻译功能，得到可复制到 SD 卡的 `minarch.elf`；也包含支持中文拼音排序、拼音跳转的 `nextui.elf` 编译方法。

**仅适用于运行 NextUI 的 TrimUI Brick。** 本教程目标为 NextUI v6.14.0（20260719），对应 `tg5040` 平台目录。不要将这里的产物安装到其他机型或系统。

## 1. 准备环境

需要：

- Git：用于获取项目源码。也可以下载本仓库的 ZIP。
- Docker：安装后启动 Docker 服务，确保终端中 `docker info` 能正常运行。
- 网络：首次构建需要拉取工具链镜像，并从 GitHub 下载依赖。
- 一个支持下面 Bash 命令的终端。macOS / Linux 可以直接使用；Windows 请在 WSL 的 Linux 终端里执行，并确保该环境可以使用 Docker。

编译器、Make、CMake、SDL 和交叉编译库都由工具链容器提供，不需要在电脑上单独安装。编译不需要 API Key，也不需要连接掌机。

工具链是 `linux/arm64` 镜像。其他架构的电脑需要 Docker 能运行该架构的容器。本教程的实际构建验证环境为 Apple Silicon macOS；其他主机环境未实测。

## 2. 获取本项目源码

在**本仓库**页面点击 **Code → Download ZIP**，解压并在终端进入项目根目录。这里应当能看到 `README.md`、`makefile` 和 `workspace/`。

也可以使用 Git 下载：

```bash
git clone https://github.com/Drafffffff/next-ui-AItranslate.git
cd next-ui-AItranslate
```

请下载这个含 AI 翻译功能的仓库源码。上游 NextUI 仓库不包含本项目的改动，也不需要另外应用 `ai-patch/` 中的补丁。

生成构建版本标识，Git 和 ZIP 下载方式都适用：

```bash
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git rev-parse --short HEAD > workspace/hash.txt
else
  printf 'source-zip\n' > workspace/hash.txt
fi
```

## 3. 下载工具链

在同一个终端中执行。这里固定镜像摘要，使用本教程验证过的工具链：

```bash
NEXTUI_TOOLCHAIN='ghcr.io/loveretro/tg5040-toolchain@sha256:f131c6af64029a8723d0ce8d3c2682642f5f091b04714f6beedda9bec18477ab'
docker pull --platform linux/arm64 "$NEXTUI_TOOLCHAIN"
```

`NEXTUI_TOOLCHAIN` 是当前终端的变量；重新打开终端后，需要重新执行赋值这一行。项目路径可以包含空格，保持下面挂载参数的双引号即可。

## 4. 编译 AI 翻译程序

在项目根目录执行：

```bash
docker run --rm --platform linux/arm64 \
  -v "$PWD/workspace:/root/workspace" \
  "$NEXTUI_TOOLCHAIN" \
  /bin/bash -lc 'cd /root/workspace/all/minarch && make PLATFORM=tg5040'
```

首次构建会自动获取 `libretro-common`、`rcheevos`、`libchdr`，并构建所需的 `libmsettings`。下载速度会影响首次编译耗时；看到较多编译输出是正常的。

**不要在这条命令里加 `make -j`。** 当前 Makefile 中依赖的下载、生成和安装需要按顺序完成。

成功后，产物位于电脑上的：

```text
workspace/all/minarch/build/tg5040/minarch.elf
```

输出目录还会包含共享库。这是构建过程的正常产物；在本教程指定的已有 NextUI 系统上安装 AI 翻译更新，使用 `minarch.elf` 即可。

检查文件类型和校验值：

```bash
file workspace/all/minarch/build/tg5040/minarch.elf
```

应当显示 ELF 64 位、ARM aarch64。macOS 计算校验值：

```bash
shasum -a 256 workspace/all/minarch/build/tg5040/minarch.elf
```

Linux / WSL 可以使用：

```bash
sha256sum workspace/all/minarch/build/tg5040/minarch.elf
```

## 5. 可选：编译支持拼音排序的主界面

如果还需要目录按中文拼音排序、按拼音跳转，再执行：

```bash
docker run --rm --platform linux/arm64 \
  -v "$PWD/workspace:/root/workspace" \
  "$NEXTUI_TOOLCHAIN" \
  /bin/bash -lc 'cd /root/workspace/all/nextui && make PLATFORM=tg5040'
```

产物位于：

```text
workspace/all/nextui/build/tg5040/nextui.elf
```

拼音数据已经包含在源码中，正常编译不需要安装 Python 或重新生成数据。

## 6. 安装到 NextUI

关机、取出 SD 卡，先把卡上原来的文件备份到电脑，再按下面对应关系复制并覆盖：

| 电脑上的编译产物 | SD 卡上的目标位置 |
| --- | --- |
| `workspace/all/minarch/build/tg5040/minarch.elf` | `.system/tg5040/bin/minarch.elf` |
| `workspace/all/nextui/build/tg5040/nextui.elf`（可选） | `.system/tg5040/bin/nextui.elf` |

如果看不到 `.system`，请开启“显示隐藏文件”。如需恢复，关机后将备份的原文件复制回对应位置。

在 SD 卡上创建或编辑 `.userdata/shared/ai-keys.txt`：

```ini
DEEPSEEK_API_KEY=填入你的DeepSeekAPIKey
DASHSCOPE_API_KEY=
```

再在 `.userdata/shared/ai-translate.txt` 中启用翻译：

```ini
aiEnable=1
aiProvider=1
```

安全弹出 SD 卡，插回掌机并开机。连接 Wi-Fi、进入游戏，按住 **MENU** 再按 **X** 翻译。新配置默认绑定 MENU+X；已有自定义快捷键配置会保留，可在游戏菜单中重新绑定。

如需同时保留百炼 Key，在公共文件中填入 `DASHSCOPE_API_KEY=你的百炼APIKey`。`aiProvider=1` 使用 DeepSeek，`aiProvider=0` 使用百炼；两个 Key 可以并存，实际请求使用当前选择的服务。

## 7. 修改代码后重新编译

修改 AI 翻译代码后重复第 4 步；修改主界面后重复第 5 步。关闭容器不会删除源码目录里的依赖和产物，不过容器内部安装的依赖会随 `--rm` 清理，下一次可能重新执行安装或依赖构建。

主要代码位置：

| 内容 | 源码 |
| --- | --- |
| 翻译提示词、服务请求、上下文、文字排版 | `workspace/all/minarch/ma_ai.c` |
| 默认快捷键 | `workspace/all/minarch/ma_config.c` |
| 游戏画面采集 | `workspace/all/minarch/ma_video.c`、`workspace/all/common/generic_video.c` |
| 主界面拼音排序与跳转 | `workspace/all/nextui/nextui.c`、`workspace/all/nextui/pinyin.h` |

发布编译文件时，记录本项目的提交号、工具链摘要和产物 SHA-256。`rcheevos` 在 Makefile 中固定了版本，但 `libretro-common` 和 `libchdr` 首次下载没有固定提交；因此仅固定工具链不能保证不同日期的构建完全相同。可以用以下命令记录依赖版本：

```bash
git -C workspace/all/minarch/libretro-common rev-parse HEAD
git -C workspace/all/minarch/rcheevos/src rev-parse HEAD
git -C workspace/all/minarch/libchdr rev-parse HEAD
```

## 8. 常见问题

| 问题 | 处理方法 |
| --- | --- |
| `Cannot connect to the Docker daemon` | 启动 Docker，先确认 `docker info` 成功。 |
| `exec format error` | 确认使用 `--platform linux/arm64`，且 Docker 主机能运行 ARM64 容器。 |
| `missing CROSS_COMPILE` 或找不到 SDL | 在上述工具链容器内编译，不要直接在主机目录执行 `make`。 |
| `hash.txt: No such file` | 在项目根目录重新执行第 2 步的版本标识命令。 |
| GitHub 下载失败、TLS 或超时错误 | 检查容器的网络和代理配置，网络恢复后重试；不要关闭证书验证。 |
| 依赖下载中断后，重试仍然报目录或文件缺失 | 保留自己的源码修改，仅删除对应的生成依赖目录：`workspace/all/minarch/libretro-common`、`rcheevos` 或 `libchdr`，然后重新编译。 |
| 复制后不能启动 | 核对 NextUI 版本和 tg5040 平台，确认文件复制完整；先恢复备份文件。 |
| 能进入游戏但不能翻译 | 编译不包含 Key；检查卡上配置、Wi-Fi、服务额度和游戏快捷键。 |

## 关于整套系统构建

以上步骤完整覆盖本项目两个可安装 ELF 的源码构建。项目根目录的 `make` 是上游整套系统构建入口，还涉及其他程序、系统文件、打包和额外下载；不是生成这两个 ELF 的必要步骤，并且 `make setup` 会删除根目录的 `build/`。

如果要开发整个 NextUI 系统，请另外参考保留的 [上游 README](NextUI-README.md)，核对根目录 Makefile 后操作。本文没有将未验证的整套系统构建作为安装 AI 翻译的前置条件。

[返回极简安装教程](../README.md)
