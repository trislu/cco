# 001 — output customization

Hand-written projects use the manual layout by default: the binary lands in
`bin/` (`./bin/custom-output`) and objects go to `obj/`.

This example overrides the final artifact path:

```cpp
std::string output = "dist/custom-output";
#ifdef _WIN32
output += ".exe";
#endif

ws.executable("custom-output")
    .sources({"src/main.cpp"})
    .output_to(output);
```

The build description can have any filename; this example uses
`custom_build.cpp` instead of the conventional `build.cpp`. cco does not
inspect the filename. `CCO_GO_REBUILD_URSELF` uses `__FILE__`, so self-rebuild
works for any name.

```sh
c++ -std=c++17 custom_build.cpp -o build
./build run custom-output
```

The binary is now `dist/custom-output` (`.exe` on Windows) while object files
remain in `obj/`. `./build clean` removes `obj/`, the binary, and the empty
`dist/` directory; if `dist/` contains other files, those files and the
directory are left alone.

For a project-wide custom root instead of a single target, use:

```cpp
ws.set_build_root("artifacts");  // artifacts/bin + artifacts/obj
```

`cco new` projects call `ws.managed_layout()` and use `.cco/bin` + `.cco/obj`.
