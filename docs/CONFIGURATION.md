# Enthalpy 配置参考

所有行为开关都放在 ini 文件的一个 `[Enthalpy]` 段里，不需要改游戏文件，也不需要重新编译。

## 1. 配置文件放在哪

按下面的顺序查找，**取第一个确实含有 `[Enthalpy]` 段**的文件：

1. `Enthalpy.ini`（与 `Enthalpy.dll` 同目录）
2. `RA2MD.INI`
3. `RA2MO.ini`
4. `RA2.INI`

推荐只用 `Enthalpy.ini`：它不会跟 MOD 或客户端自己的设置打架，卸载时删掉即可。
如果你的发行版习惯把所有设置写进 `RA2MD.INI`，把整段 `[Enthalpy]` 复制进去也能用——注意此时 `Enthalpy.ini` 若存在且含该段，会优先于 `RA2MD.INI`。

## 2. 什么时候生效

**每次按下左右键进入列阵模式时重新读取一次**。所以：改完 ini 保存，回到游戏直接再按一次双键即可，不需要重启游戏，更不需要重启客户端。

## 3. 写法约定

| 类型 | 写法 | 说明 |
| --- | --- | --- |
| 布尔 | `yes` / `no`（也接受 `1` / `0`、`true` / `false`） | 大小写不敏感 |
| 整数 | 十进制 | 超出范围会被钳到边界值 |
| 颜色 | `R,G,B`，每个分量 `0`–`255` | 内部转成游戏用的 16 位 RGB565，分量会被量化到 5/6/5 位 |

键名大小写不敏感；同一段里重复的键以第一次出现为准（Windows 配置文件语义）。

## 4. 键表

### 4.1 手势与指令

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `FormationSelectSaved` | `no` | `yes`：没有选中单位时按下双键，沿用上一次的选中；`no`：没有选中就什么都不发生（避免误把上一批部队调走） |

### 4.2 剪影外观

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `GhostOpacity` | `2` | 剪影整体透明度，`0` 完全透明（看不见）→ `4` 完全不透明，中间均匀过渡。影子的透明度按同一档位再减半 |
| `GhostShadow` | `yes` | 剪影是否包含单位自身的阴影像素。`no` 时只剩本体轮廓 |
| `GhostColorValid` | `0,251,0` | 合法格位的剪影本体颜色 |
| `GhostColorInvalid` | `255,0,0` | 格位非法、或被迫吸附到附近合法格时的本体颜色 |
| `GhostColorValidEdge` | `0,251,0` | 合法格位的描边颜色 |
| `GhostColorInvalidEdge` | `255,170,0` | 非法/吸附格位的描边颜色（默认橙色，便于和"红色本体"区分开） |

> 兼容性：旧版的单键 `GhostColorEdge` 仍然能被识别——当 `GhostColorValidEdge` 缺失时会用它作为回退，老配置不会突然变色。

### 4.3 格位与辅助线

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `TargetBlock` | `no` | `yes`：在每格剪影脚下再画一块半透明方格，方便数格子。剪影本身已经落在格位上，默认关闭以免画面过乱 |
| `TargetLine` | `no` | `yes`：在"按下点"与"光标"之间画一条绿色目标线，用来确认朝向。默认关闭 |

> 飞行单位（jumpjet、战机）的"剪影 → 地面格"竖直指示线与地面椭圆影子**不受 `TargetLine` 影响**，它们属于飞行单位的固定表现。

### 4.4 调试

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `Enthalpydebug` | `no` | `yes`：写 `Enthalpy.log`。日志优先写到 DLL 所在目录；目录只读（例如装在 `Program Files`）时自动改写到 `%LOCALAPPDATA%\Enthalpy\`。`no` 时完全不创建日志文件 |

## 5. 常用预设

**默认（推荐给玩家）**

```ini
[Enthalpy]
FormationSelectSaved=no
TargetBlock=no
TargetLine=no
GhostOpacity=2
GhostShadow=yes
GhostColorValid=0,251,0
GhostColorInvalid=255,0,0
GhostColorValidEdge=0,251,0
GhostColorInvalidEdge=255,170,0
Enthalpydebug=no
```

**醒目模式**（屏幕暗、或者只是想看得更清楚）

```ini
GhostOpacity=4
GhostShadow=yes
TargetBlock=yes
TargetLine=yes
```

**极简模式**（只保留轮廓，几乎不挡视线）

```ini
GhostOpacity=1
GhostShadow=no
TargetBlock=no
TargetLine=no
```

**排查问题**

```ini
Enthalpydebug=yes
```

改完再按一次双键验证；若仍无剪影，把 `Enthalpy.log` 发我，日志里会写清楚钩子站点是否通过自检、配置读的是哪个文件、每张剪影的捕获计数。

## 6. 按键行为（无需配置）

- 左键先松开不会取消列阵；只有**右键松开**才下达指令。
- 一个格位最多容纳一辆载具，或三名步兵（三人的站位由引擎的 `FindInfantrySubposition` 决定，不是简单并排）。
- 目标格不可用（水面、建筑、崖壁、被占）时自动吸附到最近合法格，并以红色本体 + 橙色描边提示。
- 木已成舟后单位按格位就位，并在原地转到预览时的朝向。
