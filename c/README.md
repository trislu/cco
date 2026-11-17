# cco (C11)

The C11 implementation of cco. M1 and the M2 core are implemented.

## Quick start

Copy the single header into a project:

```c
#define CCO_IMPLEMENTATION
#include "cco.h"
```

Or use the `cco` scaffold:

```sh
cc -std=c11 c/cco.c -o cco
./cco new myapp
cd myapp
cc -std=c11 build.c -o build
./build run myapp
```

Install the binary and header into a prefix:

```sh
cc -std=c11 c/cco.c -o cco
install -D cco /usr/local/bin/cco
install -D c/cco.h /usr/local/include/cco.h
```

`cco new` resolves `cco.h` from `../include` relative to the real executable,
so invoking `cco` through `PATH` works. Use `--cco-header <path>` when the
header lives elsewhere.

## Features

- Target graph: executable, library (object group), shared library, test
- Manual layout: `bin/`, `obj/`
- Managed layout: `.cco/bin/`, `.cco/obj/`
- Custom layout: `cco_workspace_set_build_root(...)` and
  `cco_target_output_to(...)`
- Content fingerprints + SHA-256
- `(size, mtime_ns)` stat fast path
- depfiles: GCC/Clang `-MD`; MSVC `/sourceDependencies`
- Toolchain fingerprint
- State cache: `obj/state.txt` (manual), `.cco/state.txt` (managed)
- `run`, `test`, `compdb`, `clean`, `explain`
- POSIX shared libraries with rpath
- `CCO_GO_REBUILD_URSELF`

## Examples

See [examples/README.md](examples/README.md).

- `000_hello_world`
- `001_output_customization`
- `002_static_library`
- `003_shared_library`
- `004_raylib` (external dependency)

## Limits

- Compilation and linking are sequential; `--jobs` is accepted but ignored.
- Response files, file locking, and parallel scheduling are not implemented.
- BSP and `watch` are not implemented.
- MSVC shared-library/DLL support is pending.
- Windows path/process code exists, but the C CI currently validates Linux
  GCC/Clang only.
- Cross compilation is not split into explicit host/target toolchains yet.
- `state.txt` is a prototype line format, not a stable JSON schema.
- Generated-header ordering is not modelled as custom actions yet.

## Roadmap

- M2 hardening: MSVC DLLs, Windows CI, response files, lock, parallel scheduler
- Host/target toolchain separation for cross builds
- M3: BSP build/diagnostic subset
- M5: `cco_from_*` import helpers under `tools/`

## Documents

- [Design draft](../docs/design.md)
- [C++ implementation](../cpp/README.md)
