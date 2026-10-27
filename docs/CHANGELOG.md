# 更新记录

本文件记录用户可见的功能与配置变化。**新增功能时请在此追加一条**，并在 `docs/CONFIGURATION.md` 补上对应键的说明。

## 2026-10-27

### 描边色拆分为"合法 / 非法"两个键

- 新增 `GhostColorValidEdge`（默认 `0,251,0`）与 `GhostColorInvalidEdge`（默认 `255,170,0`）。
- 非法或吸附格位现在是"红色本体 + 橙色描边"，合法格位保持绿色。
- 旧的 `GhostColorEdge` 作为回退键继续可用，老配置不会变色。
- 实测：把队列拖到建筑物上，该格呈红本体 + 橙描边，同排合法格位仍为绿色。

### 新增 `TargetLine` 开关

- 默认 `no`：按下点与光标之间的绿色目标线不再绘制。
- `yes`：恢复原来的目标线，可用于确认朝向。
- 飞行单位的竖直下落线与地面椭圆影子不受影响。
- 实测：`no` 时只剩框选矩形与剪影；`yes` 时绿线恢复。

### 配置参考文档

- 新增 `docs/CONFIGURATION.md`：配置查找顺序、生效时机、写法约定、完整键表、常用预设与排障步骤。
- 新增本文件（`docs/CHANGELOG.md`）。

## 2026-10-27 之前（初始版本）

- 双键手势列阵：左键选中单位后在目的地同时按下左右键，拖动决定朝向与阵型，松开右键后按预览就位。
- 格位精确分配：一辆载具一格，步兵一格最多三人，格内站位由引擎 `FindInfantrySubposition` 给出。
- 非法格位自动吸附到最近合法格，并以红色提示。
- 半透明剪影：在目标格绘制该单位当前朝向的真实图像（体素走 `DrawAsVXL`，SHP 走像素剪影），按地形层高与桥面贴地，斜坡上跟随坡面。
- 飞行单位（jumpjet、战机）：剪影悬浮在巡航高度，并绘制到地面格的指示线与地面椭圆影子。
- 配置：`FormationSelectSaved`、`TargetBlock`、`GhostOpacity`、`GhostShadow`、`GhostColorValid`、`GhostColorInvalid`、`GhostColorEdge`、`Enthalpydebug`。
- 启动自检：校验 `gamemd.exe` 与四个钩子站点，环境不符时写日志并自行停用，而不是崩溃。
- 日志回退：DLL 目录不可写时写到 `%LOCALAPPDATA%\Enthalpy\`。
