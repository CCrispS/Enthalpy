# 发布与协作流程

本项目所有改动都通过 **Pull Request** 合入，`main` 只接受经过 PR 的提交；每个 Release 同时提供**源码**与**编译产物**。

## 1. 日常改动：分支 → PR → 合并

```powershell
# 1) 从最新的 main 拉分支（命名约定见下）
git checkout main
git pull
git checkout -b feat/middle-button-gesture

# 2) 改代码 / 写文档 / 更新 CHANGELOG
git add -A
git commit -m "Describe the change in one imperative line"

# 3) 推到远程
git push -u origin feat/middle-button-gesture

# 4) 在 GitHub 上开 PR（或用 gh pr create）
#    https://github.com/CCrispS/Enthalpy/compare/main...feat/middle-button-gesture?expand=1
```

分支命名：

| 前缀 | 用途 | 例子 |
| --- | --- | --- |
| `feat/` | 新功能 | `feat/air-unit-ghost` |
| `fix/` | 缺陷修复 | `fix/slope-stutter` |
| `docs/` | 文档 | `docs/release-and-pr-workflow` |
| `refactor/` | 重构（不改行为） | `refactor/capture-pipeline` |
| `chore/` | 构建、依赖、杂务 | `chore/vsconfig` |

PR 要求（模板里也有勾选项）：

- [ ] `Release` 与 `Debug` 两种配置都能编译，0 error；
- [ ] 新功能/行为变化已写入 `docs\CHANGELOG.md`（日期 + 小节标题）；
- [ ] 新增或改动的 ini 键已写入 `docs\CONFIGURATION.md` 与 `README.md` 的配置表；
- [ ] 至少实机验证一次（说明地图、单位组合、验证点）；
- [ ] 不提交 `build/`、`shots/`、`*.log`、`*.pdb`、`*.user`。

## 2. 发布：打 tag，同时附源码与产物

GitHub 会为每个 Release 自动生成 **Source code (zip / tar.gz)**，所以“源码”这一项不用手动准备；我们只需再附上编译产物。

```powershell
# 1) 确认 main 上是准备发布的提交
git checkout main
git pull
git log --oneline -1

# 2) 打带注释的 tag（语义化版本，见下）
git tag -a v1.1.0 -m "Enthalpy v1.1.0"
git push origin v1.1.0

# 3) 构建 Release 产物
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
    'Enthalpy.vcxproj' /p:Configuration=Release /p:Platform=Win32 /m /v:m /nologo
# 产物：build\bin\Release\Enthalpy.dll

# 4) 打包（DLL + 默认 ini + LICENSE + README.txt）
#    参见本仓库做法：Enthalpy-v<版本>.zip

# 5) 创建 Release，附 zip
#    https://github.com/CCrispS/Enthalpy/releases/new
#    - Choose a tag: v1.1.0
#    - Title: Enthalpy v1.1.0
#    - Body: docs\releases\v<版本>.md 的内容
#    - Attach binaries: Enthalpy-v<版本>.zip
#    发布后确认页面上同时有 “Source code (zip)” 与我们的 zip。
```

Release 页面必须包含：

| 项目 | 来源 |
| --- | --- |
| 源码 zip / tar.gz | GitHub 自动生成（tag 对应的源码树） |
| `Enthalpy-v<版本>.zip` | 手工附上：`Enthalpy.dll`、`Enthalpy.ini`、`LICENSE.md`、`README.txt` |
| 说明正文 | `docs\releases\v<版本>.md`（中英双语：功能、安装、要求、已知限制、许可与源码地址） |
| 校验值 | 正文末尾给出 `SHA256(Enthalpy-v<版本>.zip)` |

`README.txt` 里固定写清：安装三步、依赖（YR 1.001 / Syringe）、以及
`Enthalpy is licensed under GPL-3.0-or-later. Source code: https://github.com/CCrispS/Enthalpy`。

## 3. 版本号

`MAJOR.MINOR.PATCH`：

- **MAJOR**：不兼容的配置键改名/删除，或需要重新适配游戏版本的改动；
- **MINOR**：新增功能、新增 ini 键（向下兼容）；
- **PATCH**：纯修复、文档、性能。

v1.0.0 为首次公开发布。

## 4. 谁来点“Publish”

- 有写权限的人可以直接在网页创建 Release；
- 若用 `gh` CLI：`gh release create v1.1.0 Enthalpy-v1.1.0.zip --title "Enthalpy v1.1.0" --notes-file docs/releases/v1.1.0.md`；
- 自动化（可选）：在 `.github\workflows\release.yml` 里用 `windows-latest` + MSBuild 构建并 `softprops/action-gh-release` 上传，tag 一推即可产出，无需人工。若要启用，告诉我，我会把 workflow 一并写好走 PR。