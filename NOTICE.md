# NOTICE

本文件说明本仓库的许可归属、随附第三方代码的版权与改动，以及分发二进制时的源码获取方式。

## 1. 本项目

- 名称：Enthalpy
- 许可：GPL-3.0-or-later，全文见 [`LICENSE.md`](LICENSE.md)
- 你可以自由使用、修改、再分发；再分发时须以同一许可提供源码，并保留本文件与 `LICENSE.md`。

## 2. 随附的 YRpp

`YRpp/` 是 [Phobos-developers/YRpp](https://github.com/Phobos-developers/YRpp) 的源码快照（`master` 分支，2025-10-25 取用），随本仓库一同分发，以便在没有网络、也没有 git 子模块的情况下直接编译。

- 版权：归 YRpp 原作者（Phobos 开发者社区）所有。
- 许可：GPL-3.0。上游仓库未附带单独的 LICENSE 文件，本项目根目录的 [`LICENSE.md`](LICENSE.md) 即为同一份 GPL-3.0 全文。
- 本项目对 YRpp 的改动（依 GPL-3.0 第 5 条声明改动内容与日期）：

| 文件 | 改动 | 日期 |
| --- | --- | --- |
| `YRpp/GeneralStructures.h` | 文件内 1 个非 ASCII 字节（GBK 前导字节，会吞掉紧随的 `{`）替换为 ASCII `.` | 2025-10-25 |
| `YRpp/Quaternion.h` | 同上，1 个非 ASCII 字节替换为 ASCII `.` | 2025-10-25 |

  其余文件未作内容修改。`YRpp/StaticInits.cpp` 参与本项目编译，为 YRpp 的静态成员提供定义。

## 3. 未随本仓库分发的第三方组件

以下组件**不在**本仓库内，也不是本项目的组成部分，仅作为运行环境或参考：

- **Syringe / SyringeEx**：DLL 注入器。本项目只提供 `DEFINE_HOOK` 描述符，由注入器在运行时读取，不与注入器链接。
- **Phobos**：启发与参考来源（hook 站点选择、剪影绘制思路）。本项目不链接、不修改、不分发其代码。
- **Ares**：兼容性测试环境之一，不分发。

分发 Enthalpy 的二进制文件时，只需附带 `Enthalpy.dll` 与 `Enthalpy.ini`；上述组件请让使用者自行从官方渠道获取。

## 4. 分发二进制时的源码获取

本项目以 GPL-3.0 发布。若在 GitHub Release 或其他渠道分发 `Enthalpy.dll`，请在发布页面注明：

> Enthalpy is licensed under GPL-3.0-or-later. Source code: <本仓库地址>

这样即满足 GPL-3.0 第 6 条“随二进制提供对应源码”的要求（通过网络提供等价获取方式）。

## 5. 商标

Yuri's Revenge、Command & Conquer 及相关素材为 Electronic Arts 的商标或注册商标。本项目与 Electronic Arts 无关，不包含任何游戏素材（美术、音频、地图、`gamemd.exe` 等）。
