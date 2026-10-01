# Parser throughput validation — 2026-10-01

Baseline: `561dceb`. Candidate: the parser throughput changes accompanying this report.

## Changes

- Reuse parse-local temporary map storage and compact timing-position storage; remove unnecessary metadata-only note allocations.
- Reduce repeated lookups and accelerate uppercase ASCII header/cell checks while retaining locale-sensitive fallback.
- Unroll portable SHA-256 rounds and encode digest hex directly, retaining the existing digest APIs.
- Add `Parser::Scan` for full-parse metadata and feature flags without retained notes/timelines; use it in AsoBMaShow library scanning, including archive inputs.

Public chart ownership, encoding detection, legacy metadata-only behavior, and production dependencies remain unchanged. Independent parser instances continue to run on caller-owned workers; there is no shared pool or internal worker creation.

## Parallel measurements

518 charts (73,237,573 source bytes), preloaded before timing. Timing includes parser construction, parsing, hashing, and chart destruction; it excludes filesystem/archive reads and database work. Both binaries use Apple clang 21.0.0, C++17, `-O2`, `-DBMS_PARSER_VERBOSE=0`, and `-pthread`, with amalgamated sources, on macOS 26.5.1 ARM64.

For each worker count, run baseline full → candidate full → candidate scan, then reverse that order. Each process performs four rounds; discard the first. Values below are medians of six samples, with observed min–max in parentheses. Allocation instrumentation is disabled for these runs.

| Workers | Baseline full, ms | Candidate full, ms | Candidate scan, ms | Full time reduction | Scan time reduction vs baseline full |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 5093.4 (3706.7–6388.6) | 3342.1 (2412.4–4391.2) | 2108.7 (1465.0–3330.4) | 34.4% | 58.6% |
| 2 | 1955.9 (1327.7–2746.7) | 1096.2 (1058.1–1476.4) | 748.4 (602.2–810.8) | 44.0% | 61.7% |
| 4 | 1157.6 (988.2–1301.7) | 1055.5 (980.4–1104.5) | 712.3 (636.3–746.7) | 8.8% | 38.5% |
| 8 | 905.1 (855.3–1003.1) | 747.9 (692.3–863.5) | 513.5 (472.3–574.9) | 17.4% | 43.3% |
| 16 | 869.1 (838.8–893.1) | 758.6 (682.4–773.1) | 474.9 (457.4–544.1) | 12.7% | 45.4% |

Background load varied substantially, especially in the 1- and 2-worker runs. Treat these as local observations, not portable speed guarantees or a clean scaling curve. At 8 workers, full parsing used 17.4% less median wall time and scanning used 43.3% less than baseline full parsing. The scan comparison represents the consumer’s switch from full parsing; it is not a claim that Scan constructs a full playable chart.

Raw timed samples are in [parser-throughput-samples.json](parser-throughput-samples.json). Reproduce using `make benchmark` and the commands in the repository README with a local chart manifest. The private chart corpus is not redistributed.

## Allocation measurements

A separate instrumented run counts C++ allocation calls and cumulative requested bytes for the same 518 charts at 8 workers. Requested bytes are not live or peak memory.

| Mode | Allocation calls | Requested bytes |
| --- | ---: | ---: |
| Baseline full | 16,122,290 | 1,520,982,931 |
| Candidate full | 11,305,140 | 1,306,077,595 |
| Candidate metadata-only | 779,177 | 369,126,305 |
| Candidate scan | 855,941 | 379,318,325 |

Full parsing makes 29.9% fewer allocation calls; scanning makes 94.7% fewer than baseline full parsing. Full-chart notes retain their public delete-based ownership. Private scratch allocation and avoiding retained notes in Scan deliver the reduction without changing that contract.

## Behavior and validation

- Normal and amalgamated parser suites pass, including independent hash vectors, scratch lifetime checks, cancellation, and concurrent Scan tests.
- 8,310 baseline comparisons pass: 831 real/fixture/generated cases × two seeds × five modes. Existing full/metadata/ready/file modes compare semantic snapshots of metadata, resources, timelines, notes, and links. Scan compares all metadata and the three flags against baseline full parsing.
- AsoBMaShow scanner tests pass before and after integration, including file/archive persisted metadata and feature characterization.
- AddressSanitizer reports no address-safety findings in the exercised tests. UBSan reports the same four non-finite timing-to-integer conversions in baseline and candidate when fixtures omit BPM; this change preserves those existing semantics and does not claim UBSan-clean parsing.
- Independent review found no introduced correctness issues. Existing invalid-BPM conversions, full-Parse exceptional/cancellation cleanup gaps, and overlapping-note ownership issues remain outside this performance change.
- Runtime evidence covers macOS ARM64. Windows and Android runtime behavior has not been tested here.

Application build result is recorded in the adjacent implementation [validation record](../superpowers/plans/2026-10-01-parser-throughput-progress.md).
