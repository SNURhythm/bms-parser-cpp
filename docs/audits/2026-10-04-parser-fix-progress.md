# Parser corrections and review record

**Historical record.** The subsequent
[behavior correction work](2026-10-04-jbms-behavior-parity.md) replaces the
additional policies described here. Consult that record for the current behavior
and final verification status.

**Follow-up correction:** [strict reference-fidelity validation](2026-10-04-reference-fidelity-validation.md)
failed. The completed review loop below accepted compatibility policies and
does not prove that the changes introduced no assumptions or stricter validation.

Goal: compare with the local jbms-parser, fix incorrect parsing, and finish only
after two consecutive completed review runs find no remaining problems.

Final clean-review streak: **2**. The requested find-and-fix loop is complete.

## Corrections

The original finding numbers refer to
[the initial audit](2026-10-03-jbms-parser-comparison.md), which describes the
unchanged baseline rather than current defects.

| Original finding | Implemented correction |
| --- | --- |
| 1: BASE/channel decoding | Channels stay base 36; the final BASE setting is resolved before resource IDs. |
| 2, 4, 8: timing definitions | Validate complete finite numbers and positive BPM/scales; ignore undefined references without overwriting valid events; make negative STOP lengths positive; reject unrepresentable timed charts. |
| 3: parser reuse | Reset per-chart tables, BASE, and LNOBJ on every Parse/Scan call. |
| 5, 6: pairing and collisions | Resolve ordered reciprocal pairs and lane conflicts before allocating notes; remove unfinished holds and preserve background audio. |
| 7: ready measure | Insert preparation content while preserving original measure zero. |
| 9: effective counts | Count the surviving graph using shared Parse/Scan bookkeeping; align modifier recounts. |
| 10: release audio | Preserve distinct endpoint WAVs and collect referenced WAVs from the final graph. |
| 11, 12: mode and invalid cells | Validate complete nonzero cells before note creation or mode promotion. |
| 13: difficulty | Require complete integer values and preserve the preceding valid declaration. |

Additional corrections cover PMS file mapping, tab-separated headers, resource
path normalization, whole-word difficulty labels, malformed conditional
arguments, and scoped ownership on failure/cancellation. CN/HCN counts include
both scoring endpoints and exclude mines.

## Review findings fixed during the loop

1. LNOBJ could replace a pending channel head with a tail without clearing the
   pending-head state. Clear it at either replaced endpoint; focused cases and
   600 deterministic malformed-collision charts cover graph integrity.
2. A normal note at a completed LN head could duplicate the same keysound as
   background audio depending on channel-row order. Check the exact head before
   its interval; regressions cover both orders and matching/distinct WAVs.
3. Delayed materialization hardcoded the silent-tail WAV as -1 despite the public
   configurable `Parser::NoWav`. Pass the configured sentinel into the note
   resolver. Tests with -7 failed before the correction and pass afterward for
   both LNOBJ and channel-LN tails.
4. Path normalization treated literal Unicode yen signs as directory separators.
   Preserve decoder selection through header parsing and limit that conversion
   to text produced by the legacy Shift-JIS decoder. Tests cover UTF-8, BOM
   precedence, real backslashes, and a Shift-JIS multibyte character whose trail
   byte is 0x5c, across WAV/BMP and metadata resource paths plus Scan parity.

Each finding reset the clean-review streak. Reviews prevented by process-runner
file-descriptor exhaustion are not clean reviews. Restarting the runner restored
access; source changes remained saved.

## Verification

Two consecutive independent full reviews inspected all modified production
files, the note resolver, regression/stress tests, reference harness/dumpers,
audit documents, and relevant Java logic. Both covered all 13 original finding
groups and found no actionable issues after the resource-encoding correction.
No implementation changes occurred between the two clean reviews.

| Final review | Result |
| --- | --- |
| First full review after the encoding correction | No actionable findings. |
| Second independent full review of the same source | No actionable findings; twelve additional targeted probes also preserved graph/count/Scan invariants. |

Three malformed collision probes in the second review differed from Java
because the reference retained overlapping notes or overwritten LN endpoints.
The C++ results retained coherent paired holds; these were not parser defects.

- `make test` passed after the latest resource-encoding correction, including
  resource-path cases, both custom sentinel cases, four audio-collision cases,
  and 600 malformed-collision charts.
- `make test_amalgamation`, a fresh AddressSanitizer/UBSan/float-cast-overflow
  build and full test run, and both Java reference comparisons passed after the
  resource-encoding correction. The UTF-8 yen repro also now matches Java using
  the rebuilt dumpers.
- Reference runs use Java checkout `f32572b91b60690f6fb322156b471a290b23643e`.
  Seeds 491 and 91822 each cover 233 charts (200 generated plus 33 regression and
  bundled charts); the first also enables C++ sanitizers. Both earlier runs had
  zero unexpected differences; both repeated final-source runs also passed.

The durable harness is `scripts/check_jbms_reference.py`, with dumpers under
`test/reference`. It compiles BMS decoding from the local Java source; the jar
supplies only the unused BMSON dependency. It checks note positions/types/pairs,
keysounds, background/invisible notes, timing, BPM, scroll, metadata, effective
counts, and Parse/Scan metadata parity. Regression tests also cover ready-measure
behavior, resource references, strict headers, failure paths, and parser reuse.

## Compatibility and coverage limits

- `TotalLength` remains the end of the final measure; `PlayLength` is the last
  playable object's time. The reference has no matching guessed-beat statistics.
- C++ retains difficulty inference when Java reports unknown difficulty, and its
  custom 4K/6K/8K modes. PMS is inferred from `.pms` by file APIs; byte APIs use BMS.
- Mine damage retains this library's base-36-value / 2 representation.
- The known Java nested-RANDOM parent-skip defect is explicitly exempted for
  `ubmchallenge`; C++ graph/count/Scan invariants still run for that chart.
- Timing comparisons allow one microsecond of floating-point rounding difference.
- This is not exhaustive certification of every malformed input, encoding/random
  combination, or third-party chart. BMSON and actual resource playback are outside
  the scope of this BMS parser comparison.

Final source/test fingerprint (SHA-256):
`6d9faa147c603ecb9a05b6fe6d64ce29cf77f2e9baea78476cab979b0e547e70`.
This hashes sorted paths and contents for `.cpp`, `.h`, `.py`, and `.java` files
under `src`, `test`, and `scripts`, plus the PMS regression fixture. The final
completion check confirmed it was unchanged across both clean reviews.
