# BMS parser comparison with jbms-parser

Original audit: 2026-10-03. The findings below describe the original revision.
Find-and-fix follow-up began on 2026-10-04; current status is tracked separately
in `2026-10-04-parser-fix-progress.md`.

Compared C++ commit `2b964abd62e0bd4b9abc9d450e369c72c095773f` with the local
`~/workspace/jbms-parser` at `f32572b91b60690f6fb322156b471a290b23643e`.
Line numbers below refer to these revisions. P1 means lost/invalid playable
content or timing; P2 means incorrect metadata, audio, or malformed-input handling.
The Java implementation is a reference, not an infallible specification.

## Validation

- `make test` passed.
- Compiled the reference repository's core `src/bms/model/*.java` sources and a
  diagnostic dumper. Only `BMSONDecoder.java` was excluded; the existing
  `../beatoraja/lib/jbms-parser.jar` supplied that unused class dependency.
  BMS parsing used the newly compiled local sources, not the jar's BMS parser.
- Compiled a C++ dumper against this repository's objects. Compared synthetic
  charts, sequential parser reuse, and the seven standard BMS/BME fixtures in
  `test/testcases/{metadata,beat-guess}`. The custom 4K/6K/8K fixtures were excluded
  from direct equivalence because the reference does not implement those modes.
- Both dumpers used explicit random choices of 1 for the corpus comparison.
  Java's compact 5K/10K lane indices were normalized to C++'s 16-lane layout.
- Rebuilt C++ with `-fsanitize=undefined,float-cast-overflow
  -fno-sanitize-recover=all` for the undefined-BPM repro.
- Harnesses, generated fixtures, raw outputs, and corpus results are retained
  locally in `/tmp/bms-parser-audit/`. These are diagnostic artifacts, not new
  passing regression tests. No broad external chart corpus was tested.

Unless stated otherwise, prepend these headers to each small repro below:

```bms
#TITLE audit
#BPM 120
#WAV01 head.wav
#WAV02 tail.wav
```

## Confirmed findings

### 1. P1 — BASE 62 changes channel decoding and loses notes

C++: `src/Parser.cpp:1099`, `src/Parser.cpp:2093`.
Reference: `Section.java:70`, `BMSDecoder.java:115`.

```bms
#BASE 62
#SCROLL01 2
#000SC:01
#00011:01
#00021:01
#00151:0101
```

Java produces three counted notes, including the paired long note, in 10K mode,
and applies scroll 2. C++ produces **zero notes**, stays in 5K mode, and fails to
apply scroll 2. `ParseInt(ch)` uses the resource base, but the channel constants
remain base 36. For example, `11` becomes 63 instead of 37. Some other channels
can accidentally become different supported channels instead of simply vanishing.

There is a separate ordering problem: Java determines the final BASE before
parsing resource definitions; C++ switches bases mid-read. With
`#WAVaa lower.wav`, `#00001:aa`, then `#BASE 62`, Java plays `lower.wav` and C++
produces a silent background note.

Fix direction: decode channels in base 36 independently of resource IDs, and
resolve the resource base before interpreting headers and cells.

### 2. P1 — Undefined BPM references corrupt the entire remaining timeline

C++: `src/Parser.cpp:1478`, `src/Parser.cpp:1662`, `src/TimeLine.cpp:55`.
Reference: `Section.java:130`.

```bms
#00008:01
#00011:0001
#00111:01
```

No BPM01 exists. Java ignores the event and places the notes at 1,000,000 and
2,000,000 microseconds at BPM 120. C++ sets BPM to zero, computes a NaN stop
(`0 / 0`), and converts nonfinite time to `long long`. On this build the later
notes displayed time zero; that result is not portable because the conversion
is undefined behavior. UBSan reports:

```text
src/Parser.cpp:1666:49: runtime error: nan is outside the range of representable values of type 'long long'
```

Fix direction: undefined references must not replace the effective BPM. Reject
invalid BPM values before timeline arithmetic.

### 3. P1 — Reusing Parser leaks chart-specific state into the next chart

C++: `src/Parser.h:72`, `src/Parser.cpp:837`.
Reference: `BMSDecoder.java:98` initializes a new model and clears event tables.

