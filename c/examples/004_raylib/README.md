# 004 — raylib (C)

Shows how to consume a prebuilt third-party library from a C build
description.

## Dependency

- [raylib](https://www.raylib.com/)

The example looks for raylib under `/usr/local` by default:

```text
/usr/local/
├── include/raylib.h
└── lib/libraylib.a (or .so / .dylib)
```

Set `RAYLIB_DIR` to another prefix if needed:

```sh
export RAYLIB_DIR=/path/to/raylib
cc -std=c11 -O2 build.c -o build
./build run raylib_demo
```

The build fails with a clear message if `raylib.h` is not found. This example
is intentionally not part of the default CI matrix; it needs a GUI/raylib
installation.
