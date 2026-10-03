#pragma once

#include <cmath>
#include <random>
#include <unordered_set>
#include "ChartSnapshot.h"

inline int runParserCorrectnessTests() {
  using namespace bms_parser;
  const std::string header = "#TITLE audit\n#BPM 120\n#WAV01 head.wav\n#WAV02 tail.wav\n";
  // New metadata is retained in both Parse and Scan, including Java's unusual
  // custom-value behavior inside a skipped branch and across parser reuse.
  {
    Parser parser;
    parser.SetRandomValues({1});
    std::atomic_bool cancelled{false};
    const std::string text = header + "#VOLWAV -123\n#VOLWAV 1.5\n#VOLWAV 2147483648\n"
        "%key  padded  \n@Key separate\n% empty key\n%empty \n%tab\tignored\n"
        "#RANDOM 2\n#IF 2\n%key overwritten even when skipped\n#VOLWAV 9\n#ENDIF\n#ENDRANDOM\n";
    for (const auto &input : {text, header}) {
      Chart *raw = nullptr;
      parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
      std::unique_ptr<Chart> chart(raw);
      ASSERT_EQ(true, chart != nullptr, "Java custom metadata parses");
      const bool populated = input == text;
      ASSERT_EQ(populated ? -123 : 0, chart->Meta.VolWav, "VOLWAV integer validation and default reset");
      ASSERT_EQ(populated ? size_t{3} : size_t{0}, chart->Meta.Values.size(), "custom values lexical rules and reset");
      if (populated) {
        ASSERT_EQ(std::string("overwritten even when skipped"), chart->Meta.Values.at("key"), "custom values ignore IF skip");
        ASSERT_EQ(std::string("separate"), chart->Meta.Values.at("Key"), "custom keys preserve case");
        ASSERT_EQ(std::string("empty key"), chart->Meta.Values.at(""), "custom empty key is accepted");
      }
      const auto scan = parser.Scan(bytesFromString(input), cancelled);
      ASSERT_EQ(true, scan.has_value(), "custom metadata Scan succeeds");
      ASSERT_EQ(true, parser_test::metadataSnapshot(chart->Meta) == parser_test::metadataSnapshot(scan->Meta), "custom metadata Scan agrees");
    }
  }
  // Returning a parsed chart by value must transfer both timeline ownership and
  // overwritten LN partners, without invalidating pair pointers.
  {
    const auto makeChart = [&]() -> Chart {
      Parser parser;
      std::atomic_bool cancelled{false};
      Chart *raw = nullptr;
      parser.Parse(bytesFromString(header + "#00051:0101\n#00011:02\n"),
                   &raw, false, false, cancelled);
      std::unique_ptr<Chart> parsed(raw);
      if (!parsed) throw std::runtime_error("move fixture parse failed");
      return std::move(*parsed);
    };
    Chart moved = makeChart();
    ASSERT_EQ(size_t{1}, moved.DetachedNotes.size(), "move retains detached LN head");
    auto *head = static_cast<LongNote *>(moved.DetachedNotes.front().get());
    ASSERT_EQ(true, head->Tail != nullptr && head->Tail->Head == head,
              "move preserves LN pair identity");
    const auto snapshot = parser_test::chartSnapshot(moved);
    int destroyed = 0;
    struct CountedNote : Note {
      int &destroyed;
      explicit CountedNote(int &count) : Note(1), destroyed(count) {}
      ~CountedNote() override { ++destroyed; }
    };
    Chart assigned;
    auto *measure = new Measure();
    auto *timeline = new TimeLine(16, false);
    timeline->SetNote(0, new CountedNote(destroyed));
    measure->TimeLines.push_back(timeline);
    assigned.Measures.push_back(measure);
    assigned.DetachedNotes.push_back(std::make_unique<CountedNote>(destroyed));
    assigned = std::move(moved);
    ASSERT_EQ(2, destroyed, "move assignment releases previous chart ownership");
    ASSERT_EQ(true, moved.Measures.empty() && moved.DetachedNotes.empty(),
              "moved-from chart no longer owns notes");
    ASSERT_EQ(snapshot, parser_test::chartSnapshot(assigned), "move preserves chart data");
    ASSERT_EQ(true, assigned.DetachedNotes.front().get() == head && head->Tail->Head == head,
              "move assignment preserves detached LN identity");
    Chart &self = assigned;
    assigned = std::move(self);
    ASSERT_EQ(snapshot, parser_test::chartSnapshot(assigned), "self move preserves chart");
  }
  struct Case {
    const char *name;
    const char *body;
    int notes;
    int longs;
    int mode;
    long long lastTime;
    double lastScroll = 1;
  };
  const Case cases[] = {
    {"base62_channels", "#BASE 62\n#SCROLL01 2\n#000SC:01\n#00011:01\n#00021:01\n#00151:0101\n", 3, 1, 10, 3000000, 2},
    {"undefined_bpm", "#00008:01\n#00011:0001\n#00111:01\n", 2, 0, 5, 2000000},
    {"undefined_scroll", "#SCROLL01 2\n#000SC:0102\n#00111:01\n", 1, 0, 5, 2000000, 2},
    {"undefined_stop", "#STOP01 192\n#00009:01\n#00009:02\n#00111:01\n", 1, 0, 5, 4000000},
    {"negative_bpm", "#BPM01 -120\n#00008:01\n#00111:01\n", 1, 0, 5, 2000000},
    {"zero_bpm", "#BPM 0\n#BPM01 0\n#00008:01\n#00111:01\n", 1, 0, 5, 2000000},
    {"negative_scale", "#00002:-1\n#00111:01\n", 1, 0, 5, 2000000},
    {"zero_scale", "#00002:0\n#00111:01\n", 1, 0, 5, 2000000},
    {"negative_stop", "#STOP01 -192\n#00009:01\n#00111:01\n", 1, 0, 5, 4000000},
    {"empty_dp", "#00029:0000\n#00111:01\n", 1, 0, 5, 2000000},
    {"invalid_dp", "#00029:??\n#00111:01\n", 1, 0, 5, 2000000},
    {"orphan_endpoint", "#LNOBJ ZZ\n#00011:ZZ\n", 0, 0, 5, 0},
    {"unfinished_hold", "#00051:01\n", 0, 0, 5, 0},
    {"mixed_endpoint", "#LNOBJ ZZ\n#00051:01\n#00111:ZZ\n", 1, 1, 5, 2000000},
    {"inside_hold", "#00011:00010000\n#00051:01000100\n", 1, 1, 5, 1000000},
    {"inside_hold_reversed_rows", "#00051:01000100\n#00011:00010000\n", 2, 1, 5, 1000000},
    {"mine_collision", "#00011:01\n#000D1:02\n", 1, 0, 5, 0},
    {"duplicate_cell", "#00011:01\n#00011:02\n", 1, 0, 5, 0},
    {"charge_judgements", "#LNMODE 2\n#00051:0102\n", 2, 2, 5, 1000000},
    {"hellcharge_judgements", "#LNMODE 3\n#00051:0102\n", 2, 2, 5, 1000000},
    {"invalid_cell", "#00011:??01\n", 1, 0, 5, 1000000},
    {"partial_cell", "#00011:1?01\n", 1, 0, 5, 1000000},
    {"same_position_endpoint", "#LNOBJ ZZ\n#00011:01\n#00011:ZZ\n", 1, 0, 5, 0},
    {"lnobj_replaces_pending_head", "#LNOBJ ZZ\n#00051:0001\n#00011:01ZZ\n", 1, 1, 5, 0},
    {"lnobj_replaced_head_followed_by_hold", "#LNOBJ ZZ\n#00051:0001\n#00011:01ZZ\n#00151:0101\n", 1, 1, 5, 2000000},
    {"earlier_endpoint", "#LNOBJ ZZ\n#00011:0001\n#00011:ZZ00\n", 1, 0, 5, 1000000},
    {"invalid_numeric_headers", "#BPM nan\n#BPM01 180junk\n#SCROLL01 nan\n#STOP01 inf\n#00002:2junk\n#00008:01\n#00009:01\n#000SC:01\n#00111:01\n", 1, 0, 5, 2000000},
    {"invalid_hex_bpm", "#00003:??\n#00111:01\n", 1, 0, 5, 2000000},
  };
  for (const auto &test : cases) {
    Parser parser, scanner;
    parser.SetRandomSeed(1); scanner.SetRandomSeed(1);
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    const auto bytes = bytesFromString(header + test.body);
    parser.Parse(bytes, &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(true, chart != nullptr, test.name);
    ASSERT_EQ(test.notes, chart->Meta.TotalNotes, test.name);
    ASSERT_EQ(test.longs, chart->Meta.TotalLongNotes, test.name);
    ASSERT_EQ(test.mode, chart->Meta.KeyMode, test.name);
    ASSERT_EQ(test.lastTime, chart->Meta.PlayLength, test.name);
    ASSERT_EQ(test.lastScroll, chart->Measures.back()->TimeLines.back()->Scroll, test.name);
    const auto scan = scanner.Scan(bytes, cancelled);
    ASSERT_EQ(true, scan.has_value(), test.name);
    ASSERT_EQ(true, parser_test::metadataSnapshot(chart->Meta) ==
                    parser_test::metadataSnapshot(scan->Meta), test.name);
    const auto beforeCounts = parser_test::metadataSnapshot(chart->Meta);
    BaseModifier::RecalculateNoteCounts(*chart);
    ASSERT_EQ(true, beforeCounts == parser_test::metadataSnapshot(chart->Meta),
              "parser and modifier agree on judgement counts");
    long long previousTime = 0;
    for (auto *m : chart->Measures) for (auto *tl : m->TimeLines) {
      ASSERT_EQ(true, std::isfinite(tl->Bpm) && tl->Bpm > 0 &&
                      tl->Timing >= previousTime, test.name);
      previousTime = tl->Timing;
      for (auto *n : tl->Notes) {
        if (auto *ln = dynamic_cast<LongNote *>(n)) {
          if (ln->Head) {
            ASSERT_EQ(ln, ln->Head->Tail, test.name);
            ASSERT_EQ(true, ln->Head->Timeline->BeatPosition <= tl->BeatPosition, test.name);
          } else {
            ASSERT_EQ(true, ln->Tail != nullptr, test.name);
            ASSERT_EQ(ln, ln->Tail->Head, test.name);
            ASSERT_EQ(true, tl->BeatPosition <= ln->Tail->Timeline->BeatPosition, test.name);
          }
        }
      }
    }
  }
  {
    Parser reused, fresh;
    reused.SetRandomSeed(1); fresh.SetRandomSeed(1);
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    const auto source = bytesFromString(header + "#BASE 62\n#LNOBJ zz\n#BPM01 240\n#SCROLL01 2\n#STOP01 192\n#SPEED01 3\n#00011:01zz\n");
    const auto target = bytesFromString(header + "#00008:01\n#00009:01\n#000SC:01\n#000SP:01\n#00011:0100ZZ00\n");
    reused.Parse(source, &raw, false, false, cancelled);
    delete raw;
    reused.Parse(target, &raw, false, false, cancelled);
    const std::unique_ptr<Chart> reusedChart(raw);
    fresh.Parse(target, &raw, false, false, cancelled);
    const std::unique_ptr<Chart> freshChart(raw);
    ASSERT_EQ(true, parser_test::chartSnapshot(*freshChart) ==
                    parser_test::chartSnapshot(*reusedChart), "parser reuse resets all chart state");
    (void)reused.Scan(source, cancelled);
    const auto scan = reused.Scan(target, cancelled);
    ASSERT_EQ(true, scan.has_value(), "reused scan succeeds");
    ASSERT_EQ(true, parser_test::metadataSnapshot(freshChart->Meta) ==
                    parser_test::metadataSnapshot(scan->Meta), "scan reuse resets all chart state");
  }
  {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString(header + "#00051:0102\n"), &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    const auto heads = longNoteHeads(chart.get());
    ASSERT_EQ(size_t{1}, heads.size(), "one completed LN");
    ASSERT_EQ(2, heads.front()->Tail->Wav, "LN endpoint keeps distinct keysound");
    ASSERT_EQ(true, hasReferencedWav(chart.get(), 2), "LN endpoint WAV referenced");
  }
  for (const auto *body : {"#LNOBJ ZZ\n#00011:01ZZ\n", "#00051:0101\n"}) {
    struct RestoreNoWav {
      int original = Parser::NoWav;
      ~RestoreNoWav() { Parser::NoWav = original; }
    } restore;
    Parser::NoWav = -7;
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString(header + body), &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(true, chart != nullptr, "custom silent WAV sentinel parses");
    const auto heads = longNoteHeads(chart.get());
    ASSERT_EQ(size_t{1}, heads.size(), "custom silent WAV sentinel keeps hold");
    ASSERT_EQ(Parser::NoWav, heads.front()->Tail->Wav,
              "silent LN tail honors configured NoWav");
  }
  for (const auto *body : {"#00011:01\n#00051:0101\n",
                           "#00051:0101\n#00011:01\n",
                           "#00011:02\n#00051:0101\n",
                           "#00051:0101\n#00011:02\n"}) {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString(header + body), &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(true, chart != nullptr, "LN head collision parses");
    const bool normalFirst = std::string(body).rfind("#00011", 0) == 0;
    ASSERT_EQ(normalFirst ? size_t{1} : size_t{0}, longNoteHeads(chart.get()).size(),
              "LN head follows Java source order");
    const bool distinct = std::string(body).find("#00011:02") != std::string::npos;
    size_t backgroundCount = 0;
    for (auto *measure : chart->Measures) for (auto *timeline : measure->TimeLines) {
      for (auto *note : timeline->BackgroundNotes) {
        ++backgroundCount;
        ASSERT_EQ(2, note->Wav, "LN head collision preserves distinct keysound");
      }
    }
    ASSERT_EQ(distinct && normalFirst ? size_t{1} : size_t{0}, backgroundCount,
              "only an earlier distinct normal keysound moves to BGM");
  }
  {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString(header + "#WAVaa lower.wav\n#00001:aa\n#BASE 62\n"),
                 &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(2268, chart->Measures[0]->TimeLines[0]->BackgroundNotes[0]->Wav,
              "BASE applies to earlier resource definitions");
  }
  {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString(header + "#BPM01 240\n#00008:01\n#00011:01\n#00111:01\n"),
                 &raw, true, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(size_t{3}, chart->Measures.size(), "ready measure is inserted");
    ASSERT_EQ(2, chart->Meta.TotalNotes, "ready measure preserves notes");
    ASSERT_EQ(2000000LL, chart->Measures[1]->TimeLines[0]->Timing, "original zero follows ready measure");
    ASSERT_EQ(240.0, chart->Measures[1]->TimeLines[0]->Bpm, "original zero BPM preserved");
    ASSERT_EQ(3000000LL, chart->Meta.PlayLength, "ready measure shifts original chart timing");
  }
  {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString(header + "#DIFFICULTY 4\n#DIFFICULTY 2.5\n#00011:01\n"),
                 &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(4, chart->Meta.Difficulty, "invalid difficulty preserves previous value");
  }
  {
    Parser parser;
    parser.SetRandomSeed(1);
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    const std::filesystem::path path("testcases/parser/popn.pms");
    parser.Parse(path, &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(true, chart != nullptr, "PMS file parses");
    ASSERT_EQ(9, chart->Meta.KeyMode, "PMS mode is 9K");
    ASSERT_EQ(false, chart->Meta.IsDP, "PMS is single player");
    std::ifstream input(path, std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),
                                           std::istreambuf_iterator<char>()};
    chart->Meta.BmsPath.clear();
    chart->Meta.Folder.clear();
    for (const auto *source : {"mapping.pms", "mapping.PMS", "mapping.PmS",
                               "@androidtree@/tree/Library/mapping.PMS"}) {
      Chart *bufferedRaw = nullptr;
      parser.Parse(bytes, &bufferedRaw, false, false, cancelled, source);
      const std::unique_ptr<Chart> buffered(bufferedRaw);
      ASSERT_EQ(true, buffered != nullptr, "hinted PMS bytes parse");
      ASSERT_EQ(9, buffered->Meta.KeyMode, "buffered PMS preserves format identity");
      ASSERT_EQ(true, parser_test::chartSnapshot(*chart) ==
                      parser_test::chartSnapshot(*buffered),
                "PMS bytes preserve lanes, LN links, counts and timing");
      const auto bufferedScan = parser.Scan(bytes, cancelled, source);
      ASSERT_EQ(true, bufferedScan.has_value(), "hinted PMS Scan succeeds");
      ASSERT_EQ(true, parser_test::metadataSnapshot(chart->Meta) ==
                      parser_test::metadataSnapshot(bufferedScan->Meta),
                "PMS bytes Scan metadata agrees with full path parsing");
    }
    Chart *legacyRaw = nullptr;
    parser.Parse(bytes, &legacyRaw, false, false, cancelled);
    const std::unique_ptr<Chart> legacy(legacyRaw);
    ASSERT_EQ(10, legacy->Meta.KeyMode, "legacy bytes keep BMS mapping");
    for (const auto *source : {"", "mapping.bms", "mapping.BMS", "outer.pms/chart.bms", "outer.zip"}) {
      Chart *bufferedRaw = nullptr;
      parser.Parse(bytes, &bufferedRaw, false, false, cancelled, source);
      const std::unique_ptr<Chart> buffered(bufferedRaw);
      ASSERT_EQ(true, buffered != nullptr, "BMS hint parses");
      ASSERT_EQ(true, parser_test::chartSnapshot(*legacy) ==
                      parser_test::chartSnapshot(*buffered), "BMS hint preserves default mapping");
      const auto bufferedScan = parser.Scan(bytes, cancelled, source);
      ASSERT_EQ(true, bufferedScan.has_value(), "BMS hint scans");
      ASSERT_EQ(true, parser_test::metadataSnapshot(legacy->Meta) ==
                      parser_test::metadataSnapshot(bufferedScan->Meta), "BMS hint Scan agrees");
    }
    chart->Meta.BmsPath = path;
    chart->Meta.Folder = path.parent_path();
    ASSERT_EQ(std::string("0+5+8;1;1"), noteLanesByTimeline(chart.get()), "PMS channels use popn mapping");
    const auto scan = parser.Scan(path, cancelled);
    ASSERT_EQ(true, scan.has_value(), "PMS scan succeeds");
    ASSERT_EQ(true, parser_test::metadataSnapshot(chart->Meta) ==
                    parser_test::metadataSnapshot(scan->Meta), "PMS scan and full agree");
  }
  {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString(header + "#TITLE Easy Street [ANOTHER]\n#00011:01\n"),
                 &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(0, chart->Meta.Difficulty, "Java does not infer difficulty labels");
  }
  {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString(header + "#BPM\t240\n#TITLE\t tabs \n#WAV03   subdir\\hit.wav  \n#00011:03\n"),
                 &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(120.0, chart->Meta.Bpm, "Java initial BPM requires a space");
    ASSERT_EQ(std::string("tabs"), chart->Meta.Title, "tab-separated and trimmed title");
    ASSERT_EQ(std::string("subdir/hit.wav"), chart->WavTable.at(3), "resource paths trimmed and normalized");
  }
  {
    struct ResourceCase { const char *prefix; const char *input; const char *expected; };
    const ResourceCase resources[] = {
      {"#CHARSET UTF-8\n", u8"price\u00a5100.wav", u8"price\u00a5100.wav"},
      {"\xef\xbb\xbf#CHARSET SHIFT_JIS\n", u8"price\u00a5100.wav", u8"price\u00a5100.wav"},
      {"#CHARSET UTF-8\n", u8"dir\\price\u00a5100.wav", u8"dir/price\u00a5100.wav"},
      {"#CHARSET SHIFT_JIS\n", "dir\\\x83\x5c.wav", "dir/\xe3\x82\xbd.wav"},
    };
    Parser parser;
    std::atomic_bool cancelled{false};
    for (const auto &resource : resources) {
      std::string text = std::string(resource.prefix) + header;
      for (const auto *command : {"WAV01", "BMP01", "STAGEFILE", "BANNER", "BACKBMP", "PREVIEW"})
        text += std::string("#") + command + " " + resource.input + "\n";
      text += "#00011:01\n";
      Chart *raw = nullptr;
      parser.Parse(bytesFromString(text), &raw, false, false, cancelled);
      const std::unique_ptr<Chart> chart(raw);
      ASSERT_EQ(true, chart != nullptr, "encoded resource path parses");
      const std::string expected(resource.expected);
      ASSERT_EQ(expected, chart->WavTable.at(1), "WAV path preserves literal Unicode");
      ASSERT_EQ(expected, chart->BmpTable.at(1), "BMP path preserves literal Unicode");
      for (const auto *path : {&chart->Meta.StageFile, &chart->Meta.Banner,
                               &chart->Meta.BackBmp, &chart->Meta.Preview})
        ASSERT_EQ(expected, path->generic_u8string(), "metadata resource path respects encoding");
      const auto scan = parser.Scan(bytesFromString(text), cancelled);
      ASSERT_EQ(true, scan.has_value(), "encoded resource Scan succeeds");
      ASSERT_EQ(true, parser_test::metadataSnapshot(chart->Meta) ==
                      parser_test::metadataSnapshot(scan->Meta), "encoded resource Scan parity");
    }
  }
  {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString(header + "#RANDOM 1\n#IF\n#00011:01\n#ELSEIF\n#00012:01\n#ENDIF\n#ENDRANDOM\n"),
                 &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(true, chart == nullptr, "Java aborts on a truncated IF directive");
  }
  for (const std::string &text : {
       std::string("#00011:01\n"), std::string("#BPM 1e-300\n#00111:01\n"),
       std::string("#BPM 120\n#00002:1e300\n#00111:01\n"),
       std::string("#BPM 120\n#STOP01 1e300\n#00009:01\n")}) {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString(text), &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    const bool missingBpm = text.rfind("#BPM", 0) != 0;
    ASSERT_EQ(!missingBpm, chart != nullptr, "Java accepts saturated timing but requires BPM");
    ASSERT_EQ(!missingBpm, parser.Scan(bytesFromString(text), cancelled).has_value(),
              "scan follows Java timing acceptance");
  }
  {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    parser.Parse(bytesFromString("#BPM 120\n#00002:5e-324\n#00151:01\n"),
                 &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    const auto heads = longNoteHeads(chart.get());
    ASSERT_EQ(size_t{1}, heads.size(), "Java sentinel-position head survives");
    ASSERT_EQ(true, heads.front()->Tail == nullptr, "Java sentinel-position head is unpaired");
    heads.front()->Press(100);
    ASSERT_EQ(true, heads.front()->IsHolding, "unpaired head can be pressed safely");
    heads.front()->Release(200);
    ASSERT_EQ(false, heads.front()->IsHolding, "unpaired head can be released safely");
  }
  return 0;
}

// Deliberately conflicting rows exercise bookkeeping beyond valid-chart
// equivalence: no crash, stale pointer, orphan pair, or full/Scan disagreement.
inline int runParserCollisionStressTests() {
  using namespace bms_parser;
  std::mt19937 random(0x42534d);
  Parser parser;
  parser.SetRandomSeed(1);
  const std::vector<std::string> channels = {
      "11", "12", "16", "21", "51", "52", "56", "61", "D1", "D2",
      "E1", "31", "01", "03", "08", "09", "SC"};
  const std::vector<std::string> objects = {"00", "01", "02", "ZZ", "??", "1?"};
  std::atomic_bool cancelled{false};
  for (int index = 0; index < 600; ++index) {
    const std::string header = "#BPM 120\n#LNOBJ ZZ\n#WAV01 a.wav\n#WAV02 b.wav\n"
                               "#STOP01 12\n#BPM01 180\n#SCROLL01 -0.5\n";
    std::string input = header + "#LNMODE " + std::to_string(index % 4) + "\n";
    if (index % 3 == 0) input += "#BASE 62\n";
    for (int row = 0; row < 35; ++row) {
      input += "#00" + std::to_string(random() % 4) + channels[random() % channels.size()] + ":";
      const unsigned cells = 1 + random() % 16;
      for (unsigned cell = 0; cell < cells; ++cell) input += objects[random() % objects.size()];
      input += "\n";
    }
    const auto fail = [&](const char *reason) {
      std::cerr << "collision stress case " << index << ": " << reason << '\n' << input;
      return 1;
    };
    try {
      Chart *raw = nullptr;
      parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
      const std::unique_ptr<Chart> chart(raw);
      if (!chart) return fail("unexpected parse rejection");
      const auto scan = parser.Scan(bytesFromString(input), cancelled);
      if (!scan || parser_test::metadataSnapshot(chart->Meta) !=
                   parser_test::metadataSnapshot(scan->Meta)) return fail("Scan mismatch");
      const auto snapshot = parser_test::metadataSnapshot(chart->Meta);
      BaseModifier::RecalculateNoteCounts(*chart);
      if (snapshot != parser_test::metadataSnapshot(chart->Meta)) return fail("counts mismatch");
      std::unordered_set<const Note *> notes;
      long long previousTime = 0;
      for (auto *measure : chart->Measures) for (auto *tl : measure->TimeLines) {
        if (!std::isfinite(tl->Bpm) || tl->Bpm <= 0 || tl->Timing < previousTime)
          return fail("nonmonotonic timing");
        previousTime = tl->Timing;
        for (auto *note : tl->Notes) if (note) notes.insert(note);
      }
      std::unordered_set<const Note *> owned = notes;
      for (const auto &note : chart->DetachedNotes) owned.insert(note.get());
      for (auto *note : notes) {
        if (note->Timeline->Notes[note->Lane] != note) return fail("wrong lane ownership");
        if (const auto *ln = dynamic_cast<const LongNote *>(note)) {
          const auto *pair = ln->Head ? ln->Head : ln->Tail;
          if (!owned.count(pair)) return fail("orphan LN endpoint");
          if ((ln->Head ? pair->Tail : pair->Head) != ln) return fail("nonreciprocal LN pair");
          if (!(ln->Head ? pair->Timeline->BeatPosition <= ln->Timeline->BeatPosition
                        : ln->Timeline->BeatPosition <= pair->Timeline->BeatPosition))
            return fail("unordered LN pair");
        }
      }
    } catch (const std::exception &error) {
      return fail(error.what());
    }
  }
  std::cout << "\t600 collision stress charts passed\n";
  return 0;
}
