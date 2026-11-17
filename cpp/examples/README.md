# C++ examples

Each numbered directory is a standalone `cco` project. Examples in this
repository include `../../cco.hpp` directly; projects created by `cco++ new`
vendor their own copy of `cco.hpp`.

Build any example from its directory. Most examples call their build
description `build.cpp`; cco does not require that name, and `001` uses
`custom_build.cpp` instead. Each example's README shows the exact command.

```sh
cd 000_hello_world
c++ -std=c++17 build.cpp -o build
./build run hello
```

| Directory | Demonstrates | External dependency |
|---|---|---|
| `000_hello_world` | executable + globbed sources | none |
| `001_output_customization` | `output_to()` / `set_build_root()` | none |
| `002_static_library` | object-group library, app, test target | none |
| `003_shared_library` | shared library + runtime rpath | POSIX only (MSVC pending) |
| `004_raylib` | third-party include/library flags | raylib |
| `005_protobuf` | generated protobuf sources + library | protobuf + protoc |

The repository has no root build; each example is self-contained.

These examples are hand-written, so they use cco's default **manual layout**:
binaries land in `bin/` and object files in `obj/`. `001_output_customization`
deliberately overrides the binary path to `dist/custom-output`. Projects
created by `cco++ new` call `ws.managed_layout()` and use `.cco/bin/` and
`.cco/obj/` instead.

`002_static_library` uses cco's object-group library. cco deliberately does
not invoke `ar` / `lib.exe`; a "static library" is a set of objects linked
directly into consumers. This keeps the project buildable with only a C++
compiler driver.
