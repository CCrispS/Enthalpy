# 回归清单

每次改动后按顺序过一遍；任何一项不符就先修再继续。

## 1. 构建
- [ ] `Enthalpy.vcxproj` 的 Debug 与 Release 均无 error
- [ ] `build\bin\Release\Enthalpy.dll` 生成

## 2. 二进制自检（tools\selfcheck.ps1）
- [ ] `.syhks00` 里有 4 条 hook：`6BD68D` `6D3D10` `6D9134` `692423`
- [ ] 导出 4 个符号：`Enthalpy_Init` `Enthalpy_TacticalRender_Before` `Enthalpy_TacticalRender_AfterWorld` `Enthalpy_SuppressClick`

## 3. 进游戏后（Enthalpydebug=yes）
- [ ] `==== Enthalpy loaded` / `hook: WinMain registrations reached - Enthalpy is armed`
- [ ] `settings: GhostOpacity=N ini=...\Enthalpy.ini` 与 ini 一致
- [ ] 中键手势：`formation: gesture start, N units`，剪影出现且真实单位不动
- [ ] 松开：`formation: commit N/N orders`，`formation: facing applied` 每单位一次
- [ ] 到位后不再移动（Stop 钉住），朝向与剪影一致
- [ ] 左键点地面可正常指挥（`keep: released ...`）

## 4. 设置项逐条（改 ini 后下一次手势生效，无需重启）
- [ ] `GhostOpacity` 0/1/2/3/4：描边不变，本体由不可见到实心
- [ ] `GhostColorValid` / `GhostColorInvalid`：本体色相
- [ ] `GhostColorValidEdge` / `GhostColorInvalidEdge`：描边色相且始终实心
- [ ] `GhostShadow`：影子行显隐
- [ ] `TargetLine`：锚点→光标连线显隐
- [ ] `TargetBlock`：剪影脚下格子显隐
- [ ] `FormationSelectSaved`：空选择时是否沿用上次阵型
- [ ] `Enthalpydebug`：no 时不生成日志

## 5. 兼容性
- [ ] Ares 环境、Phobos 共存时 hook 不冲突
- [ ] 缺少 ini 时用编译期默认值
