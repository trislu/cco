# 002 — C object-group library

A C "static library" is a group of objects linked directly into consumers;
cco does not invoke `ar`/`lib.exe`.

```sh
cc -std=c11 -O2 build.c -o build
./build run app
./build test
```
