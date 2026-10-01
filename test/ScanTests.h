#pragma once
#include "ChartSnapshot.h"
#include <thread>

inline int runScanTests() {
  struct Case { const char *text; bool bga; bool stop; bool scroll; };
  const Case cases[] = {
      {"", false, false, false},
      {"#BPM 120\n#WAV01 hit.wav\n#BMP00 poor.png\n#00111:01\n", true, false, false},
      {"#BPM 120\n#STOP01 48\n#SCROLL01 -0.5\n#00009:01\n#000SC:01\n#00111:01\n", false, true, true},
      // Flags describe effective timelines, not merely declarations/events.
      {"#BPM 120\n#STOP01 48\n#STOP02 -48\n#SCROLL01 -0.5\n#SCROLL02 1\n"
       "#00009:01\n#00009:02\n#000SC:01\n#000SC:02\n#00111:01\n", false, false, false},
      {"#BPM 120\n#STOP01 48\n#SCROLL01 0.5\n#00009:01\n#00009:ZZ\n"
       "#000SC:01\n#000SC:ZZ\n#00111:01\n", false, false, false},
      {"#BPM 120\n#STOP01 48\n#SCROLL01 0.5\n#00111:01\n", false, false, false},
      {"#BPM 120\n#RANDOM 1\n#IF 3\n#BMP01 hidden.png\n#ENDIF\n#ENDRANDOM\n"
       "#BMP01 \n#00111:01\n", false, false, false},
      {"#BPM 120\n#BMP!1 odd.png\n#00111:01\n", true, false, false},
      {"#bpm 120\n#bmp01 image.png\n#scroll01 nan\n#000sc:01\n#00111:01\n", true, false, true},
      // Legacy metaOnly stops reading BGM/invisible rows after the first event.
      // Scan must use full-mode positions and rounding for all statistics.
      {"#BPM 137\n#BPM01 173\n#WAV01 hit.wav\n#00102:0.75\n#00111:01\n"
       "#00101:010101010101010101010101\n#00231:010101010101\n#00208:0001\n"
       "#00306:000000ZZ\n", false, false, false},
      {"#BPM 120\n#LNOBJ ZZ\n#LNMODE 3\n#00011:01ZZ01ZZ\n#00117:01ZZ\n"
       "#00154:01\n#00354:01\n#004D9:01\n#00501:000001\n", false, false, false},
      {"#BPM 120\n#BASE 62\n#BMPzz image.png\n#WAVzz note.wav\n#00011:zz\n"
       "#00006:zz00zz\n#RANDOM 2\n#IF 1\n#SCROLL01 -1\n#000SC:01\n"
       "#ELSE\n#STOP01 24\n#00009:01\n#ENDIF\n#ENDRANDOM\n", true, true, false},
  };
  for (const auto &test : cases) {
    const auto bytes = bytesFromString(test.text);
    bms_parser::Parser fullParser, scanParser;
    fullParser.SetRandomSeed(12345); scanParser.SetRandomSeed(12345);
    fullParser.SetRandomValues({2}); scanParser.SetRandomValues({2});
    std::atomic_bool cancelled{false};
    bms_parser::Chart *raw = nullptr;
    fullParser.Parse(bytes, &raw, false, false, cancelled);
    const std::unique_ptr<bms_parser::Chart> full(raw);
    const auto scan = scanParser.Scan(bytes, cancelled);
    ASSERT_EQ(true, scan.has_value(), "scan returns metadata");
    ASSERT_EQ(true, (parser_test::metadataSnapshot(full->Meta) ==
                        parser_test::metadataSnapshot(scan->Meta)), "scan metadata matches full parsing");
    ASSERT_EQ(test.bga, scan->HasBga, "scan declared BGA");
    ASSERT_EQ(test.stop, scan->HasBpmStop, "scan positive resolved stops");
    ASSERT_EQ(test.scroll, scan->HasScrollChange, "scan effective scroll");
  }
  {
    const std::filesystem::path path("testcases/metadata/example.bme");
    bms_parser::Parser fullParser, scanParser;
    fullParser.SetRandomSeed(9); scanParser.SetRandomSeed(9);
    std::atomic_bool cancelled{false};
    bms_parser::Chart *raw = nullptr;
    fullParser.Parse(path, &raw, false, false, cancelled);
    const std::unique_ptr<bms_parser::Chart> full(raw);
    const auto scan = scanParser.Scan(path, cancelled);
    ASSERT_EQ(true, scan.has_value(), "scan file succeeds");
    ASSERT_EQ(true, (parser_test::metadataSnapshot(full->Meta) ==
                        parser_test::metadataSnapshot(scan->Meta)), "scan preserves file path metadata");
    ASSERT_EQ(false, scanParser.Scan(std::filesystem::path("missing-scan-input.bms"), cancelled).has_value(),
              "scan missing file has no result");
    cancelled = true;
    ASSERT_EQ(false, scanParser.Scan(bytesFromString("#BPM 120\n"), cancelled).has_value(),
              "scan pre-cancelled bytes have no result");
    ASSERT_EQ(false, scanParser.Scan(path, cancelled).has_value(), "scan pre-cancelled file has no result");
  }
  {
    const auto bytes = bytesFromString("#BPM 120\n#00011:01010101\n");
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    // Interrupt after header parsing when the first timeline block is acquired.
    cancelOnAllocationTarget = &cancelled;
    cancelOnAllocationSize = 16 * 1024;
    const auto result = parser.Scan(bytes, cancelled);
    cancelOnAllocationTarget = nullptr;
    cancelOnAllocationSize = 0;
    ASSERT_EQ(true, cancelled.load(), "scan cancellation occurred during parsing");
    ASSERT_EQ(false, result.has_value(), "scan discards partially parsed metadata");
  }
  {
    const auto bytes = bytesFromString("#BPM 137\n#WAV01 note.wav\n#BMP01 image.png\n"
                                       "#STOP01 48\n#00009:01\n#00111:01010101\n");
    std::atomic_bool failed{false};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
      for (int j = 0; j < 32; ++j) {
        bms_parser::Parser parser;
        std::atomic_bool cancelled{false};
        const auto scan = parser.Scan(bytes, cancelled);
        if (!scan || scan->Meta.TotalNotes != 4 || !scan->HasBga ||
            !scan->HasBpmStop || scan->HasScrollChange) failed = true;
      }
    });
    for (auto &worker : workers) worker.join();
    ASSERT_EQ(false, failed.load(), "independent concurrent scans preserve results");
  }
  return 0;
}
