# 002 — static library

Shows a reusable object-group library, an executable that links it, and a
test target.

```sh
c++ -std=c++17 build.cpp -o build
./build run app
./build test
```

cco's `library()` is an object group, not an `ar` archive. The objects are
compiled once and linked directly into `app` and `greeter_test`. This is why
the example needs no `ar` / `lib.exe` and still stays compiler-only.
