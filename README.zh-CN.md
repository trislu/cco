# cco

"C/C++ Compiler Only"

[English](README.md) | **简体中文**

## 理念

当你学习一门编程语言时，编译器就是你需要的一切。

灵感来源：
- [nob.h](https://github.com/tsoding/nob.h)
- [zig build](https://ziglang.org/learn/build-system/)
- [odin build](https://odin-lang.org/docs/overview/)
- Jai Programming Language（尚未发布）

## 快速开始

`cco` 有两套独立实现：

| 语言 | 头文件 | seed | 文档 |
|---|---|---|---|
| C11 | `c/cco.h` | `c/cco.c` | [c/README.md](c/README.md) |
| C++17 | `cpp/cco.hpp` | `cpp/cco.cpp` | [cpp/README.md](cpp/README.md) |

把对应的单头文件拷进工程，或使用 scaffold：

```sh
# C
cc -std=c11 c/cco.c -o cco
./cco new c-app

# C++
c++ -std=c++17 cpp/cco.cpp -o cco++
./cco++ new cxx-app
```

两个 scaffold 都会把对应的头文件复制到生成工程。安装、产物布局、示例、
边界和 roadmap 见各语言 README。

## 仓库结构

```text
c/       C11 实现 + 示例
cpp/     C++17 实现 + 示例
tools/   预留给未来的 cco_from_* 导入工具
docs/    设计
```

## 文档

- [C11 实现](c/README.md)
- [C++17 实现](cpp/README.md)
- [设计草案](docs/design.md)

## 许可证

MIT 或 Public Domain（Unlicense）双许可，见 [LICENSE](LICENSE)。
