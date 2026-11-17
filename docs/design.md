# cco (compiler only) — Design Draft

> Status: v0.2 / discussion draft; M2 core prototype implemented
> Baseline: `tsoding/nob.h` v3.10.1 (3404 lines), Build Server Protocol 2.2.0
> Date: 2026-11-17
> Prototype status: the C++ (`cpp/`) and C11 (`c/`) M2 cores are implemented:
> target graph, content fingerprints, depfiles (GCC/Clang `-MD`, MSVC
> `/sourceDependencies`), state cache, `compile_commands.json`, `run` and
> `test`. Response files, locking, parallelism, and BSP remain unimplemented;
> the C implementation still lacks MSVC DLLs and full cross-compilation.

---

## 0. Summary

`compiler only` is not "nob.h plus `std::filesystem`". It is:

> **A single-header build runtime. The user's build source (for example
> `build.cpp` or `custom_build.cpp` / `build.c`) is both the build description
> and the build tool itself. The project depends only on a compiler driver:
> `g++`, `clang++`, `gcc`, `clang`, or `msvc`.**

Compared with nob.h, cco adds three things that nob.h deliberately leaves out:

1. **A declarative target/action graph** instead of hand-written commands.
2. **An incremental engine based on content fingerprints and depfiles**, not
   mtime checks.
3. **An IDE adaptation layer**: `compile_commands.json` for clangd, plus an
   optional BSP server.

It also provides `new` / `init` scaffolding that generates a minimal,
immediately buildable project (app or library). The scaffold is not part of
`cco.hpp`; it lives in the separate seed `cpp/cco.cpp` and is compiled only
into the `cco++` binary. The C seed is `c/cco.c` and produces `cco`.

---

## 1. Five Conclusions

1. **nob.h has no daemon and no in-memory incremental state.** It is a
   short-lived process every time. Incremental builds rely on the user calling
   `nob_needs_rebuild()` (mtime) inside their own recipe; self-rebuild uses
   `NOB_GO_REBUILD_URSELF` to compile itself and `exec` the new binary. That
   looks like a restart, but it is still an ordinary process. What resembles
   Bazel is the optional BSP server / watch process, not a requirement.
2. **Adding `std::filesystem` is not enough to get incrementality.** An action
   graph, content fingerprints, depfile parsing, and a state store are
   required. That is the essential difference from nob.h and the main body of
   work.
3. **Do not write an LSP.** Emitting `compile_commands.json` for clangd is the
   highest-value path to C++ code intelligence. BSP is an **optional adapter**
   that only implements a compile/diagnostics subset. There are almost no
   mainstream BSP clients for C++, so BSP ranks below compdb.
4. **Single header plus self-rebuild re-exec; no daemon by default.** A disk
   state store already provides most of the incremental benefit. `watch` /
   `bsp` are optional long-running modes that reuse the same engine. Do not
   introduce `ar` / `make` / `cmake` as build dependencies; static libraries
   are object groups instead of `.a` files.
5. **Build M0–M2 first (usable incremental builds + clangd), put BSP in M3,
   and importers in M5.** Do not start with a Bazel-style server, sandbox, or
   remote cache.

---

## 2. Anatomy of nob.h

### 2.1 Fact Check

- Single header, C99 style, with `#define NOB_IMPLEMENTATION` expanding the
  implementation.
- Usage is "write a shell script in C": the user explicitly assembles a
  `Nob_Cmd` and then calls `nob_cmd_run()`.
- Incremental builds: `nob_needs_rebuild(output, inputs...)` compares mtimes and
  returns 0/1/-1. **It is never called automatically**; the user decides when to
  check.
- Self-rebuild: `NOB_GO_REBUILD_URSELF(argc, argv)` compares the mtime of
  `argv[0]` with `__FILE__`. If the binary is stale, it renames the old binary
  to `.old`, compiles itself with `NOB_REBUILD_URSELF`, and on success `exec`s
  the new binary with the original argv. On failure it restores the old binary.
- Parallelism: `nob_cmd_run(&cmd, .async = &procs, .max_procs = N)` plus
  `nob_procs_flush()`. This is a flat concurrency limit, not dependency-graph
  scheduling.
- Missing: target graph, header dependency scanning, content hashing, cache
  database, `compile_commands.json`, BSP/LSP, diagnostic parsing, response
  files, MSVC dependency output.
- Version v3.10.1, about 80 public functions, all prefixed `nob_`, with an
  optional macro to remove the prefix.

> The impression that "phase 1 behaves like a daemon" most likely comes from
> `NOB_GO_REBUILD_URSELF`'s "recompile + re-exec" behavior. It is not a daemon
> and it does not preserve state across processes.

### 2.2 Feature Matrix

| Capability | nob.h today | Decision | Notes |
|---|---|---|---|
| Distribution | Single header + `NOB_IMPLEMENTATION` | ✅ Keep | `cco.hpp` / `cco.h` + `CCO_IMPLEMENTATION` |
| Naming | `nob_` prefix, optional prefix-removal macro | 🔁 Change | `namespace cco`; no prefix-removal macro |
| Logging / assertions | `nob_log`, levels, `nob_panic` | ✅ Keep | Add structured events for BSP / `--json` |
| Strings | `Nob_String_View`, `Nob_String_Builder` | 🔁 Change | `std::string_view` / `std::string` in C++; C uses its own string views/builders |
| Temporary allocator | `nob_temp_alloc/sprintf/save/rewind` | ❌ Remove | Small/medium projects can use `std::string`; avoid hidden global state |
| Dynamic arrays | `nob_da_*` macros | ❌ Remove | `std::vector` in C++; C uses typed arrays |
| Filesystem | mkdir, copy, rename, delete, read_dir, walk_dir, file_type | ✅ Keep capability | Reimplement with `std::filesystem` and add glob |
| Path utilities | `nob_path_name`, extension, dir, cwd, executable path | ✅ Keep | `std::filesystem::path` |
| Process | `Nob_Cmd`, append, sync/async, redirect | 🔁 Rewrite | Need stdout/stderr capture for diagnostics, direct argv, no shell; nob only redirects to files |
| Parallelism | `Nob_Procs` + `max_procs` | 🔁 Upgrade | Thread pool + DAG scheduling by dependency readiness |
| Pipes | `Nob_Chain` (shell pipes) | ❌ Do not implement | Rarely needed for builds; stdout/stderr redirection is enough |
| Incremental | `nob_needs_rebuild` (mtime) | 🔁 Upgrade | Use stat `(size, mtime_ns)` as a fast path, but decide with content fingerprints + depfiles |
| Self-rebuild | `NOB_GO_REBUILD_URSELF(_PLUS)` | ✅ Keep | `CCO_GO_REBUILD_URSELF`; depfiles replace `_PLUS` semantics |
| Compiler dialects | `nob_cc`, flags, inputs, outputs | 🔁 Upgrade | `Toolchain` abstraction: GNU / MSVC / clang-cl, versions, dependency flags, response files, diagnostics |
| CLI | `nob_shift_args` | ✅ Keep capability | Built-in `--jobs`, `--explain`, `--dry-run`, `--verbose`, subcommands |
| CPU / time | `nob_nprocs`, `nob_nanos_since_unspecified_epoch` | ✅ Keep | |
| Switches | `NOB_NO_ECHO`, `NOB_WARN_DEPRECATED` | ✅ Equivalent | `CCO_NO_ECHO`, etc. |
| Platform layer | Win32 / POSIX branches | ✅ Keep approach | Concentrated in a platform section |
| **Build model** | None (hand-written commands) | 🆕 New | **Target/action graph; the core difference** |
| **Cache** | None | 🆕 New | `state.txt` line format, indexed by fingerprint |
| **Header dependencies** | None | 🆕 New | GCC/Clang `-MD`, MSVC `/sourceDependencies` |
| **compdb** | None | 🆕 New | `compile_commands.json` for clangd |
| **BSP** | None | 🆕 Optional | Stdio JSON-RPC, compile subset |
| **Diagnostic parsing** | None | 🆕 New | GCC/Clang/MSVC formats → structured diagnostics |
| **Response files** | None | 🆕 New | Required for the Windows 32K command-line limit |
| **run / test** | None | 🆕 New | `./build run app`, `./build test` |
| **Custom commands** | User-written | 🆕 New | `custom` actions (codegen etc.) with explicit inputs/outputs |
| **Project scaffolding** | None | 🆕 New | `new` / `init`, app/lib templates, no Git by default |

