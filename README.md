# Enthalpy

**RA3 风格的列队指令插件，用于《尤里的复仇》1.001（Syringe / SyringeEx 注入）。**

按住鼠标中键在目的地拖出一个"阵型预览"，松开后选中单位按预览的位置与朝向就位——不需要改游戏、不依赖 Phobos 的内部结构，只要一个 `Enthalpy.dll` 与一个 `Enthalpy.ini`。

```
选中单位 → 在目的地按住鼠标中键 → 拖动决定朝向与阵型 → 松开中键 → 单位就位并转向
```

## 功能

| 功能 | 说明 |
| --- | --- |
| 中键手势 | 左键选中单位后，在目的地按住鼠标中键即进入列队模式；移动光标调整朝向与间距，松开中键下达指令 |
| 格位分配 | 按 `CellClass` 精确占格：一个载具一格，步兵一格最多 3 人，位置由引擎的 `FindInfantrySubposition` 决定 |
| 非法格处理 | 不可用/已占用的格子自动吸附到最近合法格，并以红色提示 |
| 朝向 | 由拖动方向决定，到位后原地转向该朝向并保持 |
| 半透明剪影 | 在目标格处绘制该单位**当前朝向的真实图像**（体素走 `DrawAsVXL`，SHP 走像素剪影），透明度、颜色、阴影均可配置 |
| 地形适配 | 剪影使用目的地格自身的三维坐标（含层高与桥面），斜坡上按坡面绘制 |
| 飞行单位 | 剪影悬浮在巡航高度，并从剪影向地面画一条目标线，地面画椭圆投影 |
| 自检 | 启动时校验 `gamemd.exe` 的四个钩子站点，环境不符时写日志并自行停用，而不是崩溃 |

## 构建

需要 **Visual Studio 2022（v143 工具集）+ Windows SDK**，32 位配置。

```
git clone <repo>
cd Enthalpy
```

图形界面：打开 `Enthalpy.sln`，选择 `Release | Win32` 生成。

命令行：

```
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ^
  Enthalpy.vcxproj /p:Configuration=Release /p:Platform=Win32 /m /v:m
```

产物：`build\bin\Release\Enthalpy.dll`（约 200 KB，静态 CRT，不依赖 VC 运行库）。

`Debug` 配置同样可用，产物更大并带 PDB/MAP，便于调试。

## 安装

1. 把 `Enthalpy.dll` 和 `Enthalpy.ini` 放进游戏目录（与 `gamemd.exe` 同级）。
2. 让注入器加载它，方式取决于你用的加载器：

| 加载器 | 需要做什么 |
| --- | --- |
| SyringeEx（Phobos 0.4+ 自带） | 放入游戏目录即可，它会扫描 `*.dll` |
| 经典 Syringe 0.7.x | 命令行加 `-i=Enthalpy.dll` |
| CnCNet 客户端 | 在 `Resources\ClientDefinitions.ini` 的 `ExtraCommandLineParams` 里加 `-i=Enthalpy.dll`，然后**重启客户端** |

3. 需要排查时把 `Enthalpy.ini` 里 `Enthalpydebug=yes`，日志会写到游戏目录的 `Enthalpy.log`（目录不可写时回退到 `%LOCALAPPDATA%\Enthalpy\Enthalpy.log`）。

更多排障细节见 [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)。

## 配置

配置查找顺序（取**第一个确实含 `[Enthalpy]` 段**的文件）：`Enthalpy.ini` → `RA2MD.INI` → `RA2MO.ini` → `RA2.INI`。
推荐只用 `Enthalpy.ini`，与 DLL 放在一起。完整键表、生效时机与常用预设见 [docs/CONFIGURATION.md](docs/CONFIGURATION.md)。

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `FormationSelectSaved` | `no` | 没有选中单位时按下中键，是否沿用上一次的选择 |
| `TargetBlock` | `no` | 是否在剪影脚下绘制带色方格 |
| `GhostOpacity` | `2` | 剪影透明度，`0` 全透明 ~ `4` 全不透明 |
| `GhostShadow` | `yes` | 剪影是否包含阴影像素 |
| `GhostColorValid` | `0,251,0` | 合法格位剪影色（`R,G,B`） |
| `GhostColorInvalid` | `255,0,0` | 非法/吸附格位剪影色 |
| `GhostColorValidEdge` | `0,251,0` | 合法格位的描边色 |
| `GhostColorInvalidEdge` | `255,170,0` | 非法/吸附格位的描边色 |
| `TargetLine` | `no` | 是否绘制“按下点 → 光标”的绿色目标线 |
| `Enthalpydebug` | `no` | 是否写日志文件 |

每个键的含义、取值范围与预设组合见 [docs/CONFIGURATION.md](docs/CONFIGURATION.md)；用户可见的改动记录见 [docs/CHANGELOG.md](docs/CHANGELOG.md)。

## 仓库结构

```
Enthalpy/
├─ Enthalpy.sln / Enthalpy.vcxproj   工程（Debug/Release，Win32）
├─ src/
│  ├─ Formation.cpp/.h   手势、格位布局、下令与监督、剪影捕获与绘制
│  ├─ ShpGhost.cpp/.h    SHP 像素级剪影
│  ├─ Hooks.cpp          Syringe 钩子入口（4 处）
│  ├─ Settings.cpp/.h    配置解析
│  ├─ Compat.cpp/.h      PE 自检与钩子站点校验
│  ├─ Log.cpp/.h         日志（含只读目录回退）
│  └─ dllmain.cpp        模块入口
├─ YRpp/                 随仓库附带的 YRpp 头文件（见 NOTICE.md）
├─ docs/CONFIGURATION.md 配置参考（键表、预设、排障）
├─ docs/COMPATIBILITY.md 兼容性与排障
├─ docs/CHANGELOG.md     更新记录
└─ Enthalpy.ini          配置模板
```

## 已知限制

- 只支持《尤里的复仇》1.001 的 `gamemd.exe`；MOD（如心灵终结）若可执行文件被改动，自检会挡下并在日志里给出实际字节。
- 剪影绘制依赖 `0x6D9134` 这个渲染点，若被其它插件占用，日志会给出告警，此时不会崩溃但看不到剪影。
- 飞行单位的剪影高度取自 `RulesClass` 的巡航高度，编队时不做爬升/俯冲动画。

## 许可

GPL-3.0-or-later，全文见 [LICENSE.md](LICENSE.md)。仓库内附带的 YRpp 版权与出处见 [NOTICE.md](NOTICE.md)。
