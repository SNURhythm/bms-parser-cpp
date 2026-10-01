# encoding_c static library

This package exposes encoding_c's existing C ABI as a Rust `staticlib`.
It contains no decoder implementation; the crate and its encoding_rs dependency
are pinned by Cargo.lock. `panic = "abort"` is required by encoding_c because
unwinding across the C ABI is unsupported.

The root Makefile runs `cargo build --release --locked` and places the library
under `build/encoding_c`. `src/EncodingC.h` in the parser declares the small
subset of encoding_c 0.9.8's ABI used by the adapter. When updating the lockfile,
verify these declarations against upstream and run `make test_ms932`.

Upstream encoding_c and encoding_rs are licensed Apache-2.0 OR MIT:
- https://github.com/hsivonen/encoding_c
- https://github.com/hsivonen/encoding_rs

To inspect the native system libraries required by your Rust target:

```sh
cargo rustc --release --locked --manifest-path third_party/encoding_c/Cargo.toml \
  --target-dir build/encoding_c -- --print native-static-libs
```
