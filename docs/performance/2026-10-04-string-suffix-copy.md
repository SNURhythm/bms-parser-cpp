# Bulk copying Java-compatible substring suffixes

This change starts from `4c40a2d6bae8adab30f80eae0347e05740d8eaef`.
`javaSubstring` still locates its start using Java UTF-16 code-unit offsets,
including replacement of a split surrogate with `?`. Once that boundary is
resolved, an unbounded suffix is copied in one append. Finite-length slices
retain the existing implementation. Encoding, trimming, numeric interpretation,
timing and chart-admission behavior are unchanged.

## Measurements

517 preloaded real charts, 73,152,865 input bytes, one worker, Apple Clang 21,
C++17 `-O3 -DNDEBUG`. Forward/reverse paired ordering excludes one warmup and
pools six measured rounds per corpus variant; stress cases pool ten. These are
process CPU times, not application-wide wall-time predictions. All corpus
variants report 1,354,159 notes.

| Workload | Starting CPU ms | Bulk copy CPU ms | CPU reduction |
| --- | ---: | ---: | ---: |
| Corpus Full | 1771.325 | 1715.630 | 3.14% |
| Corpus Scan | 1248.730 | 1254.255 | -0.44%, within noise |
| Corpus Metadata | 1167.170 | 1159.635 | 0.65%, within noise |
| 1 MiB TITLE, Full | 12.169 | 7.498 | 38.39% |
| 1 MiB WAV, Full | 12.297 | 7.719 | 37.24% |
| ASCII header stress, Full | 35.658 | 23.707 | 33.52% |
| Unicode header stress, Full | 77.200 | 63.552 | 17.68% |

Full parsing processes resource filenames that Scan largely skips. The result
is a small ordinary Full improvement and a larger benefit for long values.
The production implementation matches the measured bulk prototype except for
an explanatory comment.

[Raw rounds, commands, compiler and source hashes](experiments/2026-10-04-string-suffix-results.json)
and [separate allocation measurements](experiments/2026-10-04-string-suffix-allocations.json)
include all four investigated variants.

## Alternatives left out

Removing seven redundant argument copies saved 13,768 allocations and 408,817
cumulative requested bytes on corpus Full parsing, approximately 0.10% and
0.03%, without a separately resolved throughput benefit. It is not included
in this focused change.

Trimmed metadata views saved no additional allocations on the real corpus and
retained oversized buffers after repeated header assignments. A 1 MiB TITLE
followed by `#TITLE x` left 1,048,583 bytes of title capacity in that prototype,
versus 22 bytes in the starting and selected implementations on libc++.
The view prototype is not adopted.

## Verification

The exact bulk prototype passed 1,048 charts × four explicit RANDOM selections
against freshly compiled local jbms-parser sources under ASan, UBSan and
float-cast-overflow checks: 4,192 comparisons, zero unexpected differences.
All 4,192 C++ snapshots were byte-identical to the starting parser, including
timestamps. The oracle retains its documented normalization of intentionally
demoted malformed long notes.

A broader combined prototype also passed 3,543,252 before/after helper checks,
including every byte string of length zero through two, 10,000 randomized
Unicode/malformed-byte strings, finite and unbounded slices, control-byte trim,
Unicode numbers and numeric limits. Malformed raw helper inputs are compared
against the prior C++ behavior; actual Java fixtures pass through decoding.

Six durable BOM-backed fixtures cover supplementary-character cuts in TITLE,
WAV, BMP and control values, embedded NUL/control trimming and repeated long/
short metadata assignment. Independent static review found no slicing or
lifetime issue. Production clean modular/resource/Python-comparison suites and regenerated
amalgamation tests pass. A fresh production-source sanitized Java run repeats
all 4,192 comparisons with zero unexpected differences. Application adoption
and its full build/CTest results are recorded in the application handoff.
