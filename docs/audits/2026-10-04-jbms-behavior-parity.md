# jbms-parser behavior corrections

This follow-up replaces the policies identified in the
[failed fidelity audit](2026-10-04-reference-fidelity-validation.md) with behavior
from the local Java source, revision
`f32572b91b60690f6fb322156b471a290b23643e`.

The earlier two clean reviews accepted additional C++ policies. They did not
establish reference parity. Their results are historical, not evidence for
this revision.

## Reference-derived changes

- Numeric headers use Java's floating-point grammar, including type suffixes,
  hexadecimal exponents, underflow, NaN and Infinity. Each header then applies
  the value checks present in `BMSDecoder` or `Section`. Integer headers accept
  Java's BMP decimal digits. LNOBJ permits zero resets, signed and longer
  base-36 integers, Java's relevant Unicode uppercase expansions, and the
  reference's two-character base-62 decoding.
- Floating-point to integer time conversions reproduce Java's saturation and
  NaN-to-zero behavior. Timestamp overflow does not reject the chart. STOP
  durations use the same arithmetic order and integer conversion as Java.
  FP contraction is disabled for Clang and GCC. Numeric lexical scanning is
  iterative, avoiding libstdc++ regex stack overflow on long valid headers.
- Timelines use rounded global double section positions. Events that round to
  the same position share a timeline. Bar lines and controls are inserted
  before channel objects, which follow source order; new times use the cached
  predecessor's double time. Initial BPM acceptance is checked after origin
  events have coalesced.
- Normal notes overwrite existing slots in source order. LNOBJ and channel
  long notes follow their separate Java branches, including interior notes,
  overwritten partners, backward/equal endpoints, mixed types, and orphan
  cleanup. Detached partners retain stable identities and chart ownership.
  Java's sentinel-position unpaired head is preserved; pressing or releasing
  it is safe in C++.
- Counts use the surviving active slots, per-note LN types, and Java's
  millisecond narrowing/filtering. CN/HCN tails count; mines do not contribute
  to TotalNotes. Mine damage retains the raw base-36 value, and unspecified
  PLAYER is zero.
- Difficulty remains zero when undeclared. Generic header prefix matching,
  fixed argument offsets, malformed directives, RANDOM stack behavior, empty
  resource redefinitions, and BPM/BASE delimiters follow the reference.
- Valid P2 cells determine mode even on lanes later discarded by the lane map.
  Channel cells use UTF-16 code-unit positions rather than UTF-8 byte offsets.
  CR-only lines, UTF-8/UTF-16 BOM retention, and UTF-32 BOM decoding match the
  tested Java decoder behavior. Header offsets follow UTF-16 code units,
  including multibyte separators; malformed UTF-16 replacement follows Java.
- Poor-image sequences collapse when they contain one distinct nonzero ID;
  zero cells resolve BMP00 when it exists. A later all-zero definition replaces
  the preceding sequence, as in Java.

Full Parse and Scan reject empty input or a chart whose first timeline still
has BPM zero. C++ ownership and cancellation checks remain necessary for memory
safety; they do not replace Java's musical-content decisions.

## Verification

Validation on the corrected implementation passed:

- `make test` and `make test_amalgamation`.
- Full test binary with AddressSanitizer, UndefinedBehaviorSanitizer, and
  float-cast-overflow checks, with sanitizer recovery disabled.
- Reference seed 491: 414 charts, zero differences, sanitized C++ dumper.
- Reference seed 91822: 414 charts, zero differences, normal C++ dumper.
  Each run includes 200 generated charts, 100 source-order collision charts,
  fixed regression cases, Unicode/binary cases, and existing fixtures.
- GCC 15 at `-O2`: 769 charts/probes, zero differences, including extreme
  scales, collisions, fractional positions, and a 100,000-digit BPM header.
- Parse/Scan metadata, modifier recount, LN reciprocal pointers, and all
  timeline timestamps are checked by the comparison harness.
- `git diff --check` passed; the Java reference checkout remains unchanged.

Two consecutive independent full reviews are clean on this implementation:

| Review | Result | Additional verification |
| --- | --- | --- |
| `fidelity_guard_clean_one` | No actionable findings | 450 extreme-control, scale, and LN-collision probes; zero differences |
| `fidelity_guard_clean_two` | No actionable findings | 700 UTF-16 header, directive, and channel probes; zero differences |

Both reviewed all changed production code, relevant tests, reference harness,
and documentation against the Java source. These results apply within the
boundaries below. Earlier review findings were fixed before these two runs.

Source/test/harness fingerprint:
`4f55a31e5217dcd06f4a9757cd970ddbc3357f3e90d63e959c267c2430adf9a8`.

The durable harness is `scripts/check_jbms_reference.py`. It compiles the actual
local Java BMS sources; the supplied jar only provides the unused BMSON class.
It checks acceptance, numeric/text metadata, active note graphs, head/tail roles,
pair positions, types, damage, resources, background/invisible notes, exact
microsecond times for every timeline (including empty/bar-only timelines),
BPM/scroll controls, BGA/layers, and poor-image sequences.
There is no difficulty exemption, chart exemption, or microsecond tolerance.
It also checks Parse/Scan metadata and modifier recount consistency. Fixture
names must be unique so one case cannot overwrite another.

```sh
python3 scripts/check_jbms_reference.py \
  --reference "$HOME/workspace/jbms-parser" \
  --bmson-classpath /path/to/jbms-parser.jar \
  --seed 491 --cases 200 --collision-cases 100 --sanitize
```

## Boundaries

This records tested behavior, not proof for every possible input. Existing
explicit C++ features without a counterpart in this Java revision remain:
ready-measure insertion, metadata-only inspection, 4K/6K/8K headers, SPEED
headers, declared-charset handling, and derived beat/BPM statistics. Those
features are not presented as Java-equivalent. Full/Scan fidelity checks use
BMS/PMS parsing without those extensions. Automatic encoding detection also
retains the existing C++ heuristic, rather than Java's ordered charset
round-trip heuristic:
for example, BOM-less UTF-8 `#TITLE éé` can be detected as EUC-KR by Java
but as UTF-8 by C++. Byte-for-byte text parity for ambiguous or malformed
legacy encodings is not established.

`TotalLength` remains the C++ end-of-final-measure field, and `PlayLevelText` is
the reference-backed authored value; the numeric PlayLevel projection is a
C++ convenience field. Resource identifiers and ownership have language-specific
representations, so the comparison checks resolved resources and observable
note relationships. BMSON decoding and actual audio/image playback are outside
this parser comparison.
