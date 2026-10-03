# CDDA I 手柄与 UI 基础分支

这是从现有 Brick 移植中提取的 UI 层，作为 Steam Deck 后续适配的起点。尚未生成 Steam Deck 程序，也没有 Steam Deck 实机验收。

保留上下文按键、动作面板、两行提示、屏幕键盘、制作材料滚动、地图光标模式、快捷键装饰隐藏、中英文等宽网格、菜单排版、侧栏与中文裁切修正。

`python3 ports/cdda-brick-i/apply-port.py /path/to/pristine-CDDA-I` 仅应用 UI 补丁。源码版本与校验和在 `pins.env`，不得对原来的内存实验构建树执行此命令：它不会卸载已有补丁，必须使用干净上游源码。

本分支不包含地图紧凑编码、按需大地图、监测与压力回收、冷快照、精确物品 RLE、压缩限额、启动审计/预解析包，也没有 Brick 原来的启动、打包及系统休眠脚本。普通存档与游戏数据结构沿用原版。

适配器仍使用现有 Brick 按键编号与 `CDDA_BRICK=1` 开关；手柄标准化、Steam Input、字体/提示尺寸、摇杆/扳机和 Linux x86-64 构建属于下一阶段。已移除 Brick 电源设备读取与休眠命令。`sidebar-brick-minimal.json` 是保留的布局资源，资源安装由后续平台打包负责。

历史 `input-report.md` 和 `controller-ui-report.md` 说明原 Brick 实验的验证范围，不代表提取版本或 Steam Deck 已通过这些测试。提取验证范围见仓库 `docs/cdda-change-split/README.txt`。

完整原始 G/I 代码、混合脚本、内存实验和对应检查已保存在 `codex/cdda-i-brick-memory-experiments`。
