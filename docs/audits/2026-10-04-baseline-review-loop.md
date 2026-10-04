# Correctness and performance review against the original parser baseline

## Scope and contract

Review the parser from `2b964abd62e0bd4b9abc9d450e369c72c095773f` through
`fc7f84f0ecb94be7a7b17bf20e1dd52346029d58`, then review the fixes below. Review
AsoBMaShow from `d96f713a1deb8582d426bee7e44ab3d90c1cdc75` through
`efce4a73ede94195da04da6a99f96b74bb0a5c0e` and its subsequent generated-parser
update. The user requested automatic fixes and repeated review until no
actionable correctness or performance issue remained.

Preserve the existing Java-reference contract, C++ extensions, and approved
[selective malformed-LN policy](2026-10-04-malformed-long-note-policy.md).
Do not invent input handling, replay provenance, or compatibility policies.
The existing rejection of unsupported saved playback without a new Obsolete
classification is explicitly accepted and is not a finding.

## Findings and fixes

Three performance findings were confirmed. No additional parser correctness
or application integration defect was found in the initial independent passes.

1. **Redundant timeline index.** `globalTimelines` duplicated the current
   measure's ordered timeline map, allocating another tree node per position.
   Cells cannot pass the rounded next-bar position, so only that one entry can
   be reused by a later measure. Carry its position and pointer directly;
   preserve the existing Scan scratch copy and full-chart ownership. This also
   covers successive measures whose starts round to the same position.
2. **Whole-row header length counting.** UTF-16 length was computed over every
   complete row, although dispatch only compares short length thresholds.
   Stop counting at a bound derived from the generic command names and the
   special-header thresholds. Value extraction still uses the full original
   string, including supplementary Unicode characters.
3. **Cancellation during added parsing passes.** Timing replay, event timing,
   publication, collection, and long-hold closure could continue large amounts
   of work after cancellation. Poll the existing token in those loops and
   propagate early return through the parser's existing RAII ownership.
   A second review caught the publication and hold-closure cases after the
   initial timing-loop fix; these are now included in the regression checks.

The changes do not alter successful chart output or long-note decisions.
Cancellation still returns no chart; it now stops the added work earlier.
Destruction of already allocated state still takes time.

## Regression evidence

The allocation tests exercise real public Parse/Scan calls and trigger
cancellation by allocation count, avoiding a wall-clock race and production
test hooks. On the local Clang build:

| Workload | Before | After |
| --- | ---: | ---: |
| Scan / metadata, 256,000 notes: allocations | 791,605 | 535,605 |
| Same workload: peak requested live heap | 1,193,200 B | 1,145,200 B |
| Dense Scan / metadata: allocations after cancellation | 132,339 | 0 |
| Full parse, late publication: allocations after cancellation | 128,000 | 0 |
| Full parse, long-hold closure: allocations after cancellation | 255,993 | 1 |

These are workload-specific resource regressions, not general allocation or
latency promises. Both cancelled full-parse cases retain no allocations and
return no partial chart.

The independent parser reviewer exercised 4,300 additional sanitized Java
comparisons: 3,000 collision/extreme-scale charts, 100 long charts exercising
collection and retained partners, and 1,200 mixed extreme BPM/STOP/SCROLL charts.
These also check Full/Scan metadata and recount agreement. The same corpus
passed before and after the boundary/header optimizations, and again against
the final cancellation changes. The normal oracle
includes new short-header and long supplementary-Unicode-header cases.

## Final verification

Final release measurements used Apple Clang 21, C++17, `-O3 -DNDEBUG`, one
worker, and 517 preloaded charts totaling 73,152,865 bytes. Each mode ran in
ABBA revision order, with three measured rounds after one warmup per process;
other review builds/probes were paused during these final measurements.

| Mode | Previously pushed `fc7f84f` CPU ms | Fixed CPU ms | Reduction |
| --- | ---: | ---: | ---: |
| Scan | 1,519–1,526 | 1,367–1,376 | 9.9% |
| Full | 1,994–2,009 | 1,854–1,881 | 6.7% |
| Metadata | 1,382–1,390 | 1,251–1,253 | 9.7% |

Every round counted 1,354,159 notes, and peak RSS remained comparable. Separate
allocation runs found 5,220,400 → 3,919,228 Scan allocations on this corpus.
These measurements do not imply recovery of the original baseline's speed:
earlier baseline runs measured approximately 931–933 ms Scan, 1,295–1,325 ms
Full and 832–841 ms metadata. Some auxiliary compilation overlapped those
earlier runs, so the exact baseline ratios have a contention caveat. The
remaining cost accompanies the compatibility rewrite; the review found no
further concrete avoidable regression requiring a fix. Raw final rounds,
source hashes and methodology are in the
[measurement record](../performance/experiments/2026-10-04-review-loop-results.json).

Clang modular and amalgamated suites passed. GCC 15 modular and resource suites
passed. Modular unit/resource suites passed ASan/UBSan with float-cast-overflow
checking and recovery disabled. That sanitizer run first found an existing
alignment error in the resource test's allocation prefix on macOS ARM; using
ordinary `operator new`'s required alignment fixes the instrumentation without
changing parser code. The resource regressions pass under both compilers.

The final parser passed a fresh sanitized Java comparison of 1,042 charts ×
four explicit selections (4,168 comparisons), with zero unexpected differences.
Independent final parser and performance source reviews found no actionable
issues, including partial collection ownership on cancellation. The application
review also found no new issue in the generated-file adoption or cancellation
handling. Its visual, projection and gameplay consumer suites passed ASan/UBSan
against the regenerated artifacts. Full application build/test verification is
recorded in the application's
[contract handoff](https://github.com/SNURhythm/AsoBMaShow/blob/fix/bms-parser/docs/audits/2026-10-04-bms-parser-api-handoff.md).

The final review pass found no remaining actionable issue. Mobile device/provider
smoke tests and end-to-end beatoraja graphics/gameplay execution were outside
this pass; local compiler, differential and consumer tests do not establish
those runtime guarantees.
