# NOTICE

## Bundled third-party code

### YRpp

`YRpp/` 是 [Phobos-developers/YRpp](https://github.com/Phobos-developers/YRpp) 的副本
（master 分支快照），随本仓库一同分发，以便在没有网络与子模块的情况下直接编译。

- 版权归 YRpp 原作者所有。
- 许可：GPL-3.0（与本项目一致，全文见 `LICENSE.md`）。
- 本地改动：`YRpp/GeneralStructures.h` 与 `YRpp/Quaternion.h` 中各有一个非 ASCII 字节
  （GBK 前导字节吞掉了结构体的 `{`），已替换为 ASCII `.`；其余文件未改动。
- `YRpp/StaticInits.cpp` 参与编译，为 YRpp 的静态成员提供定义。

### 相关项目

- **Syringe / SyringeEx**：注入器，本项目以 `DEFINE_HOOK` 描述符的方式与之协作，不分发其代码。
- **Phobos**：启发与参考来源（钩子点位、剪影绘制思路），本项目不链接其代码。

## 商标

Yuri's Revenge、Command & Conquer 及相关素材为 Electronic Arts 的商标或注册商标。
本项目与 EA 无关，不包含任何游戏素材。
