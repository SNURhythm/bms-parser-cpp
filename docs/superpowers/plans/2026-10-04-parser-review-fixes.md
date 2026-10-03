# Parser Review Fixes Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for parser performance changes and superpowers:dispatching-parallel-agents for independent encoding work and the app handoff document. Track verification below.

**Goal:** Fix the reviewed parser performance and Java parity defects; hand off every app API concern without implementing app consumer fixes.

**Architecture:** Use indexed completed-LN intervals and retire parser state after positions become immutable, retaining unresolved holds and LNOBJ candidates. Match Java decoding and metadata handling against the actual reference source. Keep generated app parser files derived from upstream.

**Tech Stack:** C++17, Python reference/benchmark drivers, Java reference decoder.

**Spec:** The 2026-10-04 three-category review and user's instruction to fix performance/Java compliance while documenting AsoBMaShow API findings. Benchmark evidence is recorded in the follow-up audit.

## Global Constraints

- Preserve Java collision, rounding, timing and detached-partner behavior.
- Preserve existing explicit C++ features unless they cause a reviewed parity defect.
- Do not implement AsoBMaShow consumer/API fixes; document them and their tests.
- Do not edit generated parser files by hand. Regenerate and verify before adoption.
- Use current checkouts; no new worktree. Prior session authorizes parser commits/push and app PR updates.

## Review Focus

- Cross-measure open holds can remove earlier normal notes: keep unresolved ranges.
- LNOBJ can convert the last normal note from an earlier measure: preserve that candidate.
- Floating-point positions can alias the next bar: retain the shared boundary timeline.
- Reversed/overwritten LN pairs retain observable identities and ownership.
- Encoding fallback, malformed byte replacement and metadata lexical rules must match Java, including random branches.

## Task 1: Indexed LN containment

**Files:** `src/ParserNotes.h`, parser regression tests, performance benchmark evidence.
**Interface:** Existing `normal`, `longNote`, `mine`, `finish` preserve behavior.

- [x] Use the reviewed 16k/32k LN benchmark as the failing scaling reproduction.
- [x] Replace completed-hold linear search with logarithmic inclusive interval lookup; preserve reversed-pair behavior.
- [x] Run modular tests and actual-Java collision comparisons, then record scaling.

## Task 2: Retire immutable parser state

**Files:** `src/Parser.cpp`, `src/ParserNotes.h`, `test/performance_limits.cpp`, `Makefile`, focused parser regressions.
**Interface:** Add internal per-measure finalization; public Parse/Scan signatures unchanged.

- [x] Add a Scan live-heap budget regression for 256k ordinary notes; observe it fail before implementation.
- [x] Keep only current timelines, predecessor timing state, boundary aliases and unresolved note identities.
- [x] Check cross-measure LN/LNOBJ conversions, orphan removal, detached partners and rounded boundary positions against Java.
- [x] Verify full/Scan metadata equivalence, sanitizers and the memory regression.

## Task 3: Java encoding and metadata parity

**Files:** Decoder helpers/tables, `src/Chart.h`, `src/Parser.cpp`, reference dumpers/driver, encoding/metadata regressions.
**Interface:** Expose Java VOLWAV/custom values and preserve existing chart ownership and APIs.

- [x] Observe Java/C++ differences for malformed BOM UTF-8, BOMless ambiguous text, VOLWAV/custom metadata and ordinary `**` cells.
- [x] Match Java charset detection/replacement and metadata defaults/lexing; reserve generated metronome cells for the internal ready measure.
- [x] Extend comparison coverage for metadata, explicit random selections and detached partners.
- [x] Run modular/amalgamated tests and fresh reference comparisons.

## Task 4: Verification and app handoff

**Files:** Follow-up audit, README where needed, generated amalgamation, AsoBMaShow API handoff document.

- [x] Benchmark identical compiler/flags against both pre-parity and reviewed baselines; separate timing from allocation instrumentation.
- [x] Review the complete change, resolve in-scope findings, and run required verification.
- [x] Save app findings including cache invalidation, PMS byte loading, null-tail consumers, saturated time arithmetic, mine recount and replay compatibility.
- [x] Commit/push verified parser changes and regenerate amalgamation. Save the app handoff and leave adoption/consumer fixes to its implementer.

## Execution Notes

- Initial performance reproductions already measured: 32k LN Scan 419 ms versus 2.08 ms pre-parity; ordinary 256k Scan peak live heap 114 MiB versus 0.79 MiB.
- App documentation is independent. Encoding implementation is developed in a temporary snapshot and integrated serially to avoid shared-file edits.
- User explicitly requested implementation; proceed without a plan-approval pause.
- Ruling: concurrent AsoBMaShow consumer edits/builds appeared during final verification. Leave its source, generated parser adoption, build directory and unrelated work to that implementer; this task writes only the requested handoff there.
