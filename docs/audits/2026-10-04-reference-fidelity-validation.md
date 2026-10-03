# Strict reference-fidelity validation

**Historical record.** The subsequent
[behavior correction work](2026-10-04-jbms-behavior-parity.md) replaces the
additional policies described here. Consult that record for the current behavior
and final verification status.

Result: **FAIL**. The changes cannot be described as solely derived from
jbms-parser, or as introducing no stricter validation. This supersedes any such
interpretation of the earlier completion report. No implementation was changed
during this follow-up audit.

Compared the current uncommitted source with C++ baseline
`2b964abd62e0bd4b9abc9d450e369c72c095773f` and Java reference
`f32572b91b60690f6fb322156b471a290b23643e`. Targeted probes used the already-built
dumpers from `build/jbms-review-round-two`.

## Confirmed introduced rules and deviations

| Change | Evidence against Java fidelity |
| --- | --- |
| Complete finite-number/ERANGE validation (`Parser.cpp`, `parseFiniteNumber`) | After BPM 120, `#BPM 240d` leaves C++ at 120; Java accepts 240. Java also accepts Infinity for BPM and underflow to zero for SCROLL, whereas C++ ignores those definitions. Java uses `Double.parseDouble` in `BMSDecoder.java:256`, `267`, `318`, `337`. |
| Timestamp rejection (`validTimeAdvance`) | With BPM `1e-20` and a later note, C++ returns null; Java returns a model, using Java's saturated integer conversion for overflowing times. Rejecting the chart is an additional policy. C++ undefined behavior must not be reintroduced when addressing this difference. |
| LNOBJ range/length validation (`Parser.cpp`, LNOBJ branch and `ParseInt`) | `#LNOBJ ZZ` followed by `#LNOBJ 00` leaves ZZ active in C++; Java resets it and produces two normal notes. Base-36 `0ZZ` and `+Z` are also accepted by Java but rejected by C++. See `BMSDecoder.java:748`. |
| Strict hexadecimal channel 03 | C++ ignores `#00003:1G`; Java applies BPM 32 through its base-36-to-hex arithmetic (`Section.java:103`). The C++ rejection is newly imposed. |
| Completed-hold precedence (`ParserNotes::normal`, `close`) | `#00051:01000100` followed by `#00011:00010000`: C++ retains one playable LN and moves the interior normal note to BGM; Java retains the LN and the playable normal note. Java's normal-note branch (`Section.java:353`) is source-order dependent. Making it order independent was an added policy. |
| Difficulty-label heuristic (`difficultyLabel`) | `Easy Street [ANOTHER]` without DIFFICULTY gives C++ 4 and Java 0. Difficulty guessing already existed in C++; the new last-whole-word precedence was invented here, not copied from Java. |
| Empty resource rejection | Redefining WAV01 with whitespace leaves its previous filename in C++; Java replaces it with an empty string (`BMSDecoder.java:285`). Trimming followed by rejecting the result adds a restriction. |
| Header/directive grammar | New exact-name matching ignores `#DIFFICULTYx 4`, which Java accepts as difficulty 4. Malformed IF handling also differs. Conversely, new tab support for initial BPM and BASE is broader than Java. These are not exact reproductions of its grammar. |

Additional C++-specific work includes ready-measure insertion, ownership and
cancellation repairs, and overflow protection for derived beat statistics.
These may be useful, but cannot be justified as direct Java behavior. Preserved
pre-existing differences include custom key modes, mine damage units, difficulty
inference, metadata-only inspection, and nested RANDOM handling. They should
not be confused with new deviations introduced by this patch.

## Why the previous checks did not establish fidelity

The comparison harness skips difficulty differences when Java reports zero,
allows one microsecond of timing difference, and exempts Java comparison errors
for `ubmchallenge`. Generated charts do not cover all the lexical and collision
cases above. The two clean reviews accepted these compatibility policies; they
were correctness reviews under those policies, not proof of strict parity.

Targeted raw outputs and fixtures are retained in
`/tmp/bms-reference-fidelity` and `/tmp/jbms-provenance-numeric`.

Reference-backed fixes such as state reset, base-36 channel decoding, undefined
timing-reference handling, positive scale checks, negative STOP normalization,
strict DIFFICULTY integers, PMS mapping, and distinct release WAVs remain
supported by the Java source. That does not validate the additional rules above.

Before claiming strict fidelity, remove or revise the new policies, reproduce
Java's accepted numeric grammar with well-defined C++ conversion behavior, and
run comparisons that report rather than exempt semantic differences.
