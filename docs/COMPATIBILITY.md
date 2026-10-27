# Enthalpy 安装与兼容说明

本文面向拿到 `Enthalpy.dll` 的测试者，说明它如何被加载、为什么可能"看起来没被加载"，以及出问题时怎么定位。

## 1. 这个插件是什么、怎么被加载

`Enthalpy.dll` 是一个 **Syringe 插件**：通过 Syringe 类加载器把代码钩进 `gamemd.exe`。它不是独立程序，不能双击运行，也不会自己启动游戏。

加载方式取决于机器上装的是哪种 Syringe：

| 加载器 | 行为 | 需要做什么 |
| --- | --- | --- |
| **SyringeEx**（Phobos 0.4+ 自带） | 启动时扫描游戏目录下所有 `*.dll`，自动加载 | 把 `Enthalpy.dll` 放进游戏目录即可，不需要命令行 |
| **经典 Syringe 0.7.x**（含 Mental Omega 自带） | 只加载命令行里显式列出的 DLL | 必须写成 `Syringe.exe -i=Ares.dll -i=Phobos.dll -i=Enthalpy.dll gamemd.exe` |

CnCNet / Mental Omega 客户端属于第二种：它在 `Resources\ClientDefinitions.ini` 的 `ExtraCommandLineParams` 里写死了一串 `-i=`，并且**启动时缓存该文件**，改完必须重启客户端才生效。确认方式：客户端日志 `client.log` 里的 `Launch arguments:` 行、以及 `syringe.log` 里的 `WinMain: arguments =` 行，都必须出现 `-i=Enthalpy.dll`。

**判断"有没有挂上"看 `syringe.log` 的这一行**（不是看 `Enthalpy.log`）：

```
SyringeDebugger::FindDLLs: Recognized DLL: "Enthalpy.dll"
```

有这一行 = 注入成功；没有 = 加载器找不到、或没被要求加载这个文件。

## 2. "Syringe 没有注入我的 DLL" 的常见原因

按出现频率排序：

