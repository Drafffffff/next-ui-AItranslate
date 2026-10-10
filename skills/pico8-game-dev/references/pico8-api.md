# PICO-8 / fake-08 开发参考

核查日期：2026-10-02。面向小型 2D/3D 游戏的原创整理；建议子集不等于已逐项完成 Brick 真机验证。

## 平台规则

画面是 128×128，坐标原点在左上，x 向右、y 向下。颜色是 `0..15`，绘制精灵时颜色 0 默认透明。世界坐标与屏幕坐标不要混用；第一版优先固定镜头。

每个精灵 8×8，整张 sheet 128×128。内置素材见 `assets/catalog.json`，ID `1..12` 固定，自定义用 `32..63`。精灵 `128..255` 与地图第 `32..63` 行共用存储；初版地图最多使用行 `0..31`。内置包没有设置地图或精灵碰撞标记；音效与BGM见 `modules/music.md`。

P8 Lua 使用有符号 16.16 定点数。避免巨大整数、毫秒时间戳和无限累加计时；其整数部分约在 -32768..32767，乘法的中间结果也会溢出。小型游戏用帧计数，并在合理范围内重置。不要把数字当无界 IEEE 浮点或任意精度整数。

PICO-8 的代码限制以 token 等口径描述，不能用模型 token、源码字节数或 JSON 长度替代。工坊不设固定源码字节上限；运行器兼容性、模型上下文和宿主资源预算是分别处理的约束。

## 生命周期与状态

`_init()` 建立初始状态；`_update()` 以 30 Hz 推进输入、计时、生成与碰撞；`_draw()` 只绘制。重新开始调用本轮重置函数，不清掉需要保留的本次运行最高分。初版不使用 `_update60` 或 `flip()` 自管循环。完整示例按玩法选择；不要把只展示入口的移动片段当作完整游戏。

每帧最多处理有限数量的对象。不要使用永久循环，不在绘制阶段进行大量随机生成、表删除或重新初始化。典型小规模预算：敌人不超过 16、子弹不超过 32、粒子不超过 32；这是建议起点，不是测得的性能保证。

## 按键

| 编号 | PICO-8 输入 |
| --- | --- |
| 0 | 左 |
| 1 | 右 |
| 2 | 上 |
| 3 | 下 |
| 4 | O 动作键 |
| 5 | X 动作键 |

- `btn(i)`：当前是否按住，适合移动、持续射击。
- `btnp(i)`：按下与按住后的默认重复触发。菜单方向可重复，但确认/重启/旋转/硬降需要严格单次时，保存前一帧 `btn(i)` 并比较上升沿；每帧更新前态，状态转换不把仍按住的键视为新按下。
- 游戏提示优先写 `o` / `x`。宿主验证 Brick 映射后可提供对应的提示；不能把工坊的 X 快捷键和 PICO-8 的 X 动作键混为同一层输入。
- 游戏代码不能接管 MENU、音量、亮度和系统退出。

## 图形 API 常用子集

| API | 使用约定 |
| --- | --- |
| `cls(c)` | 清屏，通常在 `_draw()` 开头 |
| `pset(x,y,c)` / `pget(x,y)` | 单像素操作；避免大范围逐像素重绘 |
| `rect(x0,y0,x1,y1,c)` / `rectfill(...)` | 参数是两个角点，终点包含在矩形里；8 像素宽用 `x..x+7` |
| `circ(x,y,r,c)` / `circfill(...)` | 圆框/填充圆；半径保持小范围 |
| `line(x0,y0,x1,y1,c)` | 绘线 |
| `spr(n,x,y,w,h,flip_x,flip_y)` | `w/h` 是精灵格数，不是像素；通常省略以画一个 8×8 精灵 |
| `sspr(sx,sy,sw,sh,dx,dy,dw,dh,flip_x,flip_y)` | 源/目标宽高是像素；初版尽量用 `spr` |
| `print(text,x,y,c)` | 默认字库与 ASCII 提示；不要输出中文或依赖自定义字体 |
| `camera(x,y)` | 需要滚动世界时才用；画 HUD 前用 `camera()` 恢复 |
| `pal(...)` / `palt(...)` | 会改变绘制状态；局部效果结束后显式恢复，避免其他素材变色或透明 |

画面中计分/生命与主要角色留出可读空间。游戏的视觉采用内置精灵和少量形状即可，不要求所有游戏都画成同一个场景。

## 数学、随机与表

