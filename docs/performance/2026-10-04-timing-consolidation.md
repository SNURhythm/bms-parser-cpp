# Consolidating timeline timing work

This follow-up starts from `00f9fc732162bbc4e10fac6920d9204eed43b30d`.
The user permits timestamp differences below one millisecond and requests
notification of any difference greater than five microseconds. The selected
implementation preserves exact timestamps instead: the extra speed of the
approximate prototype did not justify its unbounded error on accepted inputs.
No parsing, long-note, replay, or chart-admission policy changes are introduced.

## Selected implementation

The existing per-measure timeline index now owns its timing state too. Each
entry receives its first-insertion rank: the bar line, sorted control actions,
then channel objects in source order. A monotone stack over section order finds
the nearest predecessor that existed at that insertion. Each entry is pushed
and popped at most once. Replaying the original actions with those cached
predecessors preserves the original floating-point operations while removing
a second ordered tree, its node allocations, and repeated lookups.

The last immutable predecessor and any rounded next-bar state survive between
measures. Distinct local controls that round to one section still execute in
order; notably, STOP uses the BPM present at its own action. Timeline field
assignment, immutable-timeline observations, BPM bounds and carry-state updates
share the existing statistics pass. Every unbounded loop retains cancellation
polling. Generic header matching also uses a view after the leading ASCII `#`,
avoiding repeated full-value UTF-16 substring conversions.

## Why chronological timing was not adopted

A chronological prototype passed a direct comparison on 517 real charts,
976 reference BMS fixtures and 4,800 adversarial charts with at most one
microsecond of drift and no other differences in the fields inspected by the
reference dump. This is a corpus observation, not an error bound.

A subsequent accepted-input stress case exposed 1,699,968 microseconds of drift.
At BPM 120, `#STOP01 96000000000000` followed by `#00009:01` creates a finite
STOP of roughly `1e18` microseconds. A source row first inserts a note at
17/20 of the measure; a later row inserts 256,000 evenly spaced notes on another
lane. The original cached time at 17/20 is `1000000000001699968`; chronological
accumulation produces `1000000000000000000`. Double precision's 128-microsecond
spacing at this magnitude loses the tiny successive intervals. The user was
notified that the measured difference exceeds both the reporting threshold and
the allowed limit.

The first paired measurements put chronology only about 1–3% ahead of the exact
stack prototype on the real corpus. A safe approximate fallback would need
both an accumulated-error bound and special handling of discrete decisions
near startup, wrapped note-count windows, and long-note timing comparisons.
The selected exact implementation avoids adding that machinery. Its final
combined-pass version is measured below.

## Measurements

Preloaded 517-chart corpus (73,152,865 bytes), one worker, Apple Clang 21,
C++17 `-O3 -DNDEBUG`, identical benchmark harness. Each paired process excludes
one warmup and supplies three measured rounds; the table pools six measured
rounds per variant. Variants run in forward then reverse order. These are
process CPU milliseconds, not application scan wall-time predictions.

| Mode | Starting `00f9fc7` | Selected version | CPU reduction |
| --- | ---: | ---: | ---: |
| Scan | 1,379.355 | 1,255.445 | 9.0% |
| Full | 1,878.670 | 1,764.225 | 6.1% |
| Metadata | 1,262.215 | 1,173.195 | 7.1% |

All variants report the same 1,354,159 corpus notes. The incremental real-corpus
difference from fusing passes and using header views is below 1%, within run
noise; the duplicate timing-index removal accounts for the clear improvement.
The separate earlier-baseline batch still puts the exact version roughly
1.36–1.40x the CPU time of `2b964abd`. This change reduces the remaining cost;
it does not restore that earlier parser's throughput.

| Synthetic workload (Scan) | Starting version | Selected version | Reduction |
| --- | ---: | ---: | ---: |
| 256,000 notes across measures | 89.304 ms | 57.314 ms | 35.8% |
| 256,000 notes in one measure | 165.464 ms | 84.701 ms | 48.8% |
| 1 MiB TITLE header | 25.946 ms | 12.400 ms | 52.2% |
| 1 MiB ignored header | 91.243 ms | 7.400 ms | 91.9% |

The header-specific benefit follows from avoiding full-value conversions during
command matching. Long values themselves retain the same decoding behavior.
The dense single-measure process RSS observations were approximately 140–142 MiB
before and 133–138 MiB afterward; these include preloaded input and process
runtime memory. Requested live C++ heap is measured separately below.

[Raw rounds, compiler details and source hashes](experiments/2026-10-04-timing-consolidation-results.json)
include both prototype batches and the measured chronological counterexample.

The allocation-instrumented 256,000-note Scan/metadata workload drops from
535,605 allocations to 279,609. Peak requested live heap remains about 1.1 MiB.
The regression budget is tightened from three to two allocations per note;
it fails on the starting implementation and passes on the new implementation.
The budget accommodates different deque block allocations in libc++ and
libstdc++; the GCC 15 build uses 431,419 allocations on the same workload.
Cancellation triggers are derived from a completed allocation count, so a
reduction in allocations cannot prevent the test from requesting cancellation.

## Verification

Final production verification passes:

- Clean Clang modular unit/resource tests, Python reference-comparison tests,
  and regenerated amalgamation tests.
- GCC 15 unit and resource suites.
- ASan, UBSan and float-cast-overflow checks on the final modular unit/resource
  suites, with sanitizer recovery disabled.
- Fresh actual-Java comparison: 1,042 charts × four explicit RANDOM selections,
  4,168 comparisons, zero unexpected differences (seed 19041).
- Independent fused-timing review: 4,800 sanitized Java comparisons and 19,200
  byte-identical C++ snapshots across full/metadata and ready/no-ready modes.
- The initial exact prototype also passed 8,170 complete snapshots on 817 real
  and generated charts across five modes and two seeds.
- Independent correctness, performance and application-adoption source reviews:
  no remaining actionable findings.

Regression fixtures cover ordinary one-microsecond source-order differences,
rounded control collisions, and a finite enormous STOP with dense subdivisions.
The final portable allocation budget was rerun against the starting parser and
failed at 535,605 allocations, then passed on both Clang/libc++ and GCC/libstdc++.
Cancelled resource tests publish no partial chart and retain no allocations.

The application copies both regenerated artifacts; its adoption and complete
build/CTest results are recorded in its `docs/audits/2026-10-04-bms-parser-api-handoff.md`.
The public generated header is byte-identical to the prior version.
