# bms-parser-cpp [WIP]

C++ implementation of Be-Music Script parser 

WIP: Parser interface may change. This is quite functional though; you can use it right away.

You can get amalgamated code from [releases](https://github.com/SNURhythm/bms-parser-cpp/releases), or you can build it by yourself by running `make amalgamate` in the root directory.

## Build and text dependencies

The parser uses [simdutf](https://github.com/simdutf/simdutf) for ASCII scanning
and strict UTF-8 validation, and [encoding_c](https://github.com/hsivonen/encoding_c)
(encoding_rs) for Shift-JIS conversion. Install a C++17 compiler, simdutf's
development headers/library (9.0 or newer), pkg-config, and stable Rust/Cargo
(Rust 1.88 or newer).

On macOS, the dependencies are available with `brew install simdutf pkg-config rust`.
Then run:

```sh
make
make test test_amalgamation
make test_ms932                 # Also requires Java 11+ and Python 3
```

Make obtains simdutf flags from pkg-config and builds a static encoding_c library
from the pinned [Cargo manifest and lockfile](third_party/encoding_c/Cargo.toml).
The first Cargo build needs network access; subsequent builds can use its cache.
The optional nightly-only encoding_rs `simd-accel` feature is not enabled.

For a custom simdutf installation, set `SIMDUTF_CFLAGS` and `SIMDUTF_LIBS`.
`CPPFLAGS`, `LDFLAGS`, and `LDLIBS` are also respected. Link applications with
simdutf and `build/encoding_c/release/libbms_encoding_c.a`, plus the Rust native
system libraries for the target. `RUST_NATIVE_LIBS` can override the Makefile's
platform defaults. The Rust library must target the same architecture and ABI as
the C++ compiler. For cross-compilation, set Cargo's `CARGO_BUILD_TARGET` and
override `ENCODING_C_LIB` with the target-specific archive path (for example,
`build/encoding_c/aarch64-unknown-linux-gnu/release/libbms_encoding_c.a`).
The Makefile uses GNU-style C++ flags; on Windows/MinGW, use a matching Rust GNU
target. An MSVC build needs its native build flags and the resulting `.lib` file.

Amalgamation still produces `bms_parser.hpp` and `bms_parser.cpp`, but it **does
not bundle the external libraries**. Consumers need simdutf's headers and must
link both libraries. The encoding_c C declarations are included in the
amalgamation, so separate encoding_c headers are not required.

### Encoding compatibility

Encoding detection still examines the complete input. BOMs and explicit
`#CHARSET`/`#ENCODING` declarations retain their precedence; unlabelled input is
strictly validated as UTF-8 before comparing EUC-KR and Shift-JIS pair counts.
Both pair counters retain independent byte boundaries, with ties choosing
Shift-JIS. UTF-16 and EUC-KR decoding are unchanged.

When Shift-JIS is selected, decoding follows beatoraja/jbms-parser's Java
`InputStreamReader(..., "MS932")`. This corrects the old converter's backslash,
tilde and DEL mappings, adds Windows-31J extensions/private-use characters, and
matches Java's malformed-input replacement and byte recovery. A compatibility
path handles the cases where WHATWG Shift-JIS (encoding_rs) differs from Java.
The Java oracle test checks every one- and two-byte input plus deterministic
longer inputs. File hashes are still calculated from the original bytes.

## Goal
- [ ] Implement blazing-fast parser with parallel processing

## TODOs 
- [ ] Implement client-specific commands like 
  - [x] [#SCROLL](https://bemuse.ninja/project/docs/bms-extensions/#speed-and-scroll-segments)
- [ ] Refactor interface to fit standard conventions
- [ ] Provide note position calculator

## Others

Check [bms-parser-py](https://github.com/SNURhythm/bms-parser-py) for Python implementation
