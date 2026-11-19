# cco++ API (C++17)

Reference for writing `build.cpp`. The header is [`cco.hpp`](cco.hpp) in this
directory. The Doxygen comments in `cco.hpp` describe each symbol. This page
adds the grammar, the defaults, and the rules that cross symbols.

The seed binary `cco++` is built from `cco.cpp`. Generated projects carry only
`cco.hpp`.

## Include and bootstrap

A build script is both the build description and the build tool:

```cpp
#define CCO_IMPLEMENTATION
#include "cco.hpp"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);

    cco::Workspace ws(argc, argv);
    auto &app = ws.executable("app", {"src/main.cpp"});
    return ws.run();
}
```

Compile the script once:

```sh
c++ -std=c++17 build.cpp -o build
```

Define `CCO_IMPLEMENTATION` in exactly one translation unit. A build script
normally has one.

## Grammar

```text
cco::Workspace ws(argc, argv)
├─ .executable(name, sources?)       -> Target &
├─ .library(name, sources?)          -> Target &
├─ .shared_library(name, sources?)   -> Target &
├─ .test(name, sources?)             -> Target &
├─ .managed_layout()                 -> Workspace &
├─ .set_build_root(dir)              -> Workspace &
└─ .run()                            -> int

Target &
├─ .sources({...})                   replace the list
├─ .include(dir)                     append, inherited
├─ .define(NAME[=VALUE])             append, inherited
├─ .cxxflag(flag)                    append, this target only
├─ .linkflag(flag)                   append, inherited
├─ .link(other)                      add a dependency
├─ .output_to(path)                  set the output path
└─ .standard("c++20")                default "c++17"
```

## Workspace

| Member | Behavior |
| --- | --- |
| `Workspace(argc, argv)` | Capture the arguments and the current directory, and detect a toolchain. |
| `executable(name, sources?)` | Create an executable target. |
| `library(name, sources?)` | Create an object group. No archive file is produced. |
| `shared_library(name, sources?)` | Create a shared library with an rpath entry. |
| `test(name, sources?)` | Create a test target. `test` builds and runs it. |
| `managed_layout()` | Put artifacts under `.cco/`. |
| `set_build_root(dir)` | Put artifacts under `dir/`. |
| `run()` | Parse `argv`, build, and return an exit code. |

`run()` returns 0 on success, 1 on a build or test failure, and 2 on a usage
error.

A reference from a factory stays valid until the workspace is destroyed.
`Workspace` stores targets in a `std::deque`.

`workspace.toolchain` is overwritten by `run()`. To select a compiler, pass
`--cxx` or set `$CXX`.

## Target

| Member | Behavior | Default |
| --- | --- | --- |
| `sources(files)` | Replace the source list. | empty |
| `include(dir)` | Append an include directory. | empty |
| `define(def)` | Append `NAME` or `NAME=VALUE`. | empty |
| `cxxflag(flag)` | Append a compile flag for this target. | empty |
| `linkflag(flag)` | Append a link flag. | empty |
| `link(other)` | Add a link dependency. | none |
| `output_to(path)` | Set the output path. | layout default |
| `standard(std)` | Set the language standard. | `c++17` |

`sources()` replaces. Every other setter appends. Call `sources()` once, or pass
a `cco::glob` result.

### Inheritance

| Entry | Inherited by dependent targets |
| --- | --- |
| `include()` | yes, transitively |
| `define()` | yes, transitively |
| `linkflag()` | yes, transitively |
| `cxxflag()` | no |

`link(other)` changes the link line in three ways:

- Object files of a `Library` dependency join the link line.
- A `SharedLibrary` dependency adds `-L<dir>`, `-l<name>`, and an rpath entry.
  The rpath entry is `$ORIGIN` on Linux and `@loader_path` on macOS.
- Include directories, defines, and link flags of the dependency are inherited.

Objects keep depth-first order: the target's own objects come first, then the
objects of each dependency.

## Layout

