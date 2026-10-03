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
input uses BMS channel mapping. Numeric grammar and event handling follow
jbms-parser, including its accepted non-finite values and saturated timestamps.
Full parsing and Scan reject empty input and charts without an initial BPM.
Explicit `metaOnly` inspection can still read BPM-less header metadata.
`TotalNotes` counts surviving scoring judgements (including CN/HCN tails and
excluding mines); `TotalLength` includes the rest of the final measure.

Lane collisions and long-note pairing follow Java source order. Later rows can
overwrite a partner's playable slot; the chart owns detached partners so existing
LN pointers remain valid. `Scan` uses the same event bookkeeping without
allocating playable note objects. Undeclared difficulty remains zero.

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
for measured parallel results, allocation counts, and behavior checks.

For comparison against the local Java reference implementation:

```sh
python3 scripts/check_jbms_reference.py --reference "$HOME/workspace/jbms-parser" \
  --bmson-classpath /path/to/jbms-parser.jar --seed 173 --cases 200 --sanitize
```

The jar supplies only the unused BMSON dependency; BMS decoding compiles from
the reference checkout. Results and raw dumps go to `build/jbms-review` by
default. Comparisons include metadata, note graphs, exact timestamps, and BGA;
no chart or difficulty differences are exempted. See the
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
