# C examples

The C11 implementation lives in `../cco.h`; each example uses
`../../cco.h` directly and has a `build.c` description.

| Directory | Demonstrates | External dependency |
|---|---|---|
| `000_hello_world` | executable | none |
| `001_output_customization` | `cco_target_output_to()` | none |
| `002_static_library` | object-group library, app, test | none |
| `003_shared_library` | shared library + rpath | POSIX only |
| `004_raylib` | third-party include/library flags | raylib |

```sh
cd 000_hello_world
cc -std=c11 -O2 build.c -o build
./build run hello
```

Projects created by `cco` (the C seed) vendor their own `cco.h` and use
`cco_workspace_managed_layout()`.