1. **DLL 不在启动时的工作目录**。CnCNet 启动的是 `gamemd-spawn.exe`、MO 启动的是 `gamemd.exe`，DLL 必须与该 exe 同目录。放在 `Client\`、`Resources\` 或别的子目录都不会被找到。
2. **经典 Syringe 但没有 `-i=Enthalpy.dll`**。只把文件丢进目录不会加载，必须写进参数。
3. **改了 `ClientDefinitions.ini` 但没重启客户端**。客户端只在启动时读一次，之后每次开局都用缓存里的那串参数。
4. **DLL 是 64 位或者不是 DLL**。`gamemd.exe` 是 32 位进程，x64 DLL 会被直接拒绝加载。
5. **依赖未安装的 VC 运行库**。`/MD` 编译的插件需要目标机装 VC++ 2015-2022 x86 运行库，缺了 `LoadLibrary` 就失败。**本插件用 `/MT` 静态链接 CRT，不依赖任何 VC 运行库**，这条不会成为原因。
6. **文件被系统"锁定"**。从网络/压缩包解出的 DLL 可能带 Zone.Identifier 标记而被拦截：`Unblock-File Enthalpy.dll`。
7. **同地址 hook 冲突**。Syringe 一个地址只允许一个 hook；被别的插件占了，我们的 hook 会被静默跳过——不报错，只是不生效。本插件占用：`0x6BD68D`、`0x6D3D10`、`0x6D9134`、`0x692423`。
8. **游戏版本不对**。所有 hook 都是绝对地址，只对 **Yuri's Revenge 1.001 的 `gamemd.exe`** 成立。注意 MO 的 `gamemd.exe` 被改过（4,816,864 字节，原版 4,813,072），但本插件校验的四个字节点完全一致，所以 MO 属于**受支持**的版本。

## 3. 插件自己的自检行为

- **加载即写一行日志**（路径见第 5 节）。文件存在 = 注入并初始化成功。
- **启动校验游戏**：读取 `gamemd.exe` 的 PE 头，逐字节比对四个 hook 点。不匹配时打印实际字节并**主动停用**整个插件（宁可不工作，也不在未知构建上乱跳）：

  ```
  compat: gamemd.exe: imageBase=0x00400000 size=0x793000;  ghost draw@0x6D9134 expected 68A06F got XXXXXX; THIS IS NOT YURI'S REVENGE 1.001 - plugin disabled
  ```

- **绘制钩子看门狗**：帧钩子在跑、绘制钩子连续 900 帧一次没执行（典型原因就是上面第 7 条），日志会写：

  ```
  formation: WARNING the ghost draw hook never ran; another plugin probably owns 0x6D9134.
  ```

## 4. 配置读的是哪个文件

插件只认"带 `[Enthalpy]` 段"的文件，按以下顺序取第一个命中的（都在 DLL 所在目录）：

1. `Enthalpy.ini` —— 插件自带模板，**优先级最高**，建议改这个
2. `RA2MD.INI` —— 原版 YR
3. `RA2MO.ini` —— **Mental Omega 用的就是它**
4. `RA2.INI`

日志里会写明实际用的是哪个文件：

```
log: C:\...\Enthalpy.log
settings: ini=C:\...\RA2MO.ini
```

所有候选都没有 `[Enthalpy]` 段时全部走默认值，日志会写 `settings: ini=(none found, using defaults)`。**注意：把 `[Enthalpy]` 加到原版 YR 的 `RA2MD.INI` 里对 MO 无效**——MO 不读那个文件，且 MO 客户端每次启动都会重写 `RA2MO.ini`。

配置键：

```ini
[Enthalpy]
FormationSelectSaved=no   ; 空选时是否复用上次选中的单位
TargetBlock=no            ; 是否在剪影脚下画格子
GhostOpacity=2            ; 0 全透明 .. 4 全不透明
GhostShadow=yes           ; 是否保留阴影
GhostColorValid=0,251,0   ; 合法格剪影色 R,G,B
GhostColorInvalid=255,0,0 ; 非法格剪影色
GhostColorEdge=0,251,0    ; 描边色
Enthalpydebug=no          ; yes 才写日志
```

## 5. 日志写到哪里

优先写在 DLL 旁边（游戏目录）。若游戏装在 `Program Files (x86)` 这类普通进程无写权限的位置（**Mental Omega 正是如此**），自动退回到：

```
%LOCALAPPDATA%\Enthalpy\Enthalpy.log
```

实际路径就是日志第二行 `log:` 的内容；找不到时两个位置都看一眼。`Enthalpydebug=no` 时不创建任何文件。

## 6. 分发检查清单

- [ ] 发布 `bin\Release\Enthalpy.dll`（`/MT` 静态 CRT，约 200 KB）；Debug 版约 1.1 MB，仅供本地调试。
- [ ] 附带 `Enthalpy.ini` 模板（与 DLL 同目录），让测试者知道有哪些开关。
- [ ] SyringeEx → 直接放进游戏目录；经典 Syringe / 客户端 → 在 `-i=` 列表里加一项，并**重启客户端**。
- [ ] 让对方用 `Enthalpydebug=yes` 跑一次，确认 `syringe.log` 有 `Recognized DLL: "Enthalpy.dll"`，且日志里有 `hook: WinMain registrations reached` 与 `compat: ... hook sites verified`。
- [ ] 出现 `plugin disabled` 说明对方 `gamemd.exe` 不是 1.001，应换游戏版本而不是换插件。

## 7. 已知边界

- 只支持 **Yuri's Revenge 1.001** 的 32 位 `gamemd.exe`（含 MO 的修改版，其四个 hook 点与原版逐字节一致）。
- 与 Ares、Phobos 共存：已避开 Phobos 占用的 `0x692419`、`0x6D4B50`、`0x6D4C68` 等地址；若与其它插件冲突，看第 3 节 WARNING。
- 不修改任何游戏数据文件，只读 `[Enthalpy]` 配置段。