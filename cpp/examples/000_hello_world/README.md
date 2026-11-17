# 000 — hello world

Smallest cco project: one executable and one source file.

```sh
c++ -std=c++17 build.cpp -o build
./build
./build run hello
```

`cco::glob("src/**/*.cpp")` expands at configure time; no manual source list.
