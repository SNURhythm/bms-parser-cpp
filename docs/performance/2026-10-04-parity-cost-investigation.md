# Investigating parser complexity and parity cost

## Conclusion

Under a deliberately relaxed compatibility contract, removing detached LN
graphs would simplify ownership, pairing and retirement. The later beatoraja
investigation shows that doing so would change its observable behavior, so this
is not the selected direction when matching beatoraja. It also does not account
for most of the measured throughput regression by itself.
A diagnostic build that omits **all** note bookkeeping improves Scan by only
about 6–7% on the 517-chart corpus. That build deliberately returns incorrect
note metadata and is not a proposed implementation.

Two smaller changes that preserve the tested behavior already show roughly
5–9% lower process CPU time: replace a redundant timeline map with one carried
boundary entry, and stop UTF-16 header-length counting once all relevant
length thresholds have been exceeded. Their combined prototype passes the
sanitized unit suite and 4,160 actual-Java comparisons. The remaining corpus
cost is still about 1.5x the pre-parity parser.

This is an investigation, not a production change. All experimental source
changes are in isolated snapshots. AsoBMaShow API work and its new upstream
PMS API commit are separate. The tested source is `bb8658a3d68f06c11b4ce4ff0d6c9b91055f2961`;
the earlier baseline is `2b964abd62e0bd4b9abc9d450e369c72c095773f`.

## Beatoraja follow-up

The subsequent [consumer investigation](../audits/2026-10-04-beatoraja-long-note-consumers.md)
found that beatoraja actually uses detached partners for rendering and judgment;
it does not discard them. Null partners instead trigger exceptions; database
information updates catch them and continue, while gameplay drawing uses an
unguarded path (the safe drawing path is used by skin previews). Therefore
the narrower LN contract proposed below is an explicit compatibility departure,
not a way to reproduce beatoraja more simply. The user's newer request to mimic
beatoraja takes precedence over that hypothetical simplification.

## Detached and null LN partners

A detached partner still exists and is reachable through a head/tail pointer,
but no longer occupies an active playable lane slot. For example, after an LN
is paired, a later row overwrites the tail's slot with another note. Java can
leave the head pointing to the displaced tail. `Chart::DetachedNotes` owns
that object so the pointer remains valid.

A null partner is absent: an unpaired head has `Tail == nullptr`. These are
different cases. A detached partner is neither null nor a dangling pointer;
checking only for null does not identify it.

A graph census of the 517 common-success charts, with explicit C++ RNG seed
12345, found:

| Observation | Count |
| --- | ---: |
| Active LN endpoints | 70,346 |
| Charts referencing a detached partner | 1 |
| Active endpoints referencing detached partners | 52 |
| Charts with unpaired active LN endpoints | 0 |

The affected file contains repeated lane positions, including LNOBJ cells
subsequently overwritten by ordinary cells. This is evidence for a narrow case
in this corpus, not a prevalence estimate for all BMS or every RANDOM branch.
The common-success manifest also excludes a missing-base-BPM chart symmetrically.

## What can be simplified under a narrower LN contract

A proposed contract for playable output is:

- Every emitted LN has an ordered head and tail in active playable slots.
- Pair pointers are reciprocal and remain chart-owned.
- Replacing an endpoint cannot leave its old partner pointing outside the
  playable graph. Unclosed, reversed, conflicting or overwritten pairs follow
  an explicit discard/rejection policy, without a Java-compatibility promise.
- Ordinary LNOBJ, channel LNs, CN/HCN counting, scratch lanes and resource
  resolution remain supported. Full parsing and Scan agree on their results.

The exact malformed-input policy remains a design choice. Dropping a conflicting
pair while preserving unrelated notes is a possible tolerant policy; rejecting
the chart is simpler to explain but affects compatibility more broadly. Neither
requires reproducing Java's detached identities or sentinel-position orphan.
Input outside the supported contract must still have safe C++ ownership.

This would allow removal or substantial reduction of:

- `Chart::DetachedNotes` and transfer of displaced partner ownership.
- The event arena's requirement to preserve identities after slot replacement.
- Pair-closure tracing, relocation maps and pair-pointer rewrites in
  `ParserNotes::collect`.
- Special handling of backward/equal/sentinel endpoints and detached partners
  in consumers and parity comparisons.

After resolving object ordering within each measure, normal LN interpretation
can use a last-normal candidate and open-head state per lane. A bounded set of
current-measure slots is still needed for merging rows and detecting conflicts.
It is premature to claim that all lane storage can become two scalar variables:
multiple non-overlapping rows for one lane can be legitimate, and changing
source-order interpretation may affect them even when the final graph contains
no detached objects.

Keep one semantic implementation shared by Full and Scan. A separate fast
parser plus a complete Java-quirk fallback would preserve much of the current
complexity and add another equivalence boundary. Explicit Chart move ownership
remains useful independently of detached notes.

## Profile and diagnostic experiments

An eight-second native `sample` run of optimized Scan collected 6,099 worker
samples, excluding the waiting main thread. Approximate non-overlapping sampled
categories were:

| Category | Worker samples |
| --- | ---: |
| SHA-256 and MD5 | 31.9% |
| Timeline creation and Java-timing insertion callees | 15.1% |
| Note bookkeeping callees | 4.7% |
| Decoding, including declared-charset scanning | 4.1% |
| Other parser work | 44.2% |

Inlining leaves additional lookup and timing work in the main parser category;
these are sampling estimates, not exhaustive phase attribution. Hashing predates
the parity rewrite and must not be blamed for its regression. Generated charset
tables account for many added source lines but are not the primary runtime cost
in this sample.