`BpmTable`, `StopLengthTable`, `ScrollTable`, `SpeedTable`, `UseBase62`, and `Lnobj`
are members and are never reset at the start of a parse. This also affects
sequences involving `Scan`, which uses the same `ParseInternal`.

Parse a first chart containing `#LNOBJ ZZ`, `#BPM01 240`, `#SCROLL01 2`, and
`#STOP01 192`. Reuse the parser for a chart without those declarations:

```bms
#00008:01
#000SC:01
#00009:01
#00011:0100ZZ00
```

Java produces two normal notes, BPM 120, scroll 1, and no stop. Reused C++ produces
one long note, BPM 240, scroll 2, and a one-second stop. A preceding BASE 62 chart
also causes an ordinary subsequent `#00011:01` to disappear.

Fix direction: reset per-chart tables/base/LNOBJ on every parse while preserving
caller configuration such as random seed and supplied random choices.

### 4. P1 — Invalid numeric timing values can reverse or collapse time

C++: `src/Parser.cpp:1272`, `src/Parser.cpp:1908`, `src/Parser.cpp:1928`.
Reference: `Section.java:91`, `BMSDecoder.java:260`, `BMSDecoder.java:318`.

| Repro body | Java | C++ |
| --- | --- | --- |
| `#BPM01 -120`, `#00008:01`, `#00111:01` | Rejects BPM definition; note at 2 s | Note at -2 s |
| `#00002:-1`, `#00111:01` | Ignores invalid scale; note at 2 s | Note at -2 s, beat position -1 |
| `#00002:0`, `#00111:01` | Ignores invalid scale; note at 2 s | Note at 0 s, beat position 0 |
| `#STOP01 -192`, `#00009:01`, `#00111:01` | Converts stop to positive; note at 4 s | Negative stop; note at 0 s |

With no initial BPM header, Java rejects the chart; C++ returns a chart after
nonfinite timing arithmetic. The C++ conversions also accept numeric prefixes
and do not check finiteness. Java itself is not uniformly strict about infinity,
so its numeric handling should not be copied without validation.

Fix direction: validate complete numeric tokens and finite values; require
positive BPM and measure scales. Choose an explicit policy for invalid stops
that preserves monotonic timing.

### 5. P1 — Long-note endpoint and pairing validation is missing

C++: `src/Parser.cpp:1531`, `src/Parser.cpp:1577`; `src/LongNote.cpp:49`.
Reference: `Section.java:361`, `BMSDecoder.java:413`.

Independent repro bodies:

| Body | Java | C++ |
| --- | --- | --- |
| `#LNOBJ ZZ`, `#00011:ZZ` | Ignores unpaired endpoint | Adds a normal playable note |
| `#00051:01` | Removes unfinished LN | Keeps an LN with neither endpoint pointer set |
| `#LNOBJ ZZ`, `#00051:01`, `#00111:ZZ` | Pairs the channel head with LNOBJ | Keeps an unpaired LN plus a normal note |
| `#LNOBJ ZZ`, `#00011:0001`, `#00011:ZZ00` | Keeps the normal note; ignores earlier endpoint | Creates a backwards LN from position 0.5 to 0 |
| `#LNOBJ ZZ`, `#00011:01`, `#00011:ZZ` | Keeps the normal note | Replaces the same slot twice; only the tail remains reachable from the chart |

C++ accepts the last encountered normal note without checking chronological
order or whether it still occupies the lane. It also tracks channel LNs and
LNOBJ candidates separately. Unfinished channel LNs are never cleaned up.
`LongNote::IsTail()` interprets an unpaired head as a tail because its Tail is null;
calling `Press()` on that object would dereference null.

Fix direction: enforce ordered, distinct endpoints; unify lane pairing state;
validate the note still occupying the candidate timeline; discard or repair
unfinished pairs before exposing the chart. Keep Scan statistics consistent.

### 6. P1 — Overlap handling changes playable content

C++: `src/Parser.cpp:1577`, `src/Parser.cpp:1611`, `src/TimeLine.cpp:28`.
Reference: `Section.java:414`, `Section.java:445`, `Section.java:498`.

```bms
#00011:00010000
#00051:01000100
```

Java moves the normal note inside this hold to background audio and retains one
playable LN. C++ leaves the normal note inside the hold and reports two notes.

