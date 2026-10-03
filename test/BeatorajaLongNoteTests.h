#pragma once

#include <unordered_set>
#include "ChartSnapshot.h"

// The matching Java consumer probe is ProbeBeatorajaLongNotes.java. An active
// head draws using its pair even when that pair is outside the playable slots;
// an active tail has no independent drawing but can still count for CN/HCN.
inline int runBeatorajaLongNoteTests() {
  using namespace bms_parser;
  struct Case {
    const char *body;
    int heads, tails, detached, normals;
  };
  const Case cases[] = {
    {"#00151:0101\n", 1, 1, 0, 0},
    {"#00151:0101\n#00111:0002\n", 1, 0, 1, 1},
    {"#00151:0101\n#00111:02\n", 0, 1, 1, 1},
    {"#00151:01\n", 0, 0, 0, 0},
  };
  for (int mode : {1, 2, 3}) {
    const std::string header = "#BPM 120\n#WAV01 a.wav\n#WAV02 b.wav\n#LNMODE " +
                               std::to_string(mode) + "\n";
    for (const auto &test : cases) {
      Parser parser;
      std::atomic_bool cancelled{false};
      const auto bytes = bytesFromString(header + test.body);
      Chart *raw = nullptr;
      parser.Parse(bytes, &raw, false, false, cancelled);
      const std::unique_ptr<Chart> chart(raw);
      ASSERT_EQ(true, chart != nullptr, "beatoraja LN fixture parses");
      std::unordered_set<const Note *> active;
      for (const auto *measure : chart->Measures)
        for (const auto *timeline : measure->TimeLines)
          for (const auto *note : timeline->Notes)
            if (note) active.insert(note);
      int heads = 0, tails = 0, detached = 0, drawCandidates = 0;
      for (const auto *note : active) {
        const auto *ln = dynamic_cast<const LongNote *>(note);
        if (!ln) continue;
        const auto *pair = ln->IsTail() ? ln->Head : ln->Tail;
        ASSERT_EQ(true, pair != nullptr, "beatoraja renderable LN has a partner");
        ASSERT_EQ(true, ln->IsTail() ? pair->Tail == ln : pair->Head == ln,
                  "beatoraja LN pair is reciprocal");
        detached += active.count(pair) == 0;
        if (ln->IsTail()) ++tails;
        else {
          ++heads;
          ASSERT_EQ(3000000LL, pair->Timeline->Timing, "detached tail retains hold end time");
          if (pair->Timeline->Timing >= 0) ++drawCandidates;
        }
      }
      ASSERT_EQ(test.heads, heads, "beatoraja active head count");
      ASSERT_EQ(test.tails, tails, "beatoraja active tail count");
      ASSERT_EQ(test.detached, detached, "beatoraja retained partner count");
      ASSERT_EQ(test.heads, drawCandidates, "beatoraja head-driven drawing candidates");
      const int countedLongs = test.heads + (mode == 1 ? 0 : test.tails);
      ASSERT_EQ(test.normals + countedLongs, chart->Meta.TotalNotes,
                "beatoraja counts surviving playable slots");
      ASSERT_EQ(countedLongs, chart->Meta.TotalLongNotes,
                "beatoraja CN/HCN tails remain countable without active head");
      const auto scan = parser.Scan(bytes, cancelled);
      ASSERT_EQ(true, scan.has_value(), "beatoraja LN fixture scans");
      ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta),
                parser_test::metadataSnapshot(scan->Meta), "beatoraja Full and Scan agree");
    }
  }
  return 0;
}
