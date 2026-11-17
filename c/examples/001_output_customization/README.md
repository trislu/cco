# 001 — C output customization

Manual C projects default to `bin/`; this example overrides the final path:

```sh
cc -std=c11 -O2 custom_build.c -o build
./build run custom-output
```

The binary is `dist/custom-output`; objects remain in `obj/`.
