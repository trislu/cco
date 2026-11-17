# 005 — third-party: protobuf

Generates C++ sources with `protoc`, then compiles them together with a small
hand-written program and links `libprotobuf`.

## Dependency

- a C++ protobuf runtime (`libprotobuf`)
- `protoc` on `PATH`, or set `PROTOC=/path/to/protoc`
- `PROTOBUF_DIR` pointing at a prefix containing `include/` and `lib/`
  (defaults to `/usr`)

```sh
export PROTOBUF_DIR=/usr
c++ -std=c++17 build.cpp -o build
./build run protobuf_demo
```

`generated/` is created on the first build and ignored by git. This example
uses `protoc` as an external code generator; it is a third-party integration
example, not part of cco's compiler-only core. It is not built by the default
CI matrix.