For `#00011:01` followed by `#000D1:02`, Java preserves the normal note and rejects
the colliding mine. C++ overwrites the normal note with a mine. `SetNote` also
replaces the old pointer without deleting it or repairing pairing state.

Fix direction: define collision resolution against the effective lane graph,
including occupied LN intervals, and update ownership/counts together.

### 7. P1 — addReadyMeasure deletes original measure zero

C++: `src/Parser.cpp:1182`. This is a C++ API issue, independent of Java behavior.

```bms
#00011:01
#00111:01
```

`addReadyMeasure=false` returns both notes. With it enabled, C++ replaces
`measures[0]` with metronome events and returns only the second note. It does not
insert a measure or shift the original content. Original measure-zero BPM,
scroll, stop, BGA, and scale events are likewise removed.

Fix direction: insert a preparation measure while preserving the original
measure data, or explicitly constrain/rename the API if replacement is intended.

### 8. P2 — Undefined scroll/STOP references overwrite valid state

C++: `src/Parser.cpp:1495`, `src/Parser.cpp:1521`.
Reference: `Section.java:142`, `Section.java:155`.

```bms
#SCROLL01 2
#000SC:0102
#00111:01
```

Java ignores undefined SCROLL02 and keeps scroll 2. C++ resets scroll to 1 at the
second cell and carries it forward.

For `#STOP01 192`, `#00009:01`, `#00009:02`, `#00111:01`, Java keeps the valid
stop at the shared position and places the note at 4 s. C++ erases that stop and
places the note at 2 s. The undefined event should not overwrite a defined event.

### 9. P2 — Counts reflect parsed cells instead of surviving notes

C++: `src/Parser.cpp:1560`, `src/Parser.cpp:1641`.
Reference: `BMSModelUtils.java:82`, `TimeLine.java:139`.

```bms
#00011:01
#00011:02
```

Both parsers retain one note with `tail.wav`, but C++ reports TotalNotes=2 and
Java reports 1. Repeating this pattern can also inflate C++'s inferred difficulty.
The same ownership replacement leaks the overwritten note. This is separate
from any deliberate policy about whether LN tails count toward difficulty.

Fix direction: count effective notes or adjust counters on replacement/removal;
use equivalent collision bookkeeping in Scan.

### 10. P2 — Channel long-note release keysounds are discarded

C++: `src/Parser.cpp:1600`.
Reference: `Section.java:461`.

```bms
#00051:0102
```

Java assigns `head.wav` to the head and `tail.wav` to the endpoint. C++ always
constructs the endpoint with `NoWav`, losing a distinct release sound and omitting
that resource from ReferencedWavTable. Java suppresses the endpoint sound only
when it resolves to the same WAV as the head.

### 11. P2 — Empty channels change the detected key mode

C++: `src/Parser.cpp:1318` (before cell decoding).
Reference: `Section.java:186`.

```bms
#00029:0000
#00111:01
```

Java detects 5K. C++ detects 14K DP even though the second-player lane contains
no objects. Invisible, long-note, and mine channel families have the same
pre-cell mode promotion. Mode selection can consequently expose incorrect
lanes and difficulty categories.

Fix direction: promote the mode only after finding a valid nonzero object.

### 12. P2 — Invalid object tokens create phantom notes

C++: `src/Parser.cpp:1399`, `src/Parser.cpp:1530`, `src/Parser.cpp:2115`.
Reference: `Section.java:231`.

```bms
#00011:??01
```

Java ignores `??` and creates one note at half a measure. C++ creates an
additional silent note at the beginning and reports two notes. Only the literal
`00` is skipped before note construction; the base-36 fallback accepts partial
or failed `strtol` conversions. Undefined but syntactically valid WAV references
are a separate case: both implementations can retain those as silent notes.

Fix direction: validate both characters, reject invalid/zero tokens before
creating notes or changing note counts/mode.

### 13. P2 — DIFFICULTY accepts partial numbers and overwrites valid metadata

C++: `src/Parser.cpp:1899`.
Reference: `BMSDecoder.java:772`.

```bms
#DIFFICULTY 4
#DIFFICULTY 2.5
#00011:01
```

