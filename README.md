# next-ui-AItranslate

按 **MENU+X**，把游戏画面上的文字翻译成中文。

**仅适用于运行 NextUI 的 TrimUI Brick。** 当前编译版对应 NextUI v6.14.0（20260719），平台目录为 `tg5040`。

### 1. 下载并复制

前往 **[Releases 下载编译好的文件](https://github.com/Drafffffff/next-ui-AItranslate/releases/latest)**，下载 `minarch.elf`。

关机后取出 SD 卡，先备份原文件，再把下载的文件复制到 SD 卡的以下位置并覆盖：

```text
.system/tg5040/bin/minarch.elf
```

如果看不到 `.system` 文件夹，请开启“显示隐藏文件”。

### 2. 填入 API Key

在 SD 卡上创建或编辑 `.userdata/shared/ai-translate.txt`，写入以下内容（默认使用 DeepSeek）：

```ini
aiEnable=1
aiProvider=1
aiDeepseekKey=填入你的DeepSeekAPIKey
```

### 3. 开始翻译

安全弹出 SD 卡，插回掌机并开机。连接 Wi-Fi，进入游戏，**按住 MENU，再按 X** 即可翻译。

### 真机展示

TrimUI Brick 实拍，已裁切屏幕区域并统一为 4:3 比例。点击图片可查看大图。

<table>
  <tr>
    <td width="50%" align="center"><a href="docs/images/brick-7856.jpg"><img src="docs/images/brick-7856.jpg" alt="TrimUI Brick 标题翻译：46亿年物语" width="100%"></a><br><sub>标题翻译</sub></td>
    <td width="50%" align="center"><a href="docs/images/brick-7857.jpg"><img src="docs/images/brick-7857.jpg" alt="TrimUI Brick 人物对话翻译：初次见面" width="100%"></a><br><sub>人物对话</sub></td>
  </tr>
  <tr>
    <td width="50%" align="center"><a href="docs/images/brick-7855.jpg"><img src="docs/images/brick-7855.jpg" alt="TrimUI Brick 剧情文字中文翻译" width="100%"></a><br><sub>剧情文本</sub></td>
    <td width="50%" align="center"><a href="docs/images/brick-7853.jpg"><img src="docs/images/brick-7853.jpg" alt="TrimUI Brick 叙事文字翻译实拍" width="100%"></a><br><sub>叙事阅读效果</sub></td>
  </tr>
</table>

**菜单与设置**

<table>
  <tr>
    <td width="33%" align="center"><a href="docs/images/brick-7858.jpg"><img src="docs/images/brick-7858.jpg" alt="游戏菜单中的 AI Translate 入口" width="100%"></a><br><sub>菜单入口</sub></td>
    <td width="33%" align="center"><a href="docs/images/brick-7859.jpg"><img src="docs/images/brick-7859.jpg" alt="AI 翻译设置：DeepSeek、译文停留、上下文" width="100%"></a><br><sub>翻译设置</sub></td>
    <td width="33%" align="center"><a href="docs/images/brick-7860.jpg"><img src="docs/images/brick-7860.jpg" alt="AI Translate 快捷键设置为 MENU+X" width="100%"></a><br><sub>MENU+X 快捷键</sub></td>
  </tr>
</table>

---

需要从源码构建？查看 **[完整编译教程](docs/BUILD.md)**。

基于 [NextUI](https://github.com/LoveRetro/NextUI)。[详细说明](README-AI翻译.md) · [许可证](LICENSE)
