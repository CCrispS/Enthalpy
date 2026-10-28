# 参与开发

## 环境

- Windows 10/11，Visual Studio 2026（Workload：使用 C++ 的桌面开发，含 x86 工具集）
- Windows SDK 10.0.26100.0
- 不需要 CMake、vcpkg 或任何第三方包；`YRpp/` 已随仓库附带

## 构建

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  .\Enthalpy.vcxproj /p:Configuration=Release /p:Platform=Win32 /m /v:m /nologo
```

产物：`build\bin\Release\Enthalpy.dll`。Debug 同理（`/p:Configuration=Debug`）。

## 本地测试

1. 把 `Enthalpy.dll` 与 `Enthalpy.ini` 复制到 YR 游戏目录；
2. 用 SyringeEx 启动（命令行带 `-i=Enthalpy.dll`，或在客户端 `Resources\ClientDefinitions.ini` 的 `ExtraCommandLineParams` 里加）；
3. 把 `Enthalpy.ini` 里的 `Enthalpydebug` 改成 `yes`，日志会写在 DLL 同目录的 `Enthalpy.log`；
4. 按 [`docs/REGRESSION.md`](docs/REGRESSION.md) 过一遍回归清单。

二进制自检（确认 hook 段与 4 个导出都在）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\selfcheck.ps1
```

## 改动约定

- 一个新功能 = 代码 + `docs/CONFIGURATION.md` 里的一行键表 + `docs/CHANGELOG.md` 一条记录 + 一次游戏内验证；
- 注释统一写**中文**，说明“为什么这么做”，不复述代码；
- 不引入新的第三方依赖；需要引擎能力时优先查 `YRpp/`，其次查 Phobos 的用法；
- 游戏内存里的指针、结构体偏移一律走 YRpp 的封装，不要硬编码偏移量；
- 改动引擎对象（`Location`/`Facing`/`Locomotor` 等）后必须在同一帧内还原，并在还原后清掉可能残留的屏幕像素；
- 提交前确认 Debug 与 Release 都能编译通过。

## 报告问题

请附上：

- 日志：`Enthalpy.log`（`Enthalpydebug=yes` 时生成）
- 游戏版本：`gamemd.exe` 的文件大小或哈希
- 复现步骤：地图、选中单位类型、拖拽方向、是否斜坡/水面
- 如果崩溃：`debug\snapshot-*` 目录里的 `except.txt`

## 发布到 GitHub

仓库本身不依赖子模块，直接推送即可：

```powershell
git remote add origin https://github.com/<你的账号>/Enthalpy.git
git branch -M main
git push -u origin main
```

发布前检查：

- [ ] `git status` 干净，`build/`、`shots/`、`*.log` 未被跟踪
- [ ] `LICENSE.md`（GPL-3.0）与 `NOTICE.md`（YRpp 出处、EA 商标声明）都在
- [ ] `README.md` 的构建/安装/配置/已知限制四节与当前行为一致
- [ ] `docs/` 四份文档（CONFIGURATION / COMPATIBILITY / CHANGELOG / REGRESSION）已更新
- [ ] Release 包里有 `Enthalpy.dll` 与 `Enthalpy.ini` 两个文件

## 许可

本项目以 GPL-3.0-or-later 发布；随仓库附带的 `YRpp/` 版权归其原作者，同样为 GPL-3.0。提交 PR 即表示同意以同一许可分发你的改动。