- `flr(x)` 向下取整，`abs/min/max/mid` 用于距离、比较和边界。`mid(lo,v,hi)` 可夹取坐标。
- 随机整数 `0..n-1`：`flr(rnd(n))`。整数闭区间 `lo..hi`：`lo+flr(rnd(hi-lo+1))`。
- `rnd(array)` 取随机元素；空表或候选空间不存在时先处理，不能反复随机直到无限等待。
- `sin/cos` 使用圈数：`1` 是一整圈，`sin(0.25)=-1`。不要传入弧度或角度。
- 角度运动可写 `x=cx+cos(a)*r; y=cy+sin(a)*r`；按实际所需旋转方向改变 `a`。
- Lua 数组常从 `1` 开始；精灵、按键、地图与音效索引从 `0` 开始。
- `all(t)` 用于 `for item in all(items) do ... end`；需要索引或逆序删除时使用普通数字 `for`。
- 用 `add(t,v)`、`del(t,value)` 管理紧凑数组；`del` 按值删除。需要按索引可用 `deli(t,i)`。
- 锁定 fake-08 的卡带沙箱没有普通 Lua `table/math/string` 命名空间。`table.remove(bag)` 会在运行时访问 nil；取出末项写 `deli(bag,#bag)`，追加用 `add`，排序写有界循环。`math.floor` 改用 `flr`，`string.sub` 改用 `sub`。自己定义的同名对象另当别论，但优先避免这种混淆。
- 避免数组中间 `nil` 空洞。删除多个对象时使用逆序 `for i=#items,1,-1 do ... end`，或设计明确的存活对象重建。
- 优先 `if ... then ... end`、普通赋值、明确的函数定义。合法的 P8 简写如 `+=` 不是必需；清晰基础语法便于审阅和开发端检查。
- P8 扩展语法、API 与数字语义和普通 Lua 不完全相同。普通 Lua 解析通过不能证明 fake-08 兼容，解析失败也不能一概说明 P8 代码无效。

## 碰撞与地图

矩形碰撞可以使用以下规则（坐标为左上角，宽高以像素表示）：

```lua
function overlap(a,b)
 return a.x<b.x+b.w and b.x<a.x+a.w
  and a.y<b.y+b.h and b.y<a.y+a.h
end
```

角色进入重叠区域后要有明确结果：扣生命、移除对象或进入失败状态。扣生命可以加入短暂的无敌帧，避免一帧后再次受伤。收集对象后移除，再生成下一项；生成时限定候选区域和尝试次数。

`map(cell_x,cell_y,screen_x,screen_y,cell_w,cell_h)` 中前两项和宽高是地图格，显示坐标是像素。`mget/mset` 读取/写入精灵编号；`fget(sprite,flag)` 读取标记。当前素材没有设置标记，不得假设 wall 自带阻挡属性。简单迷宫可在 Lua 中存二维表并用 `spr` 绘制，避免依赖未提供的地图章节。

## 音频与卡带数据

`sfx(n)` 只在有效音效 ID 存在时调用，`music(n)` 依赖有效音乐模式。内置6种交互音效及2首循环BGM；自定义音符和pattern通过 `sfx_overrides/music_overrides` 合并，详见按需加载的 `music` 模块。四声道须为交互声留位置；只在场景转换启动音乐，不在绘制中或每帧重启。

文本卡带头包括 `pico-8 cartridge // http://www.pico-8.com` 与版本行。`__lua__` 保存代码，`__gfx__` 保存整张精灵 sheet；完整图像是 128 行，每行 128 个颜色 nibble。精灵 n 起点为 `((n%16)*8, flr(n/16)*8)`，按整张图逐行编码，不能直接把多个 8×8 方块逐块串接。

## 工坊初版限制与检查边界

禁止生成代码依赖 `#include`、`flip`、`load/reload/cstore`、`cartdata/dget/dset`、`peek/poke` 系列、`memcpy/memset`、`serial/extcmd`、系统库 `io/os/package/debug`，以及 `require/dofile/loadfile/loadstring` 等动态执行。这些限制服务于单卡带、限定素材和可恢复的掌机产品，不是对官方 API 的完整描述。

组装器检查 JSON 字段、项目/版本、生命周期名称、初版限制中可识别的标识符、像素编码和章节边界；它不解析完整 Lua，不执行代码，不判定性能、死循环、碰撞正确性或玩法。模型输出仍需用户真机试玩。

## 来源与核查范围

- [PICO-8 官方手册](https://www.lexaloffle.com/dl/docs/pico-8_manual.html)：生命周期、按键、数学、表、图形、地图与语言限制。
- [fake-08 README](https://github.com/jtothebell/fake-08/blob/814991a2571ad3970e386cef48f3b148aa1c27b9/README.md)：文本卡带支持、z8lua 与兼容边界。
- [cart.cpp](https://github.com/jtothebell/fake-08/blob/814991a2571ad3970e386cef48f3b148aa1c27b9/source/cart.cpp)、[PicoRam.h](https://github.com/jtothebell/fake-08/blob/814991a2571ad3970e386cef48f3b148aa1c27b9/source/PicoRam.h)、[stringToDataHelpers.cpp](https://github.com/jtothebell/fake-08/blob/814991a2571ad3970e386cef48f3b148aa1c27b9/source/stringToDataHelpers.cpp)：gfx/sfx 格式与数据布局。

fake-08 文档依据固定在上述提交；本仓库实际构建版本及卡上的安装状态须另行确认。宿主升级运行器时重新检查兼容范围。
