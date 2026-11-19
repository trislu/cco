# cco API (C11)

Reference for writing `build.c`. The header is [`cco.h`](cco.h) in this
directory. The Doxygen comments in `cco.h` describe each symbol. This page adds
the grammar, the defaults, the ownership rules, and the CLI.

The seed binary `cco` is built from `cco.c`. Generated projects carry only
`cco.h`.

## Include and bootstrap

A build script is both the build description and the build tool:

```c
#define CCO_IMPLEMENTATION
#include "cco.h"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);

    Cco_Workspace ws;
    cco_workspace_init(&ws, argc, argv);
    Cco_Target *app = cco_executable(&ws, "app");
    cco_target_add_source(app, "src/main.c");

    int rc = cco_workspace_run(&ws);
    cco_workspace_free(&ws);
    return rc;
}
```

Compile the script once:

```sh
cc -std=c11 build.c -o build
```

Define `CCO_IMPLEMENTATION` in exactly one translation unit. A build script
normally has one.

## Grammar

```text
Cco_Workspace ws
├─ cco_workspace_init(&ws, argc, argv)
├─ cco_executable(&ws, name)            -> Cco_Target *
├─ cco_library(&ws, name)               -> Cco_Target *
├─ cco_shared_library(&ws, name)        -> Cco_Target *
├─ cco_test(&ws, name)                  -> Cco_Target *
├─ cco_workspace_managed_layout(&ws)
├─ cco_workspace_set_build_root(&ws, d)
├─ cco_workspace_run(&ws)               -> int
└─ cco_workspace_free(&ws)

Cco_Target *
├─ cco_target_add_source(t, path)       append, copied
├─ cco_target_add_include(t, dir)       append, inherited
├─ cco_target_add_define(t, NAME)       append, inherited
├─ cco_target_add_cflag(t, flag)        append, this target only
├─ cco_target_add_linkflag(t, flag)     append, inherited
├─ cco_target_link(t, other)            add a dependency
├─ cco_target_output_to(t, path)        set the output path
└─ cco_target_standard(t, "c11")        default "c11"
```

## Lifetime and ownership

`cco_workspace_init` copies `argc`/`argv`, captures the current directory, and
detects a compiler. `cco_workspace_run` returns 0 on success, 1 on a build or
test failure, and 2 on a usage error. `cco_workspace_free` releases every target
and every string.

Every `cco_target_add_*` call copies its string argument. The caller keeps
ownership of the argument and may reuse or free it.

Factory pointers belong to the workspace. Do not free them.

`cco_glob` returns an array owned by the caller. Release it with
`cco_str_array_free` after the entries have been consumed:

```c
Cco_Str_Array srcs = cco_glob("src/*.c");
for (size_t i = 0; i < srcs.count; ++i)
    cco_target_add_source(app, srcs.items[i]);
cco_str_array_free(&srcs);
```

Arrays inside a target belong to the workspace. Do not pass them to
`cco_str_array_free`.

## Workspace functions

| Function | Behavior |
| --- | --- |
| `cco_workspace_init` | Capture the arguments and the current directory, and detect a compiler. |
| `cco_workspace_free` | Release every target and every string. |
| `cco_executable` | Create an executable target. |
| `cco_library` | Create an object group. No archive file is produced. |
| `cco_shared_library` | Create a shared library with an rpath entry. |
| `cco_test` | Create a test target. `test` builds and runs it. |
| `cco_workspace_managed_layout` | Put artifacts under `.cco/`. |
| `cco_workspace_set_build_root` | Put artifacts under `dir/`. |
| `cco_workspace_run` | Parse `argv`, build, and return an exit code. |

## Target functions

| Function | Behavior | Default |
| --- | --- | --- |
| `cco_target_add_source` | Append one source file. | empty |
| `cco_target_add_include` | Append one include directory. | empty |
| `cco_target_add_define` | Append `NAME` or `NAME=VALUE`. | empty |
| `cco_target_add_cflag` | Append one compile flag for this target. | empty |
| `cco_target_add_linkflag` | Append one link flag. | empty |
| `cco_target_link` | Add a link dependency. | none |
| `cco_target_output_to` | Set the output path. | layout default |
| `cco_target_standard` | Set the language standard. | `c11` |

Every setter appends one entry and copies the string. There is no bulk source
call; loop over a `cco_glob` result.

### Inheritance

| Entry | Inherited by dependent targets |
| --- | --- |
| `cco_target_add_include` | yes, transitively |
| `cco_target_add_define` | yes, transitively |
| `cco_target_add_linkflag` | yes, transitively |
| `cco_target_add_cflag` | no |

`cco_target_link` changes the link line in three ways:

- Object files of a `CCO_LIBRARY` dependency join the link line.
- A `CCO_SHARED_LIBRARY` dependency adds `-L<dir>`, `-l<name>`, and an rpath
  entry. The rpath entry is `$ORIGIN` on Linux and `@loader_path` on macOS.
- Include directories, defines, and link flags of the dependency are inherited.

Objects keep depth-first order: the target's own objects come first, then the
objects of each dependency.

## Layout

| Mode | Binaries | Objects | State |
| --- | --- | --- | --- |
| Default | `bin/` | `obj/<target>/<source>.o` | `obj/state.txt` |
| `cco_workspace_managed_layout` | `.cco/bin/` | `.cco/obj/<target>/<source>.o` | `.cco/state.txt` |
| `cco_workspace_set_build_root(d)` | `d/bin/` | `d/obj/<target>/<source>.o` | `d/state.txt` |

`cco_target_output_to` overrides the binary path for one target. Relative paths
in `cco_target_output_to` and `cco_workspace_set_build_root` resolve against the
workspace root.

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
- A comment change in `build.c` rebuilds the script through the self-rebuild
  check, not the project.

## Helpers

| Symbol | Behavior |
| --- | --- |
| `cco_glob(pattern)` | Expand `**`, `*`, and `?` relative to the current directory. Skip `.cco`, `.git`, `.hg`, and `.svn`. Sort the results. |
| `cco_str_array_free(array)` | Release an array returned by `cco_glob`, then zero it. A second call is safe. |
| `CCO_GO_REBUILD_URSELF(argc, argv)` | Rebuild `build.c` when it or `cco.h` is newer, then re-exec the binary. |

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
| `--cxx <path>` | Compiler driver. `$CC` is the fallback. |
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

## Limits

- Builds are sequential. `--jobs` has no effect.
- Response files, file locking, and parallel scheduling are not implemented.
- `graph`, `bsp`, and `watch` are not implemented.
- MSVC shared library and DLL support is pending.
- Windows path and process code exists, but CI validates Linux GCC and Clang
  only.
- Generated header ordering is not modelled as custom actions yet.
- `state.txt` is a prototype format, not a stable schema.
