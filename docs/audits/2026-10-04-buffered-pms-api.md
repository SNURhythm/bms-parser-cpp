# Buffered PMS source identity — 2026-10-04

Starting revision: `bb8658a3d68f06c11b4ce4ff0d6c9b91055f2961`.

The existing `popn.pms` fixture reproduces the byte-input format loss:
file Parse/Scan select 9K, while unhinted byte Parse/Scan select 10K.
A regression assertion for buffered mode 9 failed with actual mode 10 before
implementation (`make test`).

Added overloads retain the old byte entry points and append
`const std::filesystem::path &sourcePath` after the cancellation argument.
Only the source name's final, case-insensitive `.pms` extension changes mapping.
The name causes no I/O and does not set metadata paths. Applications must supply
an archive's inner entry name or a document's logical display filename, then
assign their own path metadata. Old byte callers still select BMS.
File APIs delegate to the same implementation and retain their path metadata.

Regression coverage compares complete semantic chart snapshots (including
lanes, reciprocal LN references, counts and timing) and Scan metadata for
`.pms`, `.PMS`, `.PmS`, and Android tree logical names. Empty hints, BMS names,
an outer `.pms` directory containing BMS, and archive filenames retain BMS.

Verification:

- `make clean && make test && make test_amalgamation`: passed, including the
  performance-limit executable and Python reference adapter tests.
- Amalgamated `test/main.cpp` with Clang `-O1 -g -fsanitize=address,undefined
  -fno-sanitize-recover=all`: passed.
- No new Java-reference differential run: this change routes the existing PMS
  semantics to byte callers and deliberately preserves unhinted BMS behavior.

Generated SHA-256:

- `build/bms_parser.hpp`: `04e16303be71dbb5e6111346d2f43e8413132132070d8937a0d63115ffb90c8a`
- `build/bms_parser.cpp`: `3ef896b5debec37a826d9a577b44c801fc77e06dd8e13ef12dcb60f439751a1a`
