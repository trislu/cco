# 003 — C shared library

Builds `bin/libgreeter.so` (or `.dylib`) and links an executable against it.

```sh
cc -std=c11 -O2 build.c -o build
./build run app
```

MSVC DLL support is pending.