To test the note-layer hypothesis, a diagnostic variant removed normal/LN/mine
bookkeeping, event timing, publication and collection while keeping text parsing,
timeline construction and hashing. It intentionally reports zero playable notes:

| Scan build | Corpus CPU time |
| --- | ---: |
| Current parser, nearby repeat | 1,516.96 ms |
| All note bookkeeping omitted | 1,425.49 ms |
| Boundary/prefix prototype | 1,390.31 ms |
| Prototype with all note bookkeeping omitted | 1,297.74 ms |

The reductions are about 6.0% and 6.7%. They estimate the opportunity from the
whole layer; a correct replacement must still do some note processing, and
removing detached support is narrower. Compiler layout and scheduling also
prevent treating this as a rigorous universal upper bound. Dense or malformed
LN stress charts may have different cost distributions. Even the deliberately
incomplete combined build remains about 1.41x the earlier valid parser's Scan
CPU time, so LN simplification alone cannot explain away the remaining gap.

## Behavior-preserving prototypes

### Carry one boundary timeline

`globalTimelines` repeats entries already indexed by the current measure's
`timelines` map. Prior-measure entries are needed only when a rounded position
aliases the next bar. For the parser's accepted positive finite measure scales,
current cells lie between the current and next bar; at most that boundary entry
needs carrying forward.

The prototype retains its position and pointer directly, including the existing
Scan scratch copy. It removes a tree lookup, node allocation and erasure for each
new timeline. It does not change the Java timing cache or LN graph semantics.

### Bound header-prefix counting

`javaStringLength(line)` walks the entire line before header dispatch, including
long ASCII channel rows. All uses compare against short header-length thresholds;
the largest threshold in this snapshot is 12 UTF-16 units. The prototype stops
after 16 units while preserving decoding at a surrogate boundary. A production
version should name/document that bound or expose an explicit prefix-length
predicate so adding a longer directive cannot silently invalidate it.

### Results

Process CPU milliseconds, identical Apple Clang 21 C++17 `-O3` flags, preloaded
517-chart corpus, one worker, median three measured rounds after one warmup:

| Mode | Earlier parser | Current parser | Combined prototype | Improvement vs current |
| --- | ---: | ---: | ---: | ---: |
| Full | 1,277.14 | 2,010.07 | 1,909.04 | 5.0% |
| Scan | 918.99 | 1,516.96 | 1,390.31 | 8.3% |
| Metadata | 859.10 | 1,393.76 | 1,269.15 | 8.9% |

Repeated Scan batches put the improvement around 8–9%. The first isolated
boundary-only experiment improved corpus Scan by about 7%; prefix-only was
smaller. Replacing repeated timing lookups with `lower_bound`/`emplace_hint`
was inconsistent and is not included in the proposed patch.

Separate allocation-instrumented runs measured:

| Scan input | Current calls | Prototype calls | Change |
| --- | ---: | ---: | ---: |
| 517-chart corpus | 5,216,710 | 3,915,538 | -24.9% |
| 256,000 ordinary notes | 791,095 | 535,094 | -32.4% |

Corpus requested allocation bytes decrease from 755,041,218 to 692,584,962.
These are cumulative requested bytes, not peak live heap or RSS.

Synthetic Scan CPU time, median five measured rounds after warmup:

| Input | Current | Prototype |
| --- | ---: | ---: |
| 256k ordinary notes across measures | 111.74 ms | 93.96 ms |
| 32k LN endpoints across measures | 10.68 ms | 8.45 ms |

Concurrent AsoBMaShow builds and OS ReportCrash/audio activity occurred during
some attempts. Before/after activity snapshots are retained, but these runs were
not strictly activity-gated throughout. CPU and wall times were close; the
repeated direction and allocation reduction support the finding, while the exact
percentages should be rechecked before a release performance claim. Instrumented
allocation timings are not used for throughput claims.

## Verification and next steps

The combined boundary/prefix prototype passed:

- The pinned revision's complete unit suite under ASan, UBSan and
  float-cast-overflow checks with recovery disabled.
- Fresh actual-Java comparisons: 1,040 charts x four explicit RANDOM selections,
  zero unexpected differences, sanitized C++ build, generation seed 19041.
  Java reference: `f32572b91b60690f6fb322156b471a290b23643e`.
- Patch applicability against the concurrent PMS API revision `5c3bb2f`.
  This last check is not runtime verification of a merged implementation.

The [experimental patch](experiments/2026-10-04-boundary-prefix.patch) is saved
for review and is **not applied**. It has not yet undergone the full production
adoption sequence, fresh amalgamation/GCC checks or an independent change review.
[Machine-readable results](experiments/2026-10-04-results.json) include timings,
allocation counts and prototype source hashes. Raw local profiles, logs and
experimental source are under `/tmp/bms-parser-profile-20261004`.

Recommended sequence:

1. Adopt the small boundary/prefix changes with production verification; they
   remove redundant work without needing a compatibility-policy decision.
2. For beatoraja-compatible output, retain detached pairs and define handling
   for the null-partner failure case. Replacing identity-preserving bookkeeping
   with a simpler lane model remains an option only under an explicit broader
   compatibility departure. Such a change needs a well-formed regression corpus,
   malformed-input safety tests and a documented split in the strict-Java tests.
3. Profile again, focusing on repeated timeline indexing and timing replay.
   Consolidation is a larger opportunity, but source-order-dependent floating
   rounding makes a simple chronological pass observably different even for
   otherwise valid charts. Relaxing exact timestamps is a separate policy from
   rejecting malformed LN graphs and needs explicit replay-compatibility tests.
