# cco

"C/C++ Compiler Only"

**English** | [简体中文](README.zh-CN.md)

## The `Compiler Only` idea

The idea is that when you learn a programming language, the compiler should be
all you need.

Inspired by:

- [nob.h](https://github.com/tsoding/nob.h)
- [zig build](https://ziglang.org/learn/build-system/)
- [odin build](https://odin-lang.org/docs/overview/)
- Jai Programming Language (not yet released)

## Quick start

`cco` has two independent implementations:

| Language | Header | Seed | Docs |
| --- | --- | --- | --- |
| C11 | `c/cco.h` | `c/cco.c` | [c/README.md](c/README.md) · [API](c/API.md) |
| C++17 | `cpp/cco.hpp` | `cpp/cco.cpp` | [cpp/README.md](cpp/README.md) · [API](cpp/API.md) |

Copy the appropriate single header into a project, or use the scaffold:

```sh
# C
cc -std=c11 c/cco.c -o cco
./cco new c-app

# C++
c++ -std=c++17 cpp/cco.cpp -o cco++
./cco++ new cxx-app
```

Both scaffolds copy the matching header into the generated project. See the
language READMEs for installation, artifacts, examples, limits, and roadmap.

## Repository

```text
c/       C11 implementation + examples
cpp/     C++17 implementation + examples
docs/    design notes and the cco(1) / cco++(1) man pages
tools/   reserved for future cco_from_* import helpers
```

## Documents

- [C11 implementation](c/README.md) · [API](c/API.md)
- [C++17 implementation](cpp/README.md) · [API](cpp/API.md)
- [Design draft](docs/design.md)
- Reference pages: `docs/man/cco.1` and `docs/man/cco++.1`, readable with
  `man -l docs/man/cco++.1`

## License

MIT OR Public Domain (Unlicense). See [LICENSE](LICENSE).
