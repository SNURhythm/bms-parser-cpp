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
hashing stay on the calling thread, and scratch memory belongs to each parse.
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

Inputs are preloaded; timings include parser construction and chart destruction.
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
`make test` checks that scanning a 256,000-note ordinary chart stays below
8 MiB of peak requested live C++ heap and three allocations per note, and that
cancellation interrupts dense timing, note publication and hold-closure work.

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