### 2.3 What Incremental Builds Need That nob.h Lacks

- **Action model**: compile / link / custom actions with inputs, outputs, argv,
  cwd, env, and dependencies.
- **Dependency discovery**: parse depfiles after compilation and fold headers
  into the next fingerprint.
- **Fingerprint cache**: invalidate precisely when the command, toolchain,
  input content, or relevant environment changes.
- **DAG scheduling**: link must wait for objects; custom actions may sit in the
  middle.
- **Explainability**: `--explain` must say why something rebuilt or was skipped;
  otherwise users cannot trust incrementality.
- **Concurrency safety**: two `./build` processes must not corrupt each other;
  state writes must be atomic.

### 2.4 Four Designs Worth Borrowing Directly

1. **Single header + `*_IMPLEMENTATION`**: distribution and bootstrap stay
   trivial.
2. **`.old` rollback in `GO_REBUILD_URSELF`**: restore the old binary on a failed
   compile instead of leaving the user stuck.
3. **Redefinable `NOB_REBUILD_URSELF`**: an escape hatch for platforms and custom
   flags.
4. **"The build script is the build tool"**: no extra daemon, config file, or
   serialization format; the project contains only the build source and
   `cco.hpp` / `cco.h`.

### 2.5 An Example: The Ceiling of nob.h Style

Goal: `core` (three sources) + `app` + `examples`, where app/examples depend on
core.

Typical nob.h code (illustrative):

```c
NOB_GO_REBUILD_URSELF(argc, argv);

const char *core_srcs[] = {"src/core/a.cpp", "src/core/b.cpp", "src/core/c.cpp"};
const char *core_objs[] = {"build/core/a.o", "build/core/b.o", "build/core/c.o"};

for (size_t i = 0; i < 3; ++i) {
    if (nob_needs_rebuild(core_objs[i], &core_srcs[i], 1)) {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "c++", "-std=c++17", "-Iinclude", "-c", core_srcs[i], "-o", core_objs[i]);
        if (!nob_cmd_run(&cmd)) return 1;
    }
}
// Repeat for app / examples; write another block for linking; wire Nob_Procs by hand...
```

Problems:

1. **Header dependencies must be listed manually**; changing `foo.h` does not
   trigger a rebuild.
2. **Flag changes do not invalidate anything**; changing `-DDEBUG` still skips.
3. **No concurrent scheduling**; the user must wire `async`, `max_procs`, and
   `flush`.
4. **No target model**; compdb and BSP cannot be generated.
5. **Incremental logic is scattered through `main`**; it cannot be explained or
   tested uniformly.

cco's target form:

```cpp
cco::Workspace ws(argc, argv);
auto &core  = ws.library("core").sources(cco::glob("src/core/**/*.cpp")).include("include");
auto &app   = ws.executable("app").sources(cco::glob("src/app/**/*.cpp")).link(core);
auto &examples = ws.test("examples").sources(cco::glob("examples/**/*.cpp")).link(core);
return ws.run();
```

The engine handles depfiles, fingerprints, concurrency, linking, compdb, and
BSP; `ws.custom(...)` is the escape hatch for arbitrary commands. This is not
"nob.h cannot do it"; nob.h leaves each piece to the user, while cco makes those
pieces the default.

---

## 3. Anatomy of BSP

### 3.1 Protocol Model

- JSON-RPC 2.0, reusing the LSP base protocol: `Content-Length` header plus a
  JSON body.
- Transport: stdio (the only transport we implement) or socket.
- Lifecycle: client starts the server → `build/initialize` →
  `build/initialized` → requests/notifications → `build/shutdown` →
  `build/exit`.
- Server stdout may contain protocol frames only; logs go to stderr.
- Capabilities are declared once in `initialize`; **dynamic registration is not
  supported**.
- Discovery: `.bsp/<name>.json` in the workspace with `name`, `version`,
  `bspVersion`, `languages`, and `argv`. `argv` runs at the workspace root and
  communicates over stdio.

### 3.2 Method Matrix

| Method / capability | Purpose | Priority | Notes |
|---|---|---|---|
| `build/initialize` | Handshake, return capabilities | **P0** | `bspVersion: "2.2.0"` |
| `build/initialized` | Client acknowledgement | **P0** | May be empty |
| `build/shutdown` / `build/exit` | Lifecycle | **P0** | Exit 0 after shutdown, otherwise 1 |
| `workspace/buildTargets` | List targets | **P0** | Include tags, dependencies, capabilities, C++ data |
| `buildTarget/sources` | Target sources | **P0** | `SourceItem.kind = 1` (file) |
| `buildTarget/compile` | Trigger a build | **P0** | Stream tasks + diagnostics, return statusCode |
| `build/taskStart` / `taskFinish` / `taskProgress` | Progress UI | **P0** | `compile-task` / `compile-report` dataKind |
| `build/publishDiagnostics` | Compiler diagnostics | **P0** | Per-file, with correct `reset` semantics |
| `build/logMessage` / `showMessage` | Logs / popups | **P0** | Build logs go through logMessage |
| `buildTarget/outputPaths` | Artifact paths for debugging | P1 | Cheap and useful |
| `buildTarget/inverseSources` | File → owning target | P1 | IDE "which target owns this file?" |
| `buildTarget/cppOptions` | C++ extension: copts/defines/linkopts | P1 | Fits our model naturally |
| `buildTarget/cleanCache` | Clean target cache | P1 | Delete action records and outputs |
| `workspace/reload` | Reload the build description | P1 | Re-exec self / regenerate graph |
| `buildTarget/didChange` | Target change notification | P2 | Useful in watch mode |
| `buildTarget/run` | Run a target | P2 | Streaming stdout/stderr; M4 |
| `buildTarget/test` | Test a target | P2 | Same |
| `debugSession/start` | DAP debugging | P3 | Not implemented; leave to IDE + debugger |
| `buildTarget/destinations` | Cross-compilation destinations | P3 | Not implemented |
| `dependencySources/modules/resources` | Dependency metadata | P3 | Not needed for small/medium projects |
| JVM / Scala / Cargo / Java / Maven / Python extensions | Other languages | P3 | Not implemented |
| `run/printStdout` / `printStderr` / `readStdin` | Run streams | P2 | Alongside `run` |

### 3.3 C++ Extension

BSP has a dedicated C++ extension with a small set of fields:

