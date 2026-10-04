#pragma once

inline int runParserStreamingTests() {
  using namespace bms_parser;
  const std::string header = "#BPM 120\n#WAV01 one.wav\n#WAV02 two.wav\n";
  struct Case {
    const char *body;
    int notes, longs, background;
    long long playLength;
  };
  const Case cases[] = {
    {"#00012:01\n#00151:0101\n#00111:0002\n", 3, 0, 0, 3000000},
    {"#00051:01\n#00111:0102\n#00211:01\n#00351:01\n", 1, 1, 3, 6000000},
    {"#00051:01\n#00111:0102\n#00211:01\n", 3, 0, 0, 4000000},
    {"#LNOBJ ZZ\n#00011:01\n#00311:ZZ\n", 1, 1, 0, 6000000},
    {"#00051:01\n#00011:02\n#00251:01\n", 2, 0, 0, 4000000},
    // The last cell rounds to the next measure's start and is overwritten there.
    {"#LNMODE 2\n#BPM 1e20\n#00002:1e16\n#00102:2\n#00051:01\n#00151:00000001\n#00211:02\n", 2, 0, 0, 24000},
  };
  for (const auto &test : cases) {
    Parser parser;
    std::atomic_bool cancelled{false};
    const auto bytes = bytesFromString(header + test.body);
    Chart *raw = nullptr;
    parser.Parse(bytes, &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(true, chart != nullptr, "streamed chart exists");
    ASSERT_EQ(test.notes, chart->Meta.TotalNotes, "cross-measure note count");
    ASSERT_EQ(test.longs, chart->Meta.TotalLongNotes, "cross-measure LN count");
    ASSERT_EQ(test.playLength, chart->Meta.PlayLength, "cross-measure play length");
    int background = 0;
    for (const auto *measure : chart->Measures)
      for (const auto *timeline : measure->TimeLines) {
        background += static_cast<int>(timeline->BackgroundNotes.size());
        for (const auto *note : timeline->Notes) {
          const auto *ln = dynamic_cast<const LongNote *>(note);
          if (!ln) continue;
          ASSERT_EQ(true, ln->Head ? ln->Head->Tail == ln :
                                    ln->Tail && ln->Tail->Head == ln,
                    "streamed LN pair retains reciprocal identity");
        }
      }
    ASSERT_EQ(test.background, background, "removed normal notes become BGM");
    const auto scan = parser.Scan(bytes, cancelled);
    ASSERT_EQ(true, scan.has_value(), "streamed scan exists");
    ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta),
              parser_test::metadataSnapshot(scan->Meta), "full and streamed Scan metadata");
  }
  {
    // Collection must retain the overwritten open head long enough to decode
    // its later close, whose surviving tail must then become a normal note.
    std::string body = header + "#00051:01\n#00011:02\n";
    for (const char *channel : {"#00012:", "#00112:"}) {
      body += channel;
      for (int i = 0; i < 2048; ++i) body += "01";
      body += '\n';
    }
    body += "#00351:01\n";
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    const auto bytes = bytesFromString(body);
    parser.Parse(bytes, &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(4098, chart->Meta.TotalNotes, "retired normal notes counted once");
    ASSERT_EQ(size_t{0}, chart->DetachedNotes.size(), "malformed pairs need no detached ownership");
    const auto *tail = chart->Measures.back()->TimeLines.front()->Notes[0];
    ASSERT_EQ(true, tail && !dynamic_cast<const LongNote *>(tail), "surviving tail becomes normal");
    ASSERT_EQ(Parser::NoWav, tail->Wav, "demotion preserves silent release sound");
    const auto scan = parser.Scan(bytes, cancelled);
    ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta),
              parser_test::metadataSnapshot(scan->Meta), "collected chart Scan metadata");
  }
  {
    // Queued classic heads must survive collection without triggering a full
    // chart scan. No active note precedes one second, so no start offset applies.
    std::string body = header + "#00151:0101\n#00111:0002\n";
    for (int measure = 2; measure < 8; ++measure) {
      body += "#00" + std::to_string(measure) + "12:";
      for (int i = 0; i < 1024; ++i) body += "01";
      body += '\n';
    }
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    const auto bytes = bytesFromString(body);
    parser.Parse(bytes, &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(6146, chart->Meta.TotalNotes, "queued classic head counted once after collection");
    ASSERT_EQ(1, chart->Meta.TotalLongNotes, "healthy classic hold retained after collection");
    ASSERT_EQ(size_t{1}, chart->DetachedNotes.size(), "deferred classic tail ownership retained");
    const auto scan = parser.Scan(bytes, cancelled);
    ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta), parser_test::metadataSnapshot(scan->Meta),
              "queued classic candidate Full and Scan agree");
  }
  {
    std::string body = header + "#00051:01\n";
    for (int measure = 1; measure <= 5; ++measure) {
      body += "#00" + std::to_string(measure) + "11:";
      for (int i = 0; i < 1000; ++i) body += "01";
      body += '\n';
    }
    body += "#00951:01\n";
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    const auto bytes = bytesFromString(body);
    parser.Parse(bytes, &raw, false, false, cancelled);
    const std::unique_ptr<Chart> chart(raw);
    ASSERT_EQ(1, chart->Meta.TotalNotes, "long-open hold removes all interior notes");
    int background = 0;
    for (const auto *measure : chart->Measures)
      for (const auto *timeline : measure->TimeLines)
        background += static_cast<int>(timeline->BackgroundNotes.size());
    ASSERT_EQ(5000, background, "long-open hold preserves interior keysounds");
    const auto scan = parser.Scan(bytes, cancelled);
    ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta),
              parser_test::metadataSnapshot(scan->Meta), "long-open hold Scan metadata");
  }
  {
    Parser parser;
    std::atomic_bool cancelled{false};
    Chart *raw = nullptr;
    const auto bytes = bytesFromString(header + "#00001:**\n#00111:01\n");
    parser.Parse(bytes, &raw, false, false, cancelled);
    std::unique_ptr<Chart> chart(raw);
    int metronomes = 0;
    for (const auto *measure : chart->Measures)
      for (const auto *timeline : measure->TimeLines)
        for (const auto *note : timeline->BackgroundNotes)
          if (note->Wav == Parser::MetronomeWav) ++metronomes;
    ASSERT_EQ(0, metronomes, "ordinary chart input cannot create internal metronome notes");
    parser.Parse(bytes, &raw, true, false, cancelled);
    chart.reset(raw);
    metronomes = 0;
    for (const auto *measure : chart->Measures)
      for (const auto *timeline : measure->TimeLines)
        for (const auto *note : timeline->BackgroundNotes)
          if (note->Wav == Parser::MetronomeWav) ++metronomes;
    ASSERT_EQ(4, metronomes, "ready measure still generates four metronome notes");
  }
  return 0;
}
