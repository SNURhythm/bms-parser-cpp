#include <set>

// Both schemas must reach the same playable timeline through the public API.
int runBmsonTests() {
  // Mode.java and BMSONDecoder.keyassign: beat-5/10 retain the sparse
  // eight-channel namespace; keyboard wheels occupy two lanes per player.
  struct ModeCase {
    const char *hint;
    int keyMode, width, notes, scratches;
    bool dp;
  };
  for (const auto &mode : {
      ModeCase{"popn-5k", 9, 5, 5, 0, false},
      ModeCase{"keyboard-24k", 24, 26, 26, 2, false},
      ModeCase{"keyboard-24k-double", 48, 52, 52, 4, true},
      ModeCase{"beat-5k", 5, 8, 6, 1, false},
      ModeCase{"beat-7k", 7, 8, 8, 1, false},
      ModeCase{"beat-10k", 10, 16, 12, 2, true},
      ModeCase{"beat-14k", 14, 16, 16, 2, true},
      ModeCase{"popn-9k", 9, 9, 9, 0, false}}) {
    std::string input = "{\"version\":\"1.0.0\",\"info\":{\"init_bpm\":120,\"mode_hint\":\"" +
        std::string(mode.hint) + "\"},\"sound_channels\":[{\"name\":\"keys.wav\",\"notes\":[";
    for (int x = 1; x <= mode.width + 1; ++x) {
      if (x > 1) input += ",";
      input += "{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(x * 240) + "}";
    }
    input += "]}]}";
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    bms_parser::Chart *raw = nullptr;
    parser.Parse(bytesFromString(input), &raw, true, false, cancelled, "mode.bmson");
    std::unique_ptr<bms_parser::Chart> chart(raw);
    ASSERT_EQ(true, bool(chart), mode.hint);
    ASSERT_EQ(mode.keyMode, chart->Meta.KeyMode, "mode identity");
    ASSERT_EQ(mode.dp, chart->Meta.IsDP, "mode players");
    ASSERT_EQ(mode.notes, chart->Meta.TotalNotes, "all valid lanes playable");
    ASSERT_EQ(mode.scratches, chart->Meta.TotalScratchNotes, "wheel/scratch counts");
    ASSERT_EQ(mode.notes, chart->Meta.GetTotalLaneCount(), "mode lane count");
    ASSERT_EQ(size_t(mode.notes), chart->Meta.GetTotalLaneIndices().size(), "complete lane map");
    int playable = 0, background = 0;
    for (const auto *measure : chart->Measures) for (const auto *timeline : measure->TimeLines) {
      ASSERT_EQ(true, timeline->Notes.size() >= size_t(mode.width), "ready and chart lane capacity");
      for (const auto *note : timeline->Notes) if (note) {
        ++playable;
        ASSERT_EQ((note->Lane + 1) * 500000LL + 2000000LL, timeline->Timing, "authored lane retained");
      }
      for (const auto *note : timeline->BackgroundNotes)
        if (note->Wav != bms_parser::Parser::MetronomeWav) ++background;
    }
    ASSERT_EQ(mode.notes, playable, "all mode notes materialized");
    ASSERT_EQ(mode.width + 1 - mode.notes, background, "out-of-range lanes become BGM");
    const auto scan = parser.Scan(bytesFromString(input), cancelled, "mode.bmson");
    ASSERT_EQ(true, bool(scan), "all modes scan");
    ASSERT_EQ(mode.notes, scan->Meta.TotalNotes, "scan mode counts");
    ASSERT_EQ(mode.scratches, scan->Meta.TotalScratchNotes, "scan scratch counts");
  }
  {
    // Hidden keys and mines use whole-source sounds, not sound-channel slices.
    // Mines on normal/LN endpoints and inside holds are discarded like Java.
    const std::string input = R"({"version":"1.0.0","info":{"init_bpm":120,"mode_hint":"keyboard-24k-double"},"sound_channels":[{"name":"keys.wav","notes":[{"x":27,"y":240,"l":480}]}],"key_channels":[{"name":"old.wav","notes":[{"x":52,"y":0}]},{"name":"hidden.wav","notes":[{"x":52,"y":0}]}],"mine_channels":[{"name":"mine.wav","notes":[{"x":52,"y":240,"damage":7.5},{"x":27,"y":240,"damage":9},{"x":27,"y":480,"damage":9},{"x":27,"y":720,"damage":9},{"x":27,"y":960,"damage":3},{"x":53,"y":1200,"damage":9}]}]})";
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    bms_parser::Chart *raw = nullptr;
    parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> chart(raw);
    ASSERT_EQ(true, bool(chart), "auxiliary channels accepted");
    ASSERT_EQ(2, chart->Meta.TotalLandmineNotes, "only nonconflicting mines survive");
    ASSERT_EQ(1, chart->Meta.TotalNotes, "hidden notes and mines do not add scoring notes");
    ASSERT_EQ(2000000LL, chart->Meta.PlayLength, "last mine extends play length");
    auto *hidden = chart->Measures.front()->TimeLines.front()->InvisibleNotes[51];
    ASSERT_EQ(true, hidden != nullptr, "high-lane hidden keysound retained");
    ASSERT_EQ(std::string("hidden.wav"), chart->WavTable.at(hidden->Wav), "last hidden assignment wins");
    ASSERT_EQ(true, chart->ReferencedWavTable.count(hidden->Wav) != 0, "hidden keysound loaded");
    int mines = 0;
    for (const auto *measure : chart->Measures) for (const auto *timeline : measure->TimeLines)
      for (const auto *mine : timeline->LandmineNotes) if (mine) {
        ++mines;
        ASSERT_EQ(std::string("mine.wav"), chart->WavTable.at(mine->Wav), "authored mine sound retained");
        ASSERT_EQ(true, chart->ReferencedWavTable.count(mine->Wav) != 0, "mine sound loaded");
        ASSERT_EQ(mine->Lane == 51 ? 7.5F : 3.0F, mine->Damage, "mine damage retained");
      }
    ASSERT_EQ(2, mines, "mines materialized");
    const auto scan = parser.Scan(bytesFromString(input), cancelled);
    ASSERT_EQ(2, scan->Meta.TotalLandmineNotes, "scan mine counts");
    ASSERT_EQ(2000000LL, scan->Meta.PlayLength, "scan mine duration");
  }
  {
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    for (int rank : {-1, 2, 100}) {
      const std::string input = "{\"version\":\"1.0.0\",\"info\":{\"init_bpm\":120,\"resolution\":-480,\"judge_rank\":" +
          std::to_string(rank) + "},\"sound_channels\":[{\"name\":\"a.wav\",\"notes\":[{\"x\":1,\"y\":240}]}]}";
      const auto scan = parser.Scan(bytesFromString(input), cancelled);
      ASSERT_EQ(true, bool(scan), "compatibility defaults accepted");
      ASSERT_EQ(500000LL, scan->Meta.PlayLength, "nonpositive resolution falls back to 240");
      ASSERT_EQ(rank < 0 ? 2 : rank, scan->Meta.Rank, "negative rank preserves default");
      ASSERT_EQ(true, scan->Meta.RankType == (rank < 5 ? bms_parser::JudgeRankType::BmsRank : bms_parser::JudgeRankType::DefExRank), "small rank uses BMS judgement table");
    }
  }
  {
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    bms_parser::Chart *raw = nullptr;
    parser.Parse(bytesFromString(R"({"version":"1.0.0","info":{"init_bpm":120,"mode_hint":"keyboard-24k-double"},"sound_channels":[{"name":"a.wav","notes":[{"x":1,"y":0}]}]})"), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> chart(raw);
    auto modifier = bms_parser::CreatePlayOptionModifier("RANDOM-EX", 42, 1);
    modifier->Modify(*chart);
    const auto lanes = modifier->GetLaneOrder(chart->Meta);
    ASSERT_EQ(size_t(52), lanes.size(), "keyboard double modifier retains full lane map");
    for (int lane = 0; lane < 26; ++lane)
      ASSERT_EQ(lane, lanes[lane], "P2 shuffle cannot move P1 keys or wheels");
    std::set<int> destinations;
    for (int lane = 26; lane < 52; ++lane) {
      ASSERT_EQ(true, lanes[lane] >= 26 && lanes[lane] < 52, "P2 shuffle stays on its deck");
      destinations.insert(lanes[lane]);
    }
    ASSERT_EQ(size_t(26), destinations.size(), "P2 shuffle includes both wheel lanes without duplication");
  }
  const std::string modern = R"({"version":"1.0.0","info":{"title":"bmson test","artist":"artist","init_bpm":120,"resolution":480},"bpm_events":[{"y":480,"bpm":60}],"stop_events":[{"y":480,"duration":240},{"y":480,"duration":240}],"sound_channels":[{"name":"stem.wav","notes":[{"x":1,"y":0,"l":0,"c":false},{"x":2,"y":480,"l":480,"c":true},{"x":8,"y":960,"l":0,"c":true}]}]})";
  const std::string legacy = R"({"info":{"title":"bmson test","artist":"artist","initBPM":120},"bpmNotes":[{"y":240,"v":60}],"stopNotes":[{"y":240,"v":120},{"y":240,"v":120}],"soundChannel":[{"name":"stem.wav","notes":[{"x":1,"y":0,"l":0,"c":false},{"x":2,"y":240,"l":240,"c":true},{"x":8,"y":480,"l":0,"c":true}]}]})";
  for (const auto &source : {modern, legacy}) {
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    bms_parser::Chart *raw = nullptr;
    const std::vector<unsigned char> bytes(source.begin(), source.end());
    parser.Parse(bytes, &raw, false, false, cancelled, "song.BMSON");
    std::unique_ptr<bms_parser::Chart> chart(raw);
    ASSERT_EQ(true, bool(chart), "bmson parsed");
    ASSERT_EQ(std::string("bmson test"), chart->Meta.Title, "bmson title");
    ASSERT_EQ(7, chart->Meta.KeyMode, "bmson beat-7k");
    ASSERT_EQ(3, chart->Meta.TotalNotes, "bmson note count");
    ASSERT_EQ(100.0, chart->Meta.Total, "bmson authored TOTAL retained");
    ASSERT_EQ(source == modern, chart->Meta.TotalIsRelative, "version-specific TOTAL semantics");
    ASSERT_EQ(2500000LL, chart->Meta.PlayLength, "bmson BPM and accumulated stops");
    std::vector<bms_parser::Note *> heads;
    for (auto *measure : chart->Measures) for (auto *timeline : measure->TimeLines)
      for (auto *note : timeline->Notes) if (note &&
          (!note->IsLongNote() || !static_cast<bms_parser::LongNote *>(note)->IsTail())) heads.push_back(note);
    ASSERT_EQ(size_t(3), heads.size(), "bmson playable heads");
    const auto &first = chart->WavSlices.at(heads[0]->Wav).front();
    const auto &second = chart->WavSlices.at(heads[1]->Wav).front();
    const auto &third = chart->WavSlices.at(heads[2]->Wav).front();
    ASSERT_EQ(0LL, first.StartMicros, "slice restart");
    ASSERT_EQ(500000LL, first.DurationMicros, "slice first duration");
    ASSERT_EQ(500000LL, second.StartMicros, "slice continuation start");
    ASSERT_EQ(2000000LL, second.DurationMicros, "slice includes stops");
    ASSERT_EQ(2500000LL, third.StartMicros, "slice final offset");
    ASSERT_EQ(-1LL, third.DurationMicros, "slice final tail");
    auto *ln = dynamic_cast<bms_parser::LongNote *>(heads[1]);
    ASSERT_EQ(true, ln && ln->Tail && ln->Tail->Head == ln, "bmson LN pairing");
    auto scan = parser.Scan(bytes, cancelled, "song.bmson");
    ASSERT_EQ(true, bool(scan), "bmson scan");
    ASSERT_EQ(chart->Meta.SHA256, scan->Meta.SHA256, "bmson raw source hash");
    ASSERT_EQ(chart->Meta.TotalNotes, scan->Meta.TotalNotes, "bmson scan notes");
    ASSERT_EQ(chart->Meta.PlayLength, scan->Meta.PlayLength, "bmson scan timing");
    ASSERT_EQ(true, scan->HasBpmStop, "bmson scan stop flag");
  }
  {
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    const std::vector<std::string> bad = {
      R"({"version":null,"info":{"init_bpm":120}})",
      R"({"version":"2.0.0","info":{"init_bpm":120}})",
      R"({"version":"1.0.0","info":{"init_bpm":0}})",
      R"({"version":"1.0.0","info":{"init_bpm":120,"mode_hint":"unknown"}})",
      R"({"version":"1.0.0","info":{"init_bpm":120},"sound_channels":[{"notes":[{"y":-1}]}]})",
      R"({"version":"1.0.0","info":{"init_bpm":120},"sound_channels":false})",
      R"({"version":"1.0.0","info":{"init_bpm":120}} garbage)"
    };
    for (const auto &input : bad) {
      bms_parser::Chart *raw = nullptr;
      parser.Parse(bytesFromString(input), &raw, false, false, cancelled, "invalid.bmson");
      std::unique_ptr<bms_parser::Chart> chart(raw);
      ASSERT_EQ(false, bool(chart), "invalid bmson rejected");
    }
    cancelled = true;
    ASSERT_EQ(false, bool(parser.Scan(bytesFromString(modern), cancelled)), "bmson cancellation");
  }
  {
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    const std::string layered = R"({"version":"1.0.0","info":{"init_bpm":120},"lines":[],"sound_channels":[{"name":"a.wav","notes":[{"x":1,"y":0},{"x":2,"y":240,"c":true},{"x":3,"y":240,"c":true},{"x":4,"y":480}]},{"name":"b.wav","notes":[{"x":1,"y":0}]}],"bga":{"bga_header":[{"id":42,"name":"image.png"}],"bga_events":[{"y":240,"id":42}],"layer_events":[{"y":0,"id":42}],"poor_events":[{"y":0,"id":42}]}})";
    bms_parser::Chart *raw = nullptr;
    parser.Parse(bytesFromString(layered), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> chart(raw);
    ASSERT_EQ(true, bool(chart), "bmson content sniffing");
    ASSERT_EQ(4, chart->Meta.TotalNotes, "layered notes count once");
    auto *first = chart->Measures.front()->TimeLines.front();
    ASSERT_EQ(size_t(2), chart->WavSlices.at(first->Notes[0]->Wav).size(), "both layers retained");
    ASSERT_EQ(false, first->IsFirstInMeasure, "empty lines suppress bars");
    ASSERT_EQ(42, first->BgaLayer, "modern BGA layer");
    ASSERT_EQ(42, first->BgaPoor->Frames.front(), "modern BGA poor");
    auto *chord = chart->Measures.front()->TimeLines[1];
    ASSERT_EQ(chord->Notes[1]->Wav, chord->Notes[2]->Wav, "same-pulse slices reused");
    ASSERT_EQ(-1LL, chart->WavSlices.at(chord->Notes[1]->Wav).front().DurationMicros, "restart does not truncate previous sound");
    bms_parser::Chart moved(std::move(*chart));
    ASSERT_EQ(false, moved.WavSlices.empty(), "move preserves slices");
  }
  {
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    const std::string input = R"({"version":"0.2.1","info":{"title":"legacy","initBPM":120,"resolution":1,"total":300},"soundChannel":[{"name":"a.wav","notes":[{"x":8,"y":240}]}],"bga":{"bgaHeader":[{"ID":7,"name":"a.png"}],"bgaNotes":[{"y":240,"ID":7}],"layerNotes":[],"poorNotes":[]}})";
    bms_parser::Chart *raw = nullptr;
    parser.Parse(bytesFromString(input), &raw, true, false, cancelled);
    std::unique_ptr<bms_parser::Chart> chart(raw);
    ASSERT_EQ(true, bool(chart), "explicit legacy version");
    ASSERT_EQ(2500000LL, chart->Meta.PlayLength, "legacy fixed resolution and ready measure");
    ASSERT_EQ(300.0, chart->Meta.Total, "legacy absolute TOTAL");
    ASSERT_EQ(size_t(1), chart->ReferencedBmpTable.size(), "legacy uppercase BGA ID");
    ASSERT_EQ(bms_parser::Parser::MetronomeWav, chart->Measures.front()->TimeLines.front()->BackgroundNotes.front()->Wav, "ready click");
  }
  {
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    const std::string input = R"({"version":"1.0.0","info":{"init_bpm":120,"banner_image":"art\\banner.png"},"sound_channels":[{"name":"audio\\sound.wav","notes":[{"x":1,"y":0},{"x":2,"y":0},{"x":0,"y":0},{"x":1,"y":240}]}],"bga":{"bga_header":[{"id":1,"name":"art\\bga.png"}]}})";
    bms_parser::Chart *raw = nullptr;
    parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> chart(raw);
    ASSERT_EQ(true, bool(chart), "resource paths parse");
    ASSERT_EQ(std::string("audio/sound.wav"), chart->WavTable.at(1), "sound separator normalized");
    ASSERT_EQ(std::string("art/bga.png"), chart->BmpTable.at(1), "BGA separator normalized");
    ASSERT_EQ(std::filesystem::path("art/banner.png"), chart->Meta.Banner, "artwork separator normalized");
    auto *first = chart->Measures.front()->TimeLines.front();
    auto *second = chart->Measures.front()->TimeLines[1];
    ASSERT_EQ(size_t(0), first->BackgroundNotes.size(), "playable slice discards duplicate BGM");
    ASSERT_EQ(first->Notes[0]->Wav, first->Notes[1]->Wav, "chord shares slice identity");
    ASSERT_EQ(false, first->Notes[0]->Wav == second->Notes[0]->Wav, "distinct restarts remain polyphonic");
    for (const std::string path : {"../bad.wav", "/bad.wav", "C:/bad.wav"}) {
      const std::string invalid = "{\"version\":\"1.0.0\",\"info\":{\"init_bpm\":120},\"sound_channels\":[{\"name\":\"" + path + "\"}]}";
      ASSERT_EQ(false, bool(parser.Scan(bytesFromString(invalid), cancelled)), "unsafe resource rejected");
    }
  }
  return 0;
}