```text
CppBuildTarget {
  version: "c++17"
  compiler: "gcc" | "clang" | "msvc"
  cCompiler:  URI
  cppCompiler: URI
}

CppOptionsItem {
  target: BuildTargetIdentifier
  copts:    [String]   // passed through to the compiler
  defines:  [String]   // automatically prefixed with -D / /D
  linkopts: [String]
  linkshared: Boolean
}
```

This maps directly to `Toolchain` and `Target`; implementation cost is low, so
P1 is appropriate.

### 3.4 Client Ecosystem Reality

The BSP implementations page lists servers that are almost entirely in the
JVM/Rust ecosystems (Bloop, sbt, Mill, Bazel-bsp, cargo-bsp, Gradle). Clients
include IntelliJ Scala, IntelliJ-BSP, Metals, scala-cli, VS Code Gradle, and
`bsp.nvim`.

**C++ is not on that list.** More importantly, clangd is not a BSP client; it
only understands `compile_commands.json` / `compile_flags.txt` (clangd issue
#1903 is still discussing BSP support).

Conclusions:

- BSP is not the path to C++ code intelligence; **compdb is**.
- BSP's value is IDE UI for build/run/test/diagnostics and future integration
  with IntelliJ-family tools and Neovim.
- For small/medium C++ projects, BSP is a bonus; first make compdb solid.
- BSP acceptance must not depend on a specific C++ IDE; use `bsp.nvim` or a
  purpose-built probe client for regression tests.

---

## 4. Division of Labor: LSP / BSP / compdb

```text
                ┌──────────────────────────────┐
                │            IDE / Editor      │
                │  clangd (LSP)   BSP client   │
                └───────┬──────────────┬───────┘
                        │              │
        compile_commands.json     BSP (JSON-RPC/stdio)
                        │              │
                ┌───────▼──────────────▼───────┐
                │        ./build (cco)         │
                │  graph + cache + scheduler   │
                └──────────────┬───────────────┘
                               │ argv
                        g++ / clang++ / cl.exe
```

- **LSP**: not implemented. Write `compile_commands.json` at the workspace root
  (clangd searches upward and also looks in `build/`); optionally emit
  `.clangd` / `compile_flags.txt` as a fallback.
- **BSP**: implement a server, but only the build/compile/diagnostics subset;
  `argv` points at `./build bsp`.
- **compdb**: rewrite atomically after configure; use the `arguments` array,
  absolute `directory`, and absolute `file` paths (or paths relative to
  `directory`). clangd supports `arguments` best.
- Do not write a clangd plugin, proxy clangd, or build "our own LSP".

> If BSP + LSP integration is ever required, the right design is a **small
> separate proxy**: speak LSP to the IDE, LSP to clangd, and BSP to `./build`;
> do not rewrite a C++ language server. This proxy is out of scope.

---

## 5. Incremental Engine

### 5.1 Three Approaches

| Approach | Examples | Correctness | Speed | Cost | Problems |
|---|---|---|---|---|---|
| mtime | nob.h, make | Low | Fast | Very low | Clock skew, touch without changes, branch switching, generated files |
| depfile + mtime | make, ninja | Medium | Fast | Low | Command/toolchain changes do not invalidate; header add/remove edge cases |
| Content fingerprint + action cache | Bazel, Buck2 | High | Medium | High | Hashing, state store, declaration of deps; first build is slower |

Recommendation: a **simplified version of approach 3**, keeping `(size,
mtime_ns)` as a fast path so files are not fully hashed on every build.

### 5.2 Action Model

```text
Action {
  id:        stable string (e.g. "compile:src/app/main.cpp")
  kind:     compile | link | custom
  inputs:   [FileRef]          # compile: source + headers from the previous depfile
  outputs:  [path]
  argv:     [string]           # response files already expanded
  cwd:      path
  env:      {KEY: VALUE}       # only variables that affect compilation
  deps:     [ActionId]         # link -> objects; custom dependencies
}
```

Execution order is topological; within a ready layer, a thread pool runs
actions concurrently (`--jobs`, default `hardware_concurrency`).

### 5.3 Fingerprints

```text
fp = SHA256(
  action.kind,
  toolchain.fingerprint,          # compiler absolute path + version + target + sysroot
  normalized(argv),               # normalize path separators/relative paths;
                                  # do **not** reorder arguments (-I/-D order matters)
  cwd,
  sorted(env_subset),
  sorted(input_path + content_hash),
  sorted(output_path)
)
```

- **Input content hash**: SHA-256, read in 1 MiB chunks while hashing.
- **Fast path**: cache `(size, mtime_ns, file_id)` per file; if stat is
  unchanged, reuse the previous hash without reading the file. Rehash only when
  stat changes.
- **Toolchain fingerprint**: `H(compiler absolute path, --version output,
  target triple)`; MSVC also includes `INCLUDE` / `LIB`. Compiler upgrades
  invalidate everything, which is desirable.
- **Command changes**: argv participates, so a `-DDEBUG` change invalidates all
  affected TUs.
- **Build script comment changes**: action argv is unchanged → fingerprint is
  unchanged → the project is not rebuilt (only the self-rebuild of `build`
  costs time). This is better than "build script mtime changed, rebuild
  everything".

### 5.4 Dependency Discovery (depfile)

| Toolchain | Flags | Output | Notes |
|---|---|---|---|
| GCC | `-MD -MF <file> -MT <obj>` | Makefile-style `.d` | `-MD` includes system headers; `-MMD` excludes them |
| Clang | Same | Same | Compatible with GCC |
| MSVC | `/sourceDependencies <file>.json` | JSON | VS 2019 16.7+; older versions fall back to `/showIncludes` |

`.d` parsing must handle backslash line continuations, escaped spaces, `$`
escapes, multiple targets, and duplicate suppression. MSVC `/showIncludes`
output is locale-dependent ("Note: including file:" / localized variants), so
prefer JSON; if a fallback is required, match both English and localized
prefixes and allow user overrides.

**System headers**: use `-MD` (not `-MMD`) so libstdc++ / libc++ headers are
dependencies; otherwise compiler/standard-library upgrades do not trigger a
rebuild. Depfiles become larger and more files are hashed, but the fast path
only stats them. Provide `--no-system-deps` to opt out.

**Generated headers**: a custom action's outputs must be declared explicitly by
downstream targets. A depfile can only discover files that already exist. If a
generated header does not exist on the first build, the compiler will fail;
this is correct, but the action graph must run codegen first. The design
expresses this with `target.depends(custom_action)`.

**Dynamic includes**: `#include MACRO` expands to a real path in the depfile and
is captured. "Switch include paths based on macros" also changes argv and
invalidates. C++20 modules are not supported yet; document the limitation.

### 5.5 State Store and Concurrency

**There are two output layouts:**

- **manual (default)**: used by hand-written projects. Binaries go to `./bin/`
  (`./bin/<target>` / `./bin/<target>.exe`); objects go to
  `./obj/<target>/...`. The build script controls everything and can override a
  single output with `target.output_to()`.
- **managed**: `cco++ new` / `cco new` generate a build script that calls
  `managed_layout()` and uses `.cco/` as shown below. A project-wide custom root
  can be selected with `set_build_root("build")` (`build/bin` + `build/obj`).

```text
.cco/                 # managed root (manual mode has no such directory)
  state.txt           # M2: action records + file hash cache (line format)
  lock
  obj/<target>/<relative-path>.o
  dep/<target>/<relative-path>.d
  bin/<target>[.exe]
```

`state.txt` field semantics (prototype line format; the JSON below is
illustrative only):

```text
CCOSTATE 1
T <toolchain-fingerprint-hex>
F <size> <mtime_ns> <file-hash> <path-hex>
A <action-id-hex> <action-fingerprint>
D <dep-path-hex>
O <output-path-hex>
```

```json
{
  "schema": 1,
  "toolchain": {"id": "...", "version": "..."},
  "actions": {
    "compile:src/app/main.cpp": {
      "fingerprint": "sha256:...",
      "argv": ["/usr/bin/g++", "-std=c++17", "..."],
      "inputs": [
        {"path": "src/app/main.cpp", "hash": "sha256:...", "stat": [1234, 1720000000123456789]}
      ],
      "outputs": [
        {"path": ".cco/obj/app/src/app/main.cpp.o", "hash": "sha256:...", "stat": [5678, 1720000000987654321]}
      ],
      "deps": ["include/foo.h", "..."],
      "last_run_ms": 42,
      "updated_at": 1720000000
    }
  }
}
```

- **Atomic write**: write `state.txt.tmp` → fsync → rename; on Windows use
  `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING)`.
- **Locking**: POSIX `flock`, Windows `LockFileEx`; the CLI waits by default
  (with a timeout), and BSP holds the lock during compile requests.
- **Crash safety**: update records only after an action succeeds. On
  interruption, completed work may be kept; the next run redoes at most the
  unrecorded actions.
- **Cleaning**: managed mode removes the whole build root (default `.cco/`);
  manual mode removes `obj/`, final target outputs, and output parent
  directories that become empty (for example `bin/` after
  `output_to("bin/app")`; preserve directories that still contain other files).
  `--prune` removes cache entries unused for 30 days.
- **Scale**: 10k actions is roughly 10 MB of JSON, parsing in ~50–100 ms. If
  that becomes a bottleneck, switch to a custom binary format; do not introduce
  SQLite (an external dependency).

### 5.6 Scheduling

- Start with two phases: compile everything concurrently → link after all
  compiles complete. This is enough for 95% of projects.
- Full DAG: when custom actions sit in the middle, schedule from a
  dependency-ready queue with a `std::thread` pool + `condition_variable`.
- Failure: stop scheduling new actions, wait for running actions, report the
  failure, do not update the failed action's record, and do not link.
- Cancellation: Ctrl-C / BSP `$/cancelRequest` → mark cancelled and kill the
  child process tree (POSIX process group, Windows Job Object).
- Output: capture each child's stdout/stderr; print atomically when it
  completes to avoid interleaving, or stream with a target prefix in
  `--verbose` mode.

### 5.7 Edge Cases

| Case | Behavior |
|---|---|
| Output deleted | Rerun the action even if the fingerprint matches |
| Source deleted | Remove it from the graph; error if still referenced |
| Header deleted | depfile stat fails → treat as changed → recompile (compiler reports the real error) |
| Header added | Source content changes → recompile → new depfile |
| Command/flag change | Fingerprint changes → recompile |
| Compiler upgrade | Toolchain fingerprint changes → full rebuild |
| Git branch switch causes mtime rollback | Content hash is authoritative; mtime is only a fast path |
| Two modifications within one second | `mtime_ns + size` is the fast-path key; identical nanoseconds and size may be missed; `--verify-hashes` forces full hashing (slow) |
| Case-insensitive filesystem | Case-fold path keys on Windows/macOS; preserve original case for outputs |
| Symlinks | Follow `std::filesystem` semantics; do not follow directory symlinks to avoid cycles |
| Two concurrent `./build` processes | File lock; the second waits or errors |
| Build interrupted | Do not write partial records; clean temporary files |
| Long command line | Write `.cco/rsp/<hash>.rsp` past the threshold and use `@file`; rsp content participates in the fingerprint |
| Response file regenerated | Same content → same fingerprint; path changes do not matter |
| Localized diagnostics | Set `LC_ALL=C` / `LANG=C` for the compiler; use `/sourceDependencies` for MSVC to avoid `/showIncludes` localization |
| ANSI colors | Add `-fdiagnostics-color=never` / `-fno-color-diagnostics`; strip ANSI as a fallback before parsing |

### 5.8 On Daemons

Separate three concepts:

1. **Self-rebuild re-exec**: `./build` notices that `build.cpp` is newer than
   itself → recompiles → `exec`s the new binary. It is short-lived and **not a
   daemon**.
2. **Bazel server**: a long-lived JVM holding the action graph and file state;
   clients connect over gRPC. It starts quickly and reuses work across commands,
   but introduces state consistency, crash recovery, version mismatch, and
   cleanup problems.
3. **BSP server / watch**: a long-lived process required by a protocol or the
   user; BSP lifetime is managed by the client.

Recommendations:

- **No daemon by default.** C++ binaries start in milliseconds; a disk state
  store + stat fast path is already fast enough. In-memory benefits matter mainly
  for very large projects (>100k actions) or instant file-watch feedback.
- `watch` and `bsp` are **optional long-running modes** that reuse the same
  engine and the same state cache; do not introduce a second state store.
- If a daemon is ever added, treat it only as a "cache + scheduling"
  accelerator. **Correctness still comes from the disk state and content
  fingerprints**; a crashed, stale, or killed daemon must fall back cleanly to a
  one-shot process.
- Do not adopt Bazel's "you must connect to the server to build" model; it
  destroys compiler-only simplicity.

### 5.9 Performance Budget

| Scenario | Target |
|---|---|
| `./build` startup + load 1k action states | < 100 ms |
| No-op build over 1k files | < 150 ms (excluding self-rebuild check) |
| One source change | One compile + one link (if the output changes) |
| One header change | Recompile only dependent TUs |
| `--dry-run` plan output | < 200 ms |
| Self-rebuild of `build.cpp` (including `cco.hpp`) | < 1.5 s (g++/clang -O1; -O2 can take 2–3 s) |

If self-rebuild becomes painful: split `cco.hpp` into "core" and "BSP/JSON"
sections and compile out the latter with `CCO_NO_BSP`; or default self-rebuild
to `-O1`. Do not add precompiled headers (unreliable across compilers).

---

## 6. Overall Architecture

### 6.1 Form and Bootstrap

**Form**: a single-header library included by the user's build source. The
compiled build source is the tool itself. The build source can have any
filename; `CCO_GO_REBUILD_URSELF` locates it with `__FILE__`. The C++ runtime
header is `cco.hpp`; the C11 runtime header is `cco.h`. The C++ seed is
`cco.cpp` and builds `cco++`; the C seed is `cco.c` and builds `cco`.

```cpp
// build.cpp
#define CCO_IMPLEMENTATION
#include "cco.hpp"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);   // recompile and re-exec if build.cpp / cco.hpp changed

    cco::Workspace ws(argc, argv);

    cco::Target &core = ws.library("core")
        .sources(cco::glob("src/core/**/*.cpp"))
        .include("include")
        .standard("c++17");

    cco::Target &app = ws.executable("app")
        .sources(cco::glob("src/app/**/*.cpp"))
        .link(core)
        .include("include");

    cco::Target &examples = ws.test("examples")
        .sources(cco::glob("examples/**/*.cpp"))
        .link(core);

    return ws.run();   // dispatch: build / run / test / compdb / bsp / watch / clean / explain
}
```

**Bootstrap flow**:

```text
$ c++ -std=c++17 build.cpp -o build        # first time only (documented in README)
$ # Windows/MSVC: cl /nologo /std:c++17 /O2 /Fe:build.exe build.cpp
$ ./build
  ├─ CCO_GO_REBUILD_URSELF
  │    ├─ stat(build) vs stat(build.cpp) + stat(cco.hpp)
  │    ├─ stale: mv build build.old
  │    │         c++ -std=c++17 build.cpp -o build
  │    │         failure → mv build.old build; exit 1
  │    └─ success: exec ./build "$@"   # POSIX execv; Windows spawn + wait + propagate exit code
  ├─ load .cco/state.txt
  ├─ glob → target graph → action graph
  ├─ topological execution: fingerprint hit + output exists → skip; otherwise run
  └─ atomically write state.txt
```

`CCO_HEADER_FILE` is defined inside `cco.hpp` as `__FILE__`, so the header can
live in a subdirectory and still be checked correctly. `CCO_REBUILD_URSELF` is
redefinable (for example MSVC's `cl /std:c++17 /O2 /Fe:...`, or extra
`-DCCO_...` flags).

**The flags used for the first compile cannot be inferred automatically**, a
limitation nob.h shares. The documentation recommends:

- Default `CCO_REBUILD_URSELF` uses `c++ -std=c++17 -O2`.
- Redefine `CCO_REBUILD_URSELF` when extra flags are needed.
- Do not attempt to infer "the user compiled build with `-DDEBUG`, so
  self-rebuild must preserve it"; reading argv/env is possible, but this is
  magic we avoid.

### 6.2 Layers (Still One Header, Organized in Sections)

```text
cco.hpp
  ├─ config / macros
  ├─ platform      : Win32 / POSIX wrappers (process, lock, atomic rename, UTF-8)
  ├─ base          : string, path, log, panic, temp, hash (SHA-256)
  ├─ fs            : mkdir/copy/remove/read_dir/walk/glob
  ├─ process       : Subprocess (argv, cwd, env, pipe, capture, kill)
  ├─ toolchain     : GNU / MSVC / clang-cl dialects, versions, dep flags, diagnostics
  ├─ depfile       : .d / MSVC JSON parsing
  ├─ diagnostics   : GCC/Clang/MSVC stderr → Diagnostic
  ├─ json          : parse + serialize (BSP / state cache)
  ├─ graph         : Target / Action / topological sort
  ├─ cache         : fingerprint / state.txt / lock
  ├─ scheduler     : thread pool + dependency scheduling
  ├─ cli           : subcommands, argument parsing, colors
  ├─ bsp           : JSON-RPC framing, lifecycle, method dispatch
  └─ rebuild       : GO_REBUILD_URSELF
```

Optional compile-time trimming: `CCO_NO_BSP`, `CCO_NO_COLOR`.

`cpp/cco.cpp` is not part of `cco.hpp`; it only provides `new` / `init` in the
seed binary, and generated projects do not include it.

The M2 prototype uses the line format `state.txt` to avoid a JSON parser just
for the cache. If BSP or diagnostics need JSON later, reuse the same schema.

### 6.3 API Draft

```cpp
namespace cco {

struct Toolchain {
    enum class Kind { Gnu, Clang, Msvc, ClangCl };
    Kind kind;
    fs::path cxx, cc;
    std::string version;
    static Toolchain detect();                 // from CXX / --cxx / PATH
};

struct Target {
    Target &sources(std::vector<fs::path>);
    Target &include(std::string dir);          // propagated to downstream links
    Target &define(std::string kv);
    Target &standard(std::string);             // "c++17"
    Target &cxxflag(std::string);
    Target &linkflag(std::string);
    Target &link(Target &other);
    Target &depends(Action &a);
    Target &output(fs::path);
};

struct Workspace {
    Workspace(int argc, char **argv);
    Target &executable(std::string name, std::vector<fs::path> sources = {});
    Target &library(std::string name, std::vector<fs::path> sources = {});
    Target &shared_library(std::string name, std::vector<fs::path> sources = {});
    Target &test(std::string name, std::vector<fs::path> sources = {});
    Workspace &managed_layout();              // .cco/bin + .cco/obj
    Workspace &set_build_root(fs::path dir);  // custom artifact root
    Action &custom(std::string name, std::vector<std::string> argv,
                   std::vector<fs::path> inputs, std::vector<fs::path> outputs);

    int run();       // command dispatch (build / run / test / ...)
    int bsp();       // stdio BSP server
    int watch();     // polling / file watching
    int compdb();    // write compile_commands.json
};

int init_main(int argc, char **argv);   // independent entry point for new / init

std::vector<fs::path> glob(std::string pattern);   // ** / * / ?
std::string sha256(std::string_view);
}
```

Design principles:

- **Declarative first, imperative escape hatch**: targets express normal
  compile/link; `custom` expresses codegen.
- **No hidden global state**: `Workspace` owns all state; `glob` is a pure
  function.
- **Do not parse C++**: the build description is compiled C++; no DSL parser is
  needed.
- **Paths are always `std::filesystem::path`; internal strings are UTF-8**
  (converted once on Windows).

### 6.4 CLI

```text
./build                          # build default targets
./build app examples             # build specific targets
./build run app -- --flag        # build and run
./build test                     # build and run all test targets
./build compdb                   # generate compile_commands.json
./build ide-setup                # write .bsp/ + compile_commands.json
./build bsp                      # BSP server (stdio)
./build watch                    # watch + incremental
./build clean                    # managed: remove build root; manual: remove obj/ and binaries
./build explain app              # explain why each action runs or is skipped
./build graph --format=dot       # output the dependency graph
./build --jobs 8 --dry-run --verbose --json --no-color
```

Subcommands and `run()`: `run()` first performs the self-rebuild check, then
parses `argv[1]`; `build` is the default when no subcommand is given.

Scaffolding does not live in `build`; it is provided by the separate seeds
(see §6.11):

```text
cco++ new <path>   [--bin|--lib] [--name N] [--vcs none|git] [--ide] [--build] [--dry-run] [--force] [--quiet]
cco++ init [path]  ...
cco   new <path>   ...   # C seed; same command shape
```

### 6.5 Repository Layout (This Project)

```text
compiler-only/
  c/
    cco.h
    cco.c
    examples/
  cpp/
    cco.hpp
    cco.cpp
    examples/
      000_hello_world/
      001_output_customization/
      002_static_library/
      003_shared_library/
      004_raylib/
      005_protobuf/
  docs/
    design.md               # this document
  tools/
    README.md               # reserved for cco_from_* import helpers
  .github/workflows/ci.yml
```

### 6.6 Compiler Dialects

| Concept | GCC | Clang | MSVC |
|---|---|---|---|
| C++ standard | `-std=c++17` | `-std=c++17` | `/std:c++17` |
| C standard | `-std=c11` | `-std=c11` | `/std:c11` |
| Defines | `-D` | `-D` | `/D` |
| Include | `-I` | `-I` | `/I` |
| Compile to obj | `-c -o` | `-c -o` | `/c /Fo:` |
| Executable | `-o` | `-o` | `/Fe:` |
| Shared library | `-shared -fPIC` | `-shared -fPIC` | `/LD` |
| Dependencies | `-MD -MF` | `-MD -MF` | `/sourceDependencies` |
| Warnings | `-Wall -Wextra` | `-Wall -Wextra` | `/W4 /permissive-` |
| Debug | `-g` | `-g` | `/Zi /DEBUG` |
| Runtime | — | — | `/MD` (default) or `/MT` |
| Exceptions | — | — | `/EHsc` |
| UTF-8 | `-finput-charset=UTF-8` | Same | `/utf-8` |
| Color | `-fdiagnostics-color=never` | `-fno-color-diagnostics` | None |
| Response files | `@file` | `@file` | `@file` |

Notes:

- **MSVC requires a vcvars environment.** `cl.exe` depends on `INCLUDE` / `LIB`
  / `PATH`; cco does not call `vcvarsall.bat` (a shell script). Require running
  from a Developer Command Prompt, or use `clang-cl`. This is the most practical
  "MSVC only" limitation and must be documented.
- **GCC 8/9** may need `-lstdc++fs` when linking `std::filesystem`; detect old
  GCC and add it automatically (or require GCC 9+).
- **clang-cl** is treated as a separate MSVC-compatible dialect: `/`-style
  arguments; whether dependency output uses `-MD` or `/sourceDependencies` must
  be confirmed in M2 testing.

### 6.7 Static Libraries and the `ar` Problem

"Only the g++/clang++/msvc driver" means `ar` / `ranlib` / `lib.exe` cannot be
used (`lib.exe` is part of the MSVC toolchain but not a compiler driver).

Approach:

- **`library()` is an object group by default**: a set of objects and compile
  options linked directly into `executable()` targets; no `.a` / `.lib` is
  produced.
- When a static library must be delivered to external consumers, provide an
  optional `--archive` that explicitly calls `ar` / `lib.exe` and document it as
  an exception that breaks the compiler-only constraint.
- Linking against third-party prebuilt `.a` / `.lib` is fine: pass the file to
  the linker; `ar` is not needed.
- **`shared_library()` is implemented (GCC/Clang)**: compile with `-fPIC`, link
  with `-shared` (macOS uses `-dynamiclib`), and automatically add `-L`, `-l`,
  and rpath (`$ORIGIN` on Linux, `@loader_path` on macOS) for consumers. The
  MSVC DLL/import-library path is not implemented; the prototype reports an
  explicit error.

### 6.8 MSVC Realities

1. **Environment**: as above, vcvars is required.
2. **Dependency output**: `/sourceDependencies` is VS 2019 16.7+; older versions
   only have `/showIncludes`, which is locale-sensitive.
3. **Command-line length**: 32K limit; automatic response files are mandatory.
4. **Paths**: UTF-16; `file:///C:/...` URIs; normalize backslashes to forward
   slashes before keying.
5. **Output location**: `cl /c` writes `.obj` to the current directory by
   default; always pass `/Fo:`.
6. **PDB**: `/Zi` + `/DEBUG` produces `.pdb`; incremental linking
   (`/INCREMENTAL`) is optional. Disable incremental linking by default to keep
   state simple.
7. **Diagnostics**: `file(line,col): error C1234: message`, plus
   `fatal error C1083`; linker errors use a different format.
8. **`/showIncludes` vs `/sourceDependencies`**: choose one; prefer the latter
   to avoid locale and parsing problems.

### 6.9 BSP Integration

`.bsp/compiler-only.json` (generated by `./build ide-setup`, which also writes
`compile_commands.json`):

```json
{
  "name": "compiler-only",
  "version": "0.1.0",
  "bspVersion": "2.2.0",
  "languages": ["cpp", "c"],
  "argv": ["./build", "bsp"]
}
```

`build/initialize` returns capabilities (target shape; M3 first ships only
`compileProvider`):

```json
{
  "displayName": "compiler-only",
  "version": "0.1.0",
  "bspVersion": "2.2.0",
  "capabilities": {
    "compileProvider": {"languageIds": ["cpp", "c"]},
    "canReload": true,
    "outputPathsProvider": true,
    "inverseSourcesProvider": true,
    "buildTargetChangedProvider": true
  }
}
```

> M3 declares only implemented capabilities. `runProvider` / `testProvider`
> are added when M4 makes them real. Once a capability is advertised, clients
> will call it; do not promise early.

A single target in `workspace/buildTargets` (draft):

```json
{
  "id": {"uri": "cco:/app"},
  "displayName": "app",
  "baseDirectory": "file:///home/u/proj",
  "tags": ["application"],
  "languageIds": ["cpp"],
  "dependencies": [{"uri": "cco:/core"}],
  "capabilities": {"canCompile": true, "canRun": true, "canTest": false},
  "dataKind": "cpp",
  "data": {
    "version": "c++17",
    "compiler": "gcc",
    "cCompiler": "file:///usr/bin/gcc",
    "cppCompiler": "file:///usr/bin/g++"
  }
}
```

Implementation order:

1. JSON-RPC framing + `initialize` / `initialized` / `shutdown` / `exit`.
2. `workspace/buildTargets` + `buildTarget/sources`.
3. `buildTarget/compile` + task notifications + diagnostics.
4. `buildTarget/cppOptions` / `outputPaths` / `inverseSources` / `cleanCache`.
5. `run` / `test` / `reload` (M4).

**stdout discipline**: in BSP mode all logs go to stderr; `printf` is allowed
only inside JSON frames. If self-rebuild happens during startup, its logs must
also go to stderr, and failures must exit quickly so clients do not hang.

### 6.10 C++17 Trade-offs

`std::filesystem` is the biggest advantage over nob.h, but it has pitfalls:

- **Error handling**: use `error_code` overloads; engine APIs return `bool` plus
  an error message and do not let exceptions cross the build script. User code
  may still use exceptions freely.
- **Path encoding**: use UTF-16 Win32 APIs internally on Windows and UTF-8 at
  the boundary. C++17's `path::u8string()` returns `std::string`; C++20 changes
  it to `std::u8string`. Isolate with `to_utf8()` / `from_utf8()`.
- **mtime**: `last_write_time` has an unspecified epoch but is comparable within
  one platform; store it as an opaque integer in the stat cache. For nanosecond
  precision use platform `stat` (POSIX `st_mtim`, Windows
  `GetFileInformationByHandleEx`).
- **file id**: no portable API; POSIX uses `(st_dev, st_ino)`, Windows uses
  `(volume serial, file index)`. Fetch only when needed to avoid an extra
  syscall every time.
- **glob**: the standard library has no glob; implement `*`, `?`, `**` on top of
  `recursive_directory_iterator`, excluding `.cco/`, `.git/`, and output
  directories by default; sort results for determinism.
- **What we avoid**: `<regex>` (slow to compile and run), `<iostream>` (static
  initialization, binary size), hot-path `std::function`, RTTI. Use `fprintf`,
  `std::string`, and function pointers.
- **Compile time**: `<filesystem>`, `<thread>`, and `<mutex>` are heavy; they
  dominate the self-rebuild budget. Trim BSP/JSON with `CCO_NO_BSP` when
  necessary.
- **ABI**: do not share binaries across compilers; everything is compiled in
  `build.cpp`, avoiding ABI issues.

### 6.11 Scaffolding: `new` / `init`

Goal: one command generates a minimal project that **compiles and runs
immediately**, not a tree of empty directories.

#### Commands

```text
cco++ new <path>   [--bin|--lib] [--name N] [--vcs none|git] [--ide] [--build] [--dry-run] [--force] [--quiet]
cco++ init [path]  [--bin|--lib] [--name N] [--vcs none|git] [--ide] [--build] [--dry-run] [--force] [--quiet]
```

`cco++` / `cco` are separate seed binaries; `cco.hpp` / `cco.h` contain only
the runtime, and generated `build` binaries do not include `new` / `init`.

- `new`: the directory must not exist; create it and generate the project.
- `init`: generate in an existing directory (default `.`). If `build.cpp`
  exists, error unless `--force`; other scaffold files are skipped with a
  warning by default and overwritten only with `--force`; sources under `src/`,
  `include/`, and `examples/` are never overwritten.
- `--bin` / `--lib`: executable (default) or library template.
- `--name`: target name; defaults to the directory name. Restrict to
  `[A-Za-z0-9._-]+`, must not start with `-`, and must not be `.` / `..` (to
  prevent template injection and path ambiguity); the C++ identifier is
  sanitized separately (`my-app` → `my_app`).
- `--vcs none|git`: default `none`. `git` only runs `git init` (an optional
  external tool, explicitly documented as an exception); skip if `.git` already
  exists; run no other VCS commands.
- `--ide`: additionally write `.bsp/compiler-only.json` (Unix uses `./build`,
  Windows uses `build.exe`).
- `--build`: after writing, compile `build.cpp` with the detected toolchain and
  spawn the generated `build` directly (without a shell).
- `--dry-run`: print files that would be created/overwritten without writing.
- `--quiet`: suppress file lists and next-step output; errors still print.
- `--force`: overwrite scaffold files such as `build.cpp` / `cco.hpp` / `README`
  / `.gitignore`; never overwrite source directories; never delete user files.

> `--ide` writes `.bsp/compiler-only.json` pointing at `./build bsp`; the first
> bootstrap must succeed before an IDE can connect.

#### Directory Layout (Executable)

```text
myapp/
├── cco.hpp              # copied from the current tool, same version
├── build.cpp            # build description
├── .gitignore
├── README.md
├── compile_flags.txt    # clangd bootstrap fallback
└── src/
    └── main.cpp
```

`compile_flags.txt` for an app is just `-std=c++17`. clangd works before the
first `./build compdb`; once `compile_commands.json` exists, it is ignored.

#### Directory Layout (Library)

```text
mylib/
├── cco.hpp
├── build.cpp
├── .gitignore
├── README.md
├── compile_flags.txt    # -std=c++17 / -Iinclude
├── include/
│   └── mylib/
│       └── mylib.h
├── src/
│   └── mylib.cpp
└── examples/
    └── mylib_test.cpp
```

#### Template Contents

`build.cpp` (app):

```cpp
// Generated by compiler-only {{version}}
#define CCO_IMPLEMENTATION
#include "cco.hpp"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);
    cco::Workspace ws(argc, argv);
    ws.managed_layout();

    [[maybe_unused]] auto &app = ws.executable("{{name}}")
        .sources(cco::glob("src/**/*.cpp"));

    return ws.run();
}
```

`build.cpp` (lib):

```cpp
// Generated by compiler-only {{version}}
#define CCO_IMPLEMENTATION
#include "cco.hpp"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);
    cco::Workspace ws(argc, argv);
    ws.managed_layout();

    [[maybe_unused]] auto &lib = ws.library("{{name}}")
        .sources(cco::glob("src/**/*.cpp"))
        .include("include");

    [[maybe_unused]] auto &examples = ws.test("{{name}}_examples")
        .sources(cco::glob("examples/**/*.cpp"))
        .link(lib);

    return ws.run();
}
```

`src/main.cpp`:

```cpp
#include <cstdio>

int main() {
    std::printf("Hello from {{name}}!\n");
    return 0;
}
```

The library template's `examples/{{ident}}_test.cpp` uses a `CHECK` macro plus a
return code; no test framework is introduced. `./build test` builds and runs all
test targets.

`.gitignore`:

```text
.cco/
/build
/build.exe
/build.old
/compile_commands.json
*.o
*.obj
*.pdb
*.ilk
```

`README.md` documents only three things: the first bootstrap command (one line
for GCC/Clang and one for MSVC), `./build` / `./build test` / `./build compdb`,
and the directory layout.

#### Source of `cco.hpp`

- By default, copy from the running tool: prefer `--cco-header`, then the
  compile-time `CCO_HEADER_FILE`, then search near the executable directory /
  `CCO_SOURCE_DIR`.
- If the target exists, compare SHA-256: skip if identical; error if different
  unless `--force`; skip when source and target are the same file.
- **No network, no package manager, no global install directory**; every project
  vendors its own copy for self-containment.
- **Upgrade path**: replace the old `cco.hpp` with the new one; do not use
  `init --force` to upgrade a project (it would overwrite `build.cpp` and
  sources).

#### First Use (Seed)

`init` needs a working `build` binary first, a chicken-and-egg problem. The
approach:

1. **`cpp/cco.cpp` / `c/cco.c`**: the seed entry point and scaffold
   implementation live in one file; compile once to get `cco++` / `cco`:

   ```cpp
   // cpp/cco.cpp (excerpt)
   #define CCO_IMPLEMENTATION
   #include "cco.hpp"
   // ... init_main, templates, new/init implementation ...
   int main(int argc, char **argv) { return cco::init_main(argc, argv); }
   ```

   ```console
   $ c++ -std=c++17 cpp/cco.cpp -o cco++
   $ ./cco++ new myapp
   ```

   `cco++` can live at the repository root; the header is at `cpp/cco.hpp`, and
   `init` finds it through the compile-time `CCO_HEADER_FILE` path. Use
   `--cco-header cpp/cco.hpp` when moving the binary.

   After that, use `cco++ new` for new projects; `cco++` is not a build
   dependency and may be deleted. The generated `build` only builds; it does not
   provide `new` / `init`.

   Why the seed and scaffold share one file: `init_main` and `main` are in the
   same file, so one compile produces the complete CLI; the repository does not
   maintain a separate seed unit-test entry point.

#### Why Not a Full Copy of `cargo`

- No `git init` by default: it would introduce an external tool and break
  "compiler only". `--vcs git` is explicit opt-in.
- No lockfile or dependency manifest: there is no package manager.
- Templates are embedded as raw strings in `cpp/cco.cpp` / `c/cco.c`; no
  template directory is required, and `cco.hpp` / `cco.h` remain pure runtime.
- The generated project is a minimal buildable project, not an empty directory
  tree.

#### Implementation Notes

1. Validate arguments: `new` path does not exist; `--name` is valid;
   `--lib` / `--bin` are mutually exclusive.
2. Resolve the header source (see above), read it, and compare (prototype uses
   byte comparison; SHA-256 can replace it later).
3. Build an in-memory `path -> content` plan; detect conflicts.
4. `--dry-run` prints the plan and returns.
5. Create parent directories and write files (temporary file + rename),
   preserving default permissions.
6. Optionally run `git init` / `--build`; a failed `--build` does not roll back
   written files (the error includes the retry command).
7. Print the `+` file list and next steps (platform-specific bootstrap command).

#### Acceptance

- On all three compilers, generated app/lib projects succeed with their first
  `c++ build.cpp -o build && ./build`.
- `--lib` passes `./build test`; `./build compdb` produces a file usable by
  clangd.
- Repeating `new` errors; `--force` overwrites; `--dry-run` writes nothing.
- Name sanitization, Windows reserved names, non-empty directories, and
  `cco.hpp` version conflicts have tests.

---

## 7. Roadmap

| Phase | Content | Acceptance |
|---|---|---|
| **M0 Skeleton** (1–2 weeks) | `cco.hpp` infrastructure: log, fs, process, cmd, hash, temp; GNU/Clang/MSVC dialects; hello world; self-rebuild re-exec; CI matrix | Linux/macOS/Windows × gcc/clang/msvc all produce an executable with `c++ build.cpp -o build && ./build`; changing `build.cpp` recompiles automatically |
| **M1 Graph and Execution** (2–3 weeks) | target graph, glob, compile/link, DAG scheduling, state cache (mtime first), `--explain`, `clean`, `--dry-run`, `test`, `new` / `init` | One source change rebuilds one TU; link runs only when needed; generated app/lib build on all three platforms; `./build test` works |
| **M2 True Incrementality** (2–3 weeks) | content fingerprints, SHA-256, stat fast path, depfiles (GCC/Clang/MSVC), toolchain fingerprints, compdb, clangd integration, response files | Changing a build-script comment does not rebuild; a header change rebuilds only dependent TUs; `-D` changes rebuild everything; compiler upgrades rebuild everything; clangd reports no errors; no-op < 150 ms |
| **M3 BSP** (2–3 weeks) | JSON-RPC, initialize, buildTargets/sources/compile, diagnostics, cppOptions, connection file, probe tests | `bsp.nvim` or a custom probe can connect, list targets, trigger builds, receive diagnostics; stdout has no stray output |
| **M4 watch / run** (2 weeks) | polling watcher, streaming BSP notifications for run/test, cancellation | `./build watch` rebuilds incrementally on file changes; `./build run app` forwards stdout/stderr and arguments |
| **M5 Importers** (2–3 weeks) | `compile_commands.json` → `build.cpp`; CMake File API; `make -n` fallback | A real small CMake project generates a buildable `build.cpp`, with a report for untranslatable parts |
| **M6 Hardening** (ongoing) | locking, atomic writes, crash recovery, fuzzing, docs, performance, platform edge cases | Concurrent builds do not corrupt state; interruption recovers; tests cover three compilers |

Dependencies: M2 is core; M3 depends on M2 diagnostics and JSON; M5 can run in
parallel but has the lowest priority.

> Current progress: both C++ and C11 M2 cores are implemented: content
> fingerprints, SHA-256, stat fast path, GCC/Clang depfiles, MSVC
> `/sourceDependencies`, state cache, and `--explain`. Still missing: response
> files, file locking, parallel scheduling, custom actions / full DAG, and BSP;
> builds remain sequential.

---

## 8. Non-Goals

> One-line positioning: one graph and cache more than nob.h, one generator and
> external tool less than CMake/Ninja.

- No template marketplace, network download, or global installation; templates
  live in the seed, and each project vendors `cco.hpp` / `cco.h`.
- No package manager or dependency resolution (vcpkg, Conan, etc.).
- No remote cache or remote execution.
- No sandbox (Bazel-style isolation).
- No C++20 modules (document the limitation first).
- No cross-compilation toolchain management (only the local compiler;
  `--cxx` / `--cc` may point at a path).
- No custom LSP, IDE plugin, or GUI.
- No perfect translation of arbitrary Makefiles / CMakeLists; importers are
  migration helpers, not compatibility layers.
- No distributed builds.

---

## 9. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Single header makes build-script compilation slow | Slower build-script iteration | Split sections + `CCO_NO_BSP`; default `-O1`; measure and optimize |
| Incremental correctness is hard | Users distrust it; dirty builds | Extensive fixture tests; `--explain`; conservative invalidation (rebuild more rather than less) |
| depfile misses dependencies | Underbuilding | `-MD` includes system headers; custom actions declare inputs; document dynamic-include limits |
| MSVC environment/localization | Poor Windows experience | Require vcvars; prefer `/sourceDependencies`; cover in CI |
| Few BSP clients for C++ | Low return on investment | BSP subset only; compdb first; do not gate acceptance on a specific IDE |
| Command-line length / response files | Windows build failures | Automatic rsp; rsp content participates in fingerprints |
| Self-rebuild race | build.cpp edited during a build | Hold a lock during self-rebuild; roll back `.old` on failure |
| Template/`cco.hpp` version drift | Generated projects behave differently | Version stamp + README version + upgrade notes; templates ship with the header |
| `init` overwrites user files | Data loss | Conflict detection rejects by default; `--force` only; never delete files |
| Windows reserved names / invalid paths | Generation failures | Validate `CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`, trailing dots/spaces; test coverage |
| `--vcs git` introduces an external dependency | Breaks compiler-only | Default `none`; explicit opt-in; warn and continue if git is absent |
| IDE file-watcher conflicts | Duplicate builds | Watch debounce; BSP builds only on request |
| Scope creep (becoming Bazel) | Never finishes | Hold the non-goals; ship M0–M2 first |

---

## 10. Open Decisions

1. **Form**: single-header embedded in `build.cpp` (recommended), or a separate
   `cco` executable plus a JSON config?
   - Recommended: the former best fits compiler-only and requires no extra tool
     installation; the latter can be an optional launcher later.
2. **BSP depth**: implement only the compile/diagnostics subset in M3
   (recommended), or skip BSP and make compdb solid first?
   - Recommended: M3 subset, after M2, not blocking the main line.
3. **Incremental strategy**: go straight to content fingerprints + depfiles
   (recommended), or validate with mtime first and upgrade later?
   - Recommended: M1 uses mtime to get the graph/scheduling working; M2 switches
     to fingerprints without changing the interface.
4. **Static libraries**: object groups by default (recommended), or always
   produce `.a` / `.lib` (requires `ar` / `lib.exe`, breaking the constraint)?
   - Recommended: object groups by default; `--archive` is an explicit
     exception.
5. **Naming**: use `cco.hpp` + `namespace cco` + user binary `build` for now;
   alternatives are `cobuild.h` / `namespace cob`. If renaming, decide before
   M2.
6. ~~**`cco.hpp` distribution**~~: confirmed one vendored copy per project.
7. ~~**Seed style**~~: confirmed `cpp/cco.cpp` builds `cco++`; `c/cco.c` builds
   `cco`.
8. ~~**Default VCS**~~: confirmed `none`; `--vcs git` is explicit opt-in.
9. ~~**Template set**~~: confirmed app + lib; `multi` is out of scope for now.
10. ~~**Scaffold location**~~: confirmed `cco.hpp` / `cco.h` are runtime only;
    `new` / `init` live in the seeds.

---

## 11. References

- nob.h repository: <https://github.com/tsoding/nob.h> (v3.10.1, 3404 lines)
- nob.h source: <https://raw.githubusercontent.com/tsoding/nob.h/main/nob.h>
- BSP specification: <https://build-server-protocol.github.io/docs/specification> (2.2.0)
- BSP server discovery: <https://build-server-protocol.github.io/docs/overview/server-discovery>
- BSP implementations: <https://build-server-protocol.github.io/docs/overview/implementations>
- BSP C++ extension: <https://build-server-protocol.github.io/docs/extensions/cpp>
- clangd project setup (`compile_commands.json` / `compile_flags.txt`):
  <https://clangd.llvm.org/installation#project-setup>
- clangd BSP discussion (issue #1903): <https://github.com/clangd/clangd/issues/1903>
- compile_commands.json specification: <https://clang.llvm.org/docs/JSONCompilationDatabase.html>
