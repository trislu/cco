# 004 — raylib

Shows how to consume a prebuilt third-party library with include and link
flags.

## Dependency

- [raylib](https://www.raylib.com/)

This demo assumes you have raylib installed to default location

```text
/usr/local/
├── include/raylib.h
└── lib/libraylib.a (or .so / .dylib)
```

Then:

```sh
export RAYLIB_DIR=/path/to/raylib   # optional if vendored
c++ -std=c++17 build.cpp -o build
./build run raylib_demo
```

The build fails with a clear message if `raylib.h` is not found. This example
is intentionally not part of the default CI matrix; it needs a GUI/raylib
installation.