Java retains 4 after rejecting the invalid integer. C++ truncates the token to
2 and replaces it. This is the same permissive-conversion pattern already
avoided by C++'s stricter RANK implementation. Apply complete-token integer
validation to DIFFICULTY rather than interpreting a numeric prefix.

## Compatibility decisions and coverage gaps

- **Difficulty inference:** jbms-parser keeps missing DIFFICULTY as 0. C++ guesses
  from title/subtitle substrings, then note-count thresholds (`Parser.cpp:1820`).
  This is a policy difference, not evidence that every inferred value is wrong.
  However, `Easy Street [ANOTHER]` becomes 1 because `easy` is checked before
  `another`. Retaining the declared value separately from a guess would allow
  consumers to distinguish unknown difficulty from inferred difficulty.
- **CN/HCN counts:** with `#LNMODE 2` and `#00051:0102`, Java TotalNotes is 2,
  while C++ TotalNotes is 1. Java counts both judgements for CN/HCN; C++ counts
  only heads. `BaseModifier::RecalculateNoteCounts` also uses a head-only policy.
  Define whether TotalNotes means physical notes or scoring judgements before
  changing this. Beatoraja-compatible scoring/density needs the latter.
- **PMS:** the C++ path overload does not select a PMS lane map. A `.pms` with
  channels 11, 22, and 25 produces Java POPN_9K lanes 0/5/8, but C++ 10K lanes
  0/9/12. Treat PMS as unsupported or implement explicit format selection; do
  not present this as correct PMS parsing.
- **Length and derived BPM fields:** C++ TotalLength includes the remainder of
  the last measure, despite its header comment describing the final timeline.
  The reference does not provide identical most-prevalent/guessed-beat fields;
  these were not certified by this comparison.
- **Reference limitations:** Java's BMS decoder has no corresponding C++
  scratchless 4K/6K/8K headers or working SPEED declaration branch in this
  revision. Its nested RANDOM handling also has the bug described below.
  Do not remove C++ functionality just to achieve byte-for-byte equivalence.
- **Scope:** BMSON decoding, resource file existence, gameplay rendering, and
  exhaustive encoding/random-branch combinations were not covered. Legacy
  metaOnly deliberately skips some events; full/Scan equivalence is a different
  contract from Java equivalence.

## Bundled-chart results

The comparison checked effective note positions, lanes, note classes, counts,
BPM, and scroll. It did not exhaustively compare every keysound or LN pair in
these real charts; dedicated synthetic cases above covered endpoint behavior.

| Chart | Placement/types | Largest timing difference | BPM/scroll |
| --- | --- | --- | --- |
| Altale | Match | 1 microsecond | Match |
| Ozma | Match | 0 | Match |
| Wizdomiot | Match | 0 | Match |
| Jack the Ripper | Match | 1 microsecond | Match |
| example | Match | 0 | Match |
| Aleph0 | Match | 0 | Match |
| ubmchallenge, all supplied random choices = 1 | Different | Not summarized | BPM range matches |

The last chart exposes a reference bug. At fixture line 347780, an outer
`#IF 14` is inactive, but an inner `#RANDOM 1024` / `#IF 1` follows it. Java
consults only the innermost skip flag (`BMSDecoder.java:232`) and reads
`#00116:...A1...` at line 347783 despite the skipped parent. C++ correctly skips
that note. Java returns 5,292 notes; C++ retains 4,032 note objects while reporting
4,095 notes. The Java extra-branch content is not a C++ defect; the C++ discrepancy
between its own retained objects and count is consistent with finding 9.

Jack the Ripper and example have Java difficulty 0 versus C++ difficulty 2,
consistent with C++'s inference policy. No placement defect was found in the
six matching charts. The 0–1 microsecond rounding differences do not explain the
large timing failures in the synthetic cases.

## Recommended repair order

1. Reset per-chart state and separate channel/resource base decoding.
2. Guard timing values and ignore undefined event references without overwriting
   effective values.
3. Repair LN pairing, collisions, cleanup, and ready-measure preservation.
4. Align counts, keysound references, key-mode detection, and strict metadata
   conversion with the chosen compatibility contract.
5. Turn the minimal cases into regression tests covering full Parse and Scan,
   including repeated calls on the same Parser instance. Decide PMS and
   inferred-difficulty policies explicitly.
