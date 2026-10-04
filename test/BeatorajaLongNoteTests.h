#pragma once

#include <unordered_set>
#include "ChartSnapshot.h"

// Decode collisions like beatoraja, then demote surviving malformed LN
// endpoints to normal notes. Ordinary unclosed LN-channel heads stay discarded.
inline int runBeatorajaLongNoteTests() {
  using namespace bms_parser;
  struct Case {
    const char *body;
    int heads, tails, detached, normals, nulls = 0;
    bool retainedClassicTail = false;
  };
  const Case cases[] = {
    {"#00151:0101\n", 1, 1, 0, 0},
    {"#00151:0101\n#00111:0002\n", 0, 0, 0, 2, 0, true},
    {"#00151:0101\n#00111:02\n", 0, 0, 0, 2},
    {"#00051:0101\n#00011:0002\n", 0, 0, 0, 2},
    {"#00052:01\n#00151:0101\n#00111:0002\n", 0, 0, 0, 2, 0, true},
    {"#00151:01\n", 0, 0, 0, 0},
    {"#LNOBJ 02\n#00111:01\n", 0, 0, 0, 1},
    {"#LNOBJ 02\n#00111:02\n", 0, 0, 0, 0},
    {"#00002:5e-324\n#00151:01\n", 0, 0, 0, 1},
  };
  for (const std::filesystem::path source : {"fixture.bms", "fixture.pms"})
  for (bool scratch : {false, true}) for (int mode : {1, 2, 3}) {
    if (source.extension() == ".pms" && scratch) continue;
    const std::string header = "#BPM 120\n#WAV01 a.wav\n#WAV02 b.wav\n#LNMODE " +
                               std::to_string(mode) + "\n";
    for (auto test : cases) {
      if (test.retainedClassicTail && mode == 1) {
        test.heads = 1;
        test.detached = 1;
        test.normals = 1;
      }
      Parser parser;
      std::atomic_bool cancelled{false};
      std::string body = test.body;
      if (scratch) {
        for (size_t at = 0; (at = body.find("51:", at)) != std::string::npos; ++at)
          body.replace(at, 3, "56:");
        for (size_t at = 0; (at = body.find("11:", at)) != std::string::npos; ++at)
          body.replace(at, 3, "16:");
      }
      const auto bytes = bytesFromString(header + body);
      Chart *raw = nullptr;
      parser.Parse(bytes, &raw, false, false, cancelled, source);
      const std::unique_ptr<Chart> chart(raw);
      ASSERT_EQ(true, chart != nullptr, "beatoraja LN fixture parses");
      std::unordered_set<const Note *> active;
      for (const auto *measure : chart->Measures)
        for (const auto *timeline : measure->TimeLines)
          for (const auto *note : timeline->Notes)
            if (note) active.insert(note);
      int heads = 0, tails = 0, detached = 0, drawCandidates = 0, nulls = 0, normals = 0;
      for (const auto *note : active) {
        const auto *ln = dynamic_cast<const LongNote *>(note);
        if (!ln) {
          ++normals;
          continue;
        }
        const auto *pair = ln->IsTail() ? ln->Head : ln->Tail;
        if (ln->IsTail()) ++tails;
        else ++heads;
        if (!pair) {
          ++nulls;
          continue;
        }
        ASSERT_EQ(true, ln->IsTail() ? pair->Tail == ln : pair->Head == ln,
                  "beatoraja LN pair is reciprocal");
        detached += active.count(pair) == 0;
        if (!ln->IsTail()) {
          ASSERT_EQ(3000000LL, pair->Timeline->Timing, "detached tail retains hold end time");
          if (pair->Timeline->Timing >= 0) ++drawCandidates;
        }
      }
      ASSERT_EQ(test.heads, heads, "beatoraja active head count");
      ASSERT_EQ(test.normals, normals, "beatoraja unmatched LNOBJ candidate stays normal");
      ASSERT_EQ(test.tails, tails, "beatoraja active tail count");
      ASSERT_EQ(test.detached, detached, "beatoraja retained partner count");
      ASSERT_EQ(test.nulls, nulls, "no null LN pairs survive finalization");
      ASSERT_EQ(test.heads - test.nulls, drawCandidates, "beatoraja heads with usable drawing predicates");
      const int countedLongs = test.heads + (mode == 1 ? 0 : test.tails);
      ASSERT_EQ(test.normals + countedLongs, chart->Meta.TotalNotes,
                "beatoraja counts surviving playable slots");
      ASSERT_EQ(scratch ? 0 : countedLongs, chart->Meta.TotalLongNotes,
                "only valid pairs contribute long-note counts");
      ASSERT_EQ(scratch ? countedLongs : 0, chart->Meta.TotalBackSpinNotes,
                "valid scratch holds retain their count");
      ASSERT_EQ(scratch ? test.normals : 0, chart->Meta.TotalScratchNotes,
                "demoted scratch endpoints count as normal scratches");
      ASSERT_EQ(static_cast<size_t>(test.detached), chart->DetachedNotes.size(), "classic heads retain usable tails");
      const auto scan = parser.Scan(bytes, cancelled, source);
      ASSERT_EQ(true, scan.has_value(), "beatoraja LN fixture scans");
      ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta),
                parser_test::metadataSnapshot(scan->Meta), "beatoraja Full and Scan agree");
      Chart *metadataRaw = nullptr;
      parser.Parse(bytes, &metadataRaw, false, true, cancelled, source);
      const std::unique_ptr<Chart> metadataOnly(metadataRaw);
      ASSERT_EQ(true, metadataOnly != nullptr, "beatoraja LN metadata-only parse accepts the chart");
      ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta),
                parser_test::metadataSnapshot(metadataOnly->Meta), "beatoraja Full and metadata-only agree");
      if (test.detached) {
        auto *head = const_cast<LongNote *>(static_cast<const LongNote *>(*std::find_if(active.begin(), active.end(),
            [](const Note *note) { return dynamic_cast<const LongNote *>(note) != nullptr; })));
        auto *tail = head->Tail;
        // Model a chart with no authored LNMODE, finalized after player choice.
        head->Type = tail->Type = LongNoteType::Undefined;
        auto *timeline = head->Timeline;
        const auto lane = head->Lane;
        ASSERT_EQ(static_cast<Note *>(head), timeline->DemoteUnusableLongNote(lane, LongNoteType::LongNote),
                  "healthy classic detached-tail hold remains an LN");
        auto *normal = timeline->DemoteUnusableLongNote(lane, LongNoteType::ChargeNote);
        ASSERT_EQ(true, !normal->IsLongNote(), "selected CN demotes unusable detached-tail hold");
        ASSERT_EQ(1, normal->Wav, "mode finalization preserves keysound");
        ASSERT_EQ(true, tail->Head == nullptr, "retained owner has no dangling reference");
        ASSERT_EQ(normal, timeline->DemoteUnusableLongNote(lane, LongNoteType::ChargeNote),
                  "same-mode finalization is idempotent");
      }
    }
  }
  return 0;
}
