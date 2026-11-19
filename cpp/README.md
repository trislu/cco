# cco++

The C++17 implementation of cco.

## Quick start

Copy the single header into a project:

```cpp
#define CCO_IMPLEMENTATION
#include "cco.hpp"
```

Or use the `cco++` scaffold:

```sh
c++ -std=c++17 cpp/cco.cpp -o cco++
./cco++ new myapp
cd myapp
c++ -std=c++17 build.cpp -o build
./build run myapp
```

Install the binary and header into a prefix:

```sh
c++ -std=c++17 cpp/cco.cpp -o cco++
install -D cco++ /usr/local/bin/cco++
install -D cpp/cco.hpp /usr/local/include/cco.hpp
```

`cco++ new` resolves `cco.hpp` from `../include` relative to the real
executable, so invoking `cco++` through `PATH` works. Use
`--cco-header <path>` when the header lives elsewhere.

## Features

- Target graph: executable, library (object group), shared library, test
- Manual layout: `bin/`, `obj/`
- Managed layout: `.cco/bin/`, `.cco/obj/`
- Custom layout: `ws.set_build_root(...)` and `target.output_to(...)`
- Content fingerprints + SHA-256
- `(size, mtime_ns)` stat fast path
- depfiles: GCC/Clang `-MD`; MSVC `/sourceDependencies`
- Toolchain fingerprint
- State cache: `obj/state.txt` (manual), `.cco/state.txt` (managed)
- `run`, `test`, `compdb`, `clean`, `explain`
- GCC/Clang shared libraries with `$ORIGIN` / `@loader_path` rpath
- `CCO_GO_REBUILD_URSELF`

## Examples

See [examples/README.md](examples/README.md).

- `000_hello_world`
- `001_output_customization`
- `002_static_library`
- `003_shared_library`
- `004_raylib` (external dependency)
- `005_protobuf` (external dependency)

## Limits

- Compilation and linking are sequential; `--jobs` is accepted but ignored.
- Response files, file locking, and parallel scheduling are not implemented.
- BSP and `watch` are not implemented.
- MSVC shared-library/DLL support is pending.
- `state.txt` is a prototype line format, not a stable JSON schema.
- Cross compilation is not split into explicit host/target toolchains yet.
- Header changes are tracked through depfiles; generated headers must be
  produced before the consuming compile.
- 004 and 005 are not part of the default CI matrix.

## Roadmap

- M2 hardening: response files, lock, parallel scheduler
- M3: BSP build/diagnostic subset
- M5: `cco_from_*` import helpers under `tools/`

## Documents

- [API reference](API.md)
- [Design draft](../docs/design.md)
- [C11 implementation](../c/README.md)
