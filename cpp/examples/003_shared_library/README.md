# 003 — shared library

Builds `bin/libgreeter.so` / `bin/libgreeter.dylib`, then links an executable against
it. cco adds the right `-L`, `-l`, and rpath flags:

- Linux: `-Wl,-rpath,$ORIGIN`
- macOS: `-Wl,-rpath,@loader_path`

```sh
c++ -std=c++17 build.cpp -o build
./build run app
```

MSVC shared-library support is not implemented in this prototype yet; the
example is POSIX-only for now.
