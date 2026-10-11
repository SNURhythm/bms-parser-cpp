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
       "#00009:01\n#00009:02\n#000SC:01\n#000SC:02\n#00111:01\n", false, true, false},
      {"#BPM 120\n#STOP01 48\n#SCROLL01 0.5\n#00009:01\n#00009:ZZ\n"
       "#000SC:01\n#000SC:ZZ\n#00111:01\n", false, true, true},
      {"#BPM 120\n#STOP01 48\n#SCROLL01 0.5\n#00111:01\n", false, false, false},
      {"#BPM 120\n#RANDOM 1\n#IF 3\n#BMP01 hidden.png\n#ENDIF\n#ENDRANDOM\n"
       "#BMP01 \n#00111:01\n", false, false, false},
      {"#BPM 120\n#BMP!1 odd.png\n#00111:01\n", false, false, false},
      {"#bpm 120\n#bmp01 image.png\n#scroll01 nan\n#000sc:01\n#00111:01\n", true, false, false},
      // Legacy metaOnly stops reading BGM/invisible rows after the first event.
      // Scan must use full-mode positions and rounding for all statistics.
      {"#BPM 137\n#BPM01 173\n#WAV01 hit.wav\n#00102:0.75\n#00111:01\n"
       "#00101:010101010101010101010101\n#00231:010101010101\n#00208:0001\n"
       "#00306:000000ZZ\n", false, false, false},
      {"#BPM 120\n#LNOBJ ZZ\n#LNMODE 3\n#00011:01ZZ01ZZ\n#00117:01ZZ\n"
       "#00154:01\n#00354:01\n#004D9:01\n#00501:000001\n", false, false, false},
      {"#BPM 120\n#BASE 62\n#BMPzz image.png\n#WAVzz note.wav\n#00011:zz\n"
       "#00006:zz00zz\n#RANDOM 2\n#IF 1\n#SCROLL01 -1\n#000SC:01\n"
       "#ELSE\n#STOP01 24\n#00009:01\n#ENDIF\n#ENDRANDOM\n", true, false, false},
  };
  bms_parser::ScanWorkspace workspace;
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
    bms_parser::ScanTimings timings;
    const auto reused = scanParser.Scan(bytes, cancelled, {}, workspace, &timings);
    ASSERT_EQ(scan.has_value(), reused.has_value(), "workspace preserves rejected input");
    if (scan) {
      ASSERT_EQ(true, parser_test::metadataSnapshot(scan->Meta) ==
                          parser_test::metadataSnapshot(reused->Meta),
                "workspace reset preserves every metadata field");
      ASSERT_EQ(scan->HasBga, reused->HasBga, "workspace BGA flag");
      ASSERT_EQ(scan->HasBpmStop, reused->HasBpmStop, "workspace stop flag");
      ASSERT_EQ(scan->HasScrollChange, reused->HasScrollChange, "workspace scroll flag");
    }
    ASSERT_EQ(std::uint64_t{0}, timings.ReadNanoseconds, "buffered scan has no file read");
    if (bytes.empty()) {
      ASSERT_EQ(true, full == nullptr && !scan, "empty input matches Java rejection");
      continue;
    }
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
    const auto reused = scanParser.Scan(path, cancelled, workspace);
    ASSERT_EQ(true, reused && parser_test::metadataSnapshot(scan->Meta) ==
                                 parser_test::metadataSnapshot(reused->Meta),
              "workspace file scan preserves paths and metadata");
    ASSERT_EQ(true, scan.has_value(), "scan file succeeds");
    ASSERT_EQ(true, (parser_test::metadataSnapshot(full->Meta) ==
                        parser_test::metadataSnapshot(scan->Meta)), "scan preserves file path metadata");
    ASSERT_EQ(false, scanParser.Scan(std::filesystem::path("missing-scan-input.bms"), cancelled).has_value(),
              "scan missing file has no result");
    cancelled = true;
    ASSERT_EQ(false, scanParser.Scan(bytesFromString("#BPM 120\n"), cancelled).has_value(),
              "scan pre-cancelled bytes have no result");
    ASSERT_EQ(false, scanParser.Scan(path, cancelled).has_value(), "scan pre-cancelled file has no result");
    ASSERT_EQ(false, scanParser.Scan(path, cancelled, workspace).has_value(),
              "workspace pre-cancelled scan has no result");
    cancelled = false;
    const auto recovered = scanParser.Scan(path, cancelled, workspace);
    ASSERT_EQ(true, recovered && parser_test::metadataSnapshot(scan->Meta) ==
                                    parser_test::metadataSnapshot(recovered->Meta),
              "workspace survives cancellation without changing retained results");
  }
  {
    // Reuse one workspace across the encoding/PMS fixtures, and keep prior
    // results alive to catch views accidentally escaping scratch storage.
    std::vector<std::pair<std::string, bms_parser::ChartScanResult>> retained;
    for (const auto &entry : std::filesystem::recursive_directory_iterator("testcases")) {
      const auto extension = entry.path().extension().string();
      if (extension != ".bms" && extension != ".bme" &&
          extension != ".bml" && extension != ".pms") continue;
      bms_parser::Parser parser;
      parser.SetRandomSeed(12345);
      std::atomic_bool cancelled{false};
      const auto expected = parser.Scan(entry.path(), cancelled);
      auto actual = parser.Scan(entry.path(), cancelled, workspace);
      ASSERT_EQ(expected.has_value(), actual.has_value(), "workspace fixture acceptance");
      if (!expected) continue;
      const auto snapshot = parser_test::metadataSnapshot(expected->Meta);
      ASSERT_EQ(true, snapshot == parser_test::metadataSnapshot(actual->Meta),
                "workspace mixed-encoding fixture metadata");
      ASSERT_EQ(expected->HasBga, actual->HasBga, "workspace fixture BGA");
      ASSERT_EQ(expected->HasBpmStop, actual->HasBpmStop, "workspace fixture stop");
      ASSERT_EQ(expected->HasScrollChange, actual->HasScrollChange, "workspace fixture scroll");
      retained.emplace_back(snapshot, std::move(*actual));
    }
    for (const auto &entry : retained) {
      ASSERT_EQ(true, entry.first == parser_test::metadataSnapshot(entry.second.Meta),
                "later scans cannot change earlier results");
    }
  }
  {
    const auto bytes = bytesFromString("#BPM 120\n#00011:01010101\n");
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    // Interrupt after header parsing when the first timeline block is acquired.
    cancelOnAllocationTarget = &cancelled;
    cancelOnAllocationSize = 16 * 1024;
    bms_parser::ScanWorkspace interruptedWorkspace;
    const auto result = parser.Scan(bytes, cancelled, {}, interruptedWorkspace);
    cancelOnAllocationTarget = nullptr;
    cancelOnAllocationSize = 0;
    ASSERT_EQ(true, cancelled.load(), "scan cancellation occurred during parsing");
    ASSERT_EQ(false, result.has_value(), "scan discards partially parsed metadata");
    cancelled = false;
    const auto recovered = parser.Scan(bytes, cancelled, {}, interruptedWorkspace);
    ASSERT_EQ(true, recovered && recovered->Meta.TotalNotes == 4,
              "workspace survives cancellation inside a measure");
  }
  {
    const auto bytes = bytesFromString("#BPM 137\n#WAV01 note.wav\n#BMP01 image.png\n"
                                       "#STOP01 48\n#00009:01\n#00111:01010101\n");
    std::atomic_bool failed{false};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
      bms_parser::ScanWorkspace workerWorkspace;
      for (int j = 0; j < 32; ++j) {
        bms_parser::Parser parser;
        std::atomic_bool cancelled{false};
        const auto scan = parser.Scan(bytes, cancelled, {}, workerWorkspace);
        if (!scan || scan->Meta.TotalNotes != 4 || !scan->HasBga ||
            !scan->HasBpmStop || scan->HasScrollChange) failed = true;
      }
    });
    for (auto &worker : workers) worker.join();
    ASSERT_EQ(false, failed.load(), "independent concurrent scans preserve results");
  }
  return 0;
}
