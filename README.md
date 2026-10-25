# bms-parser-cpp [WIP]

C++ implementation of Be-Music Script parser 

WIP: Parser interface may change. This is quite functional though; you can use it right away.

You can get amalgamated code from [releases](https://github.com/SNURhythm/bms-parser-cpp/releases), or you can build it by yourself by running `make amalgamate` in the root directory.

## Library scanning

Use `Parser::Scan` when you need full-parse metadata and feature flags without
retaining playable notes or timelines:

```cpp
bms_parser::Parser parser;
std::atomic_bool cancelled{false};
auto result = parser.Scan(path, cancelled); // also accepts a byte vector
if (result) {
  const auto &metadata = result->Meta;
  // result->HasBga: at least one valid, nonempty #BMP declaration
  // result->HasBpmStop: an effective timeline has StopLength > 0
  // result->HasScrollChange: an effective timeline has Scroll != 1
}
```

`Scan` returns no result on cancellation or file-read failure. It uses the same
encoding detection, hashes, random choices, event order, and statistics as full
`Parse` with `addReadyMeasure = false`. Set the same random seed/values when
comparing the two. The file overload assigns `BmsPath` and `Folder`; the byte
overload leaves those for the caller. The existing `metaOnly` mode keeps its
legacy behavior and is separate from `Scan`.

For concurrent scans, use a separate `Parser` instance per job. Parsing and
hashing stay on the calling thread. A worker can reuse buffers across jobs with
its own `ScanWorkspace`; keep constructing a fresh parser per job to preserve
independent random state:

```cpp
bms_parser::ScanWorkspace workspace; // one per worker, outside its job loop
// For each job:
bms_parser::Parser parser;
bms_parser::ScanTimings timings;
auto result = parser.Scan(path, cancelled, workspace, &timings);
// Or: parser.Scan(bytes, cancelled, sourcePath, workspace, &timings);
```

The workspace retains input, decoded text, timing positions and arena blocks,
with a 1 MiB ceiling for each category after every call (about 4 MiB per worker).
Larger charts still parse, then release their oversized buffers. Cancellation
and exceptions also clear the workspace. Results own their data and remain valid
when the workspace is reused. A workspace is not safe for simultaneous calls.
Optional timings report input read, hashes, decoding and remaining parse work;
parallel sums are worker time, not elapsed batch time. Archive extraction and
caller work are outside these counters. Omit the timing pointer to avoid clocks.
Full charts retain their existing public vectors and `delete` ownership.

File parsing/scanning selects the nine-key PMS layout for `.pms` files; byte
input defaults to BMS channel mapping for compatibility. Supply the original
chart filename as the final argument when reading a buffer (for an archive,
use the inner entry name, not the archive name):

```cpp
parser.Parse(bytes, &chart, false, false, cancelled, "charts/song.PMS");
auto result = parser.Scan(bytes, cancelled, "charts/song.PMS");
```

The source-name overloads use a case-insensitive `.pms` extension, do not read
that path, and leave `BmsPath`/`Folder` for the caller to assign. Other extensions
and an empty source name use BMS mapping. The existing byte overloads remain
available. Numeric grammar and event handling follow
jbms-parser, including its accepted non-finite values and saturated timestamps.
Full parsing and Scan reject empty input and charts without an initial BPM.
Explicit `metaOnly` inspection can still read BPM-less header metadata.
`TotalNotes` counts surviving scoring judgements (including CN/HCN tails and
excluding mines); `TotalLength` includes the rest of the final measure.

Lane collisions and long-note pairing follow Java source order. Later rows can
overwrite a partner's playable slot. Beatoraja's discards are preserved. Surviving
LNs that cannot render/judge as healthy holds become normal notes with their
existing timing, lane and sound. Healthy classic heads can retain detached tails;
the chart owns those partners. Demotion is O(1) per endpoint, with no chart-wide
repair scan. `Scan` uses the same rules without allocating playable note objects.
Undeclared difficulty remains zero.

If the player selects CN/HCN after parsing an undefined LN type, call
`TimeLine::DemoteUnusableLongNote(lane, resolvedType)` during the existing
pre-playback count pass. It returns the current slot and may delete the old LN;
resolve the mode before caching note pointers, and reparse for a different mode.
See the [malformed-LN policy](docs/audits/2026-10-04-malformed-long-note-policy.md).

Default charset detection follows Java's ordered round-trip checks, including
ambiguous BOM-less text and malformed-byte replacement. Explicit `#CHARSET`
and `#ENCODING` remain C++ extensions. `ChartMeta::VolWav` preserves `#VOLWAV`
(default zero), and `ChartMeta::Values` preserves Java's `%`/`@` custom metadata.
The internal `**` click token is accepted only in a generated ready measure.

## Performance and equivalence checks

`make benchmark` builds `build/parser_benchmark`. Supply a text file containing
one chart path per line, a mode, a worker count, and a round count:

```sh
build/parser_benchmark chart-paths.txt full 8 5
build/parser_benchmark chart-paths.txt scan 8 5
```

All inputs are preloaded. `file` and `scan-file` reread them during timing, so
these measure warmed filesystem caches. Timings include parser construction and
chart destruction. Define `BMS_BENCH_WORKSPACE=1` together with
`BMS_BENCH_SCAN=1` to reuse one workspace per worker in `scan`/`scan-file` modes.
An optional fifth argument writes semantic fingerprints instead of running a
pure timing measurement. `scripts/check_parser_equivalence.py` compares two
separately built benchmark binaries across full/metadata/ready/file parsing,
two random seeds, the supplied corpus, and generated edge cases. Add `--scan`
to compare the candidate's scan result against baseline full parsing. For
example:

```sh
python3 scripts/check_parser_equivalence.py /tmp/baseline/parser_benchmark \
  build/parser_benchmark --manifest chart-paths.txt --output /tmp/parser-check --scan
```

Build the baseline harness from `test/performance.cpp` with
`-DWITH_AMALGAMATION=1 -I/path/to/baseline`, linking the baseline amalgamated
source. Omit `-DBMS_BENCH_SCAN` when the baseline predates `Scan`. Use the same
compiler/optimization flags for both binaries and keep correctness checks and
profiling separate from timing runs.

See the [throughput validation report](docs/performance/2026-10-01-parser-throughput.md)
for the original optimization results and the
[review follow-up](docs/audits/2026-10-04-parser-review-fixes.md) for measurements
after the Java behavior corrections. The later
[baseline review loop](docs/audits/2026-10-04-baseline-review-loop.md) records
allocation and cancellation fixes without changing the decoding contract.
The [timing consolidation](docs/performance/2026-10-04-timing-consolidation.md)
removes the remaining duplicate timing index while retaining exact timestamps.
`make test` checks that scanning a 256,000-note ordinary chart stays below
8 MiB of peak requested live C++ heap and half an allocation per note, and that
cancellation interrupts dense chart construction, note publication and hold-closure work.

For comparison against the local Java reference implementation:

```sh
python3 scripts/check_jbms_reference.py --reference "$HOME/workspace/jbms-parser" \
  --bmson-classpath /path/to/jbms-parser.jar --seed 173 --cases 200 --sanitize
```

The jar supplies only the unused BMSON dependency; BMS decoding compiles from
the reference checkout. Results and raw dumps go to `build/jbms-review` by
default. Comparisons include metadata/custom values, note graphs and detached
partners, integer microsecond timestamps/STOP durations, and BGA. Four explicit
RANDOM selection sequences are checked; this does not compare PRNG algorithms.
The explicit malformed-LN policy is applied structurally to raw Java expectations;
the report counts changed endpoints and retains untouched raw dumps. No chart or
difficulty names are exempted. See the
[behavior correction record](docs/audits/2026-10-04-jbms-behavior-parity.md)
for validation results and the scope of existing C++ extensions.

## Goal
- [ ] Implement blazing-fast parser with parallel processing

## TODOs 
- [ ] Implement client-specific commands like 
  - [x] [#SCROLL](https://bemuse.ninja/project/docs/bms-extensions/#speed-and-scroll-segments)
- [ ] Refactor interface to fit standard conventions
- [ ] Provide note position calculator

## Others

Check [bms-parser-py](https://github.com/SNURhythm/bms-parser-py) for Python implementation

## bmson

`Parser::Parse` and `Parser::Scan` accept bmson 1.0 (`version: "1.0.0"`)
and legacy 0.21/0.2.1 (no version, `"0.21"`, or `"0.2.1"`). Files ending in
`.bmson` are recognized case-insensitively; byte input is also recognized by
its JSON object, optionally preceded by a UTF-8 BOM. Invalid JSON, unsupported
versions/layouts, invalid timing, and cancellation produce no chart/result.

Legacy fields (`initBPM`, `judgeRank`, `bpmNotes`, `stopNotes`, `soundChannel`,
`bgaHeader`, `bgaNotes`, `layerNotes`, `poorNotes`, and uppercase `ID`) are
mapped explicitly. Legacy timing uses 240 pulses per quarter note; 1.0 uses
`info.resolution` (default 240). Both use pulse-based note and stop positions.
`ChartMeta::TotalIsRelative` is false for legacy absolute TOTAL, true for 1.0's
percentage of the player's default gain. `Total` retains its authored units;
zero relative TOTAL means no gain. Judge ranks 0–4 use the BMS rank table,
matching jbms-parser compatibility behavior; negative values retain normal
rank. Other judge widths use the existing `DefExRank` percentage representation; `Values["bmson.judge_rank"]` preserves the authored
fractional percentage for clients with finer judgement precision.

Supported layouts cover every mode in jbms-parser's `Mode` enum:
`beat-5k`, `beat-7k`, `beat-10k`, `beat-14k`, `popn-5k`, `popn-9k`,
`keyboard-24k`, and `keyboard-24k-double`. Beat modes retain the sparse BMS
lane namespace. Keyboard modes expose 26/52 consecutive lanes, with wheels at
24/25 (and 50/51 for double play); their `KeyMode` values are 24/48.
Pop'n 5-key shares `KeyMode == 9` with pop'n 9-key, like the reference mode ID,
but `Values["bmson.mode_hint"]` selects five active lanes. Preserve that value
when copying chart metadata. Unsupported mode hints are rejected rather than
remapped. Notes, paired long notes, timing, bar lines, metadata,
BGA/layer/poor events and referenced resources share the BMS chart model.
The common `ln_type`, note `t`/`up`, `scroll_events`, `key_channels` and
`mine_channels` extensions are read. Hidden keys and mines reference whole
source audio; their WAV IDs remain in `WavTable` rather than `WavSlices`.
Mines retain authored damage and are suppressed on playable notes or within
long notes. Nonpositive modern resolution falls back to 240.
Other unknown extensions are ignored. An empty `lines` array hides bars; omitted/null
lines generate 4/4 bars. Implicit generation is bounded to one million bars.

### BGA event metadata

`bga_sequence`, `layer_events` conditions/`id_set`/`interval`, and sequence IDs
in `poor_events` are decoded into `TimeLine::BgaEvents`. Each `BgaEvent` has a
`BgaEventCondition` (`Always`, `Play`, `Miss`), a signed `Interval`, and
`Sequences`: nonnull `shared_ptr<const BgaSequence>` alternatives in authored
order. A `BgaSequence` is a vector of `BgaSequenceFrame` containing signed
64-bit `TimeMillis`, `BmpId`, and `IsEnd`. Omitted frame IDs and the Java
`Integer.MIN_VALUE` sentinel mean END. An unresolved image has
`BmpId == BgaSequenceBlank` but `IsEnd == false`.

Frame IDs resolve against authored `bga_header` IDs, just like direct BGA
references. This deliberately differs from `BMSONDecoder.java`, which copies
sequence frame IDs directly into runtime resource indexes while mapping
ordinary references through the header table. Arbitrary header IDs therefore
remain safe and consistent here. Undefined direct base/layer IDs produce no
visual change. All images reachable from effective events, including alternate
sequences, are retained in `ReferencedBmpTable`.

Standard direct layers still populate `BgaLayer`; conditional and sequence
layers do not. The last layer event at a pulse wins; `poor_events` override
that pulse's event metadata and use `Miss` with interval 1. A direct image
fallback becomes an image at 0 ms followed by END at 500 ms. Effective MISS
events also populate the existing `BgaPoor.Frames` from only their first
sequence, mapping END to `BgaSequenceBlank`. An empty first sequence or empty
`id_set` produces a present `BgaPoor` with empty `Frames`, meaning a blank miss.

This projection does not implement frame timing, interval, PLAY, or alternate
sequence playback. The referenced beatoraja renderer uses only MISS events and
the first sequence, distributing frame positions across its 500 ms miss window
without reading authored frame timestamps or intervals. Timed metadata shares
immutable storage across reused sequences; the legacy MISS projection still
owns frame copies. Full parsing and Scan reject charts expanding beyond
1,048,576 effective MISS frames (4 MiB of frame IDs), before allocating those
copies. Resource traversal visits each shared sequence once.

### Audio slices

A bmson note's `Wav` is a synthetic ID in `Chart::WavSlices`, whose value is a
vector of `WavSlice { SourceWav, StartMicros, DurationMicros, SliceId }`. `SourceWav`
indexes `WavTable`/`ReferencedWavTable`, containing actual audio file names.
Slices start at the given offset in the decoded source; duration `-1` means
through EOF. Other durations exclude the end. Mix all slices for a synthetic
ID together when that note is triggered. This preserves layered keysounds
without adding extra judgements or requiring the parser to decode audio.
`SliceId` identifies one channel/pulse slice. Share one monophonic voice for
that identity even across layered notes or chord keys; distinct identities may
overlap, including identical ranges from later restarts. Duplicate BGM assignments
of a playable slice are discarded. Resource paths normalize backslashes to
slashes and reject absolute paths, parent traversal and embedded NULs.

Continuation offsets use elapsed chart time, including BPM changes and stops.
Notes at one pulse in a channel share a slice. A restart resets the offset and
lets the previous sound finish; a continuation cuts the previous slice at its
onset. Scan/meta-only calls retain metadata but no playable graph or slice table.
Hashes always identify the original input bytes, not normalized JSON.

The JSON reader is picojson v1.3.0 (BSD-2-Clause), vendored under
`src/third_party/picojson.h` with its namespace renamed for amalgamation safety.