| Mode | Binaries | Objects | State |
| --- | --- | --- | --- |
| Default | `bin/` | `obj/<target>/<source>.o` | `obj/state.txt` |
| `managed_layout()` | `.cco/bin/` | `.cco/obj/<target>/<source>.o` | `.cco/state.txt` |
| `set_build_root(d)` | `d/bin/` | `d/obj/<target>/<source>.o` | `d/state.txt` |

`output_to()` overrides the binary path for one target. Relative paths in
`output_to()` and `set_build_root()` resolve against the workspace root.

The source path in an object path is relative to the workspace root. A source
outside the root contributes only its file name.

## Incremental rules

- An action fingerprint is a SHA-256 hash over the action kind, the toolchain
  fingerprint, the argument list, the working directory, the relevant
  environment, the input hashes, and the output paths.
- `(size, mtime_ns)` is a fast path. A stat change triggers a rehash.
- Header dependencies come from depfiles. GCC and Clang write them with
  `-MD -MF`; MSVC writes them with `/sourceDependencies`.
- A changed flag, define, or include directory changes the argument list and
  rebuilds the affected targets.
- A comment change in `build.cpp` rebuilds the script through the self-rebuild
  check, not the project.

## Helpers

| Symbol | Behavior |
| --- | --- |
| `cco::glob(pattern)` | Expand `**`, `*`, and `?` relative to the current directory. Skip `.cco`, `.git`, `.hg`, and `.svn`. Sort the results. |
| `cco::run_program(args, opt)` | Run a child process without a shell and wait. Return the exit code, or -1 when the child cannot start. |
| `cco::read_file(path, out)` | Read a whole file. |
| `cco::write_file(path, content, err)` | Create parent directories, write a temporary file, and rename it into place. |
| `CCO_GO_REBUILD_URSELF(argc, argv)` | Rebuild `build.cpp` when it or `cco.hpp` is newer, then re-exec the binary. |

## Command line

| Command | Action |
| --- | --- |
| `build [targets]` | Default command. |
| `run <target> [-- args]` | Build the target, then run it. |
| `test [targets]` | Build and run test targets. |
| `compdb` | Write `compile_commands.json` at the workspace root. |
| `clean` | Remove the build root, or `obj/` and the target outputs in the default layout. |
| `explain [targets]` | Build and print why each action runs or is skipped. |
| `graph`, `bsp`, `watch` | Reserved. The prototype prints an error and returns 1. |

Without a target list, `build` selects executables and shared libraries. When
none exist, `build` selects libraries, then tests.

| Option | Behavior |
| --- | --- |
| `--cxx <path>` | Compiler driver. `$CXX` is the fallback. |
| `--jobs <n>` | Accepted and ignored. Builds are sequential. |
| `--verbose` | Echo each command. |
| `--dry-run` | Print commands without running them. |
| `--explain` | Print rebuild reasons. |
| `--no-color` | Accepted, no effect. |
| `--help` | Print usage. |

## Recipes

| Goal | Example |
| --- | --- |
| Minimal executable | [`000_hello_world`](examples/000_hello_world/) |
| Custom output names | [`001_output_customization`](examples/001_output_customization/) |
| Object-group library | [`002_static_library`](examples/002_static_library/) |
| Shared library and rpath | [`003_shared_library`](examples/003_shared_library/) |
| External dependency | [`004_raylib`](examples/004_raylib/) |
| Code generation | [`005_protobuf`](examples/005_protobuf/) |

## Limits

- Builds are sequential. `--jobs` has no effect.
- Response files, file locking, and parallel scheduling are not implemented.
- `graph`, `bsp`, and `watch` are not implemented.
- MSVC shared library and DLL support is pending.
- Generated headers must exist before the consuming compile. `005_protobuf`
  runs `protoc` in `main()` for this reason.
- `state.txt` is a prototype format, not a stable schema.
- `004_raylib` and `005_protobuf` are not part of the default CI matrix.
