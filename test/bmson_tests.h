#include <set>

// Exercise sequence decoding through the existing playback projection first.
int runBmsonBgaTests() {
  const std::string input = R"({"version":"1.0.0","info":{"init_bpm":120},"bga":{
    "bga_header":[{"id":42,"name":"first.png"},{"id":77,"name":"second.png"},
                  {"id":91,"name":"alternate.png"},{"id":123,"name":"unused.png"}],
    "bga_sequence":[
      {"id":100,"sequence":[{"time":-10,"id":42},{"time":125,"id":77},{"time":500}]},
      {"id":101,"sequence":[{"time":30,"id":91},{"time":9000000000,"id":-2147483648}]},
      {"id":102,"sequence":[]},
      {"id":103,"sequence":[{"time":0,"id":77},{"time":500}]}],
    "bga_events":[{"y":0,"id":42},{"y":1440,"id":999}],
    "layer_events":[{"y":0,"id":77},
      {"y":240,"condition":"miss","interval":-3,"id_set":[100,101]},
      {"y":480,"condition":"play","interval":2,"id":77},
      {"y":720,"condition":"miss","id":100},
      {"y":1440,"id":999},
      {"y":1680,"condition":"miss","id_set":[]}],
    "poor_events":[{"y":720,"id":103},{"y":960,"id":102},{"y":1200,"id":42}]
  }})";
  bms_parser::Parser parser;
  std::atomic_bool cancelled{false};
  bms_parser::Chart *raw = nullptr;
  parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
  std::unique_ptr<bms_parser::Chart> chart(raw);
  ASSERT_EQ(true, bool(chart), "extended BGA parsed");
  std::map<long long, bms_parser::TimeLine *> timelines;
  for (auto *measure : chart->Measures) for (auto *timeline : measure->TimeLines)
    timelines[timeline->Timing] = timeline;
  ASSERT_EQ(42, timelines.at(0)->BgaBase, "arbitrary base header ID retained");
  ASSERT_EQ(77, timelines.at(0)->BgaLayer, "standard layer playback retained");
  const auto *miss = timelines.at(500000);
  ASSERT_EQ(-1, miss->BgaLayer, "conditional MISS is never an unconditional layer");
  ASSERT_EQ(true, miss->BgaPoor.has_value(), "MISS sequence projected for playback");
  ASSERT_EQ(size_t(3), miss->BgaPoor->Frames.size(), "all first-sequence frames projected");
  ASSERT_EQ(42, miss->BgaPoor->Frames[0], "sequence resolves authored header ID");
  ASSERT_EQ(77, miss->BgaPoor->Frames[1], "sequence advances to second image");
  ASSERT_EQ(bms_parser::BgaSequenceBlank, miss->BgaPoor->Frames[2], "omitted frame ID is END");
  ASSERT_EQ(-1, timelines.at(1000000)->BgaLayer, "conditional PLAY is never an unconditional layer");
  ASSERT_EQ(false, timelines.at(1000000)->BgaPoor.has_value(), "PLAY does not become MISS");
  const auto *poor = timelines.at(1500000);
  ASSERT_EQ(77, poor->BgaPoor->Frames.front(), "poor event overrides same-pulse conditional layer");
  ASSERT_EQ(bms_parser::BgaSequenceBlank, poor->BgaPoor->Frames.back(), "explicit Java END sentinel supported");
  ASSERT_EQ(size_t(0), timelines.at(2000000)->BgaPoor->Frames.size(), "empty sequence remains a blank miss");
  ASSERT_EQ(size_t(2), timelines.at(2500000)->BgaPoor->Frames.size(), "direct poor fallback includes terminal END");
  ASSERT_EQ(42, timelines.at(2500000)->BgaPoor->Frames.front(), "direct poor fallback resolves header ID");
  ASSERT_EQ(bms_parser::BgaSequenceBlank, timelines.at(2500000)->BgaPoor->Frames.back(), "direct poor fallback terminates");
  ASSERT_EQ(-1, timelines.at(3000000)->BgaBase, "undefined direct base ignored");
  ASSERT_EQ(-1, timelines.at(3000000)->BgaLayer, "undefined direct layer ignored");
  ASSERT_EQ(true, timelines.at(3500000)->BgaPoor.has_value(), "empty id_set remains an effective MISS");
  ASSERT_EQ(true, timelines.at(3500000)->BgaPoor->Frames.empty(), "empty id_set is a blank miss");
  ASSERT_EQ(size_t(3), chart->ReferencedBmpTable.size(), "all reachable alternatives loaded, unused headers excluded");
  ASSERT_EQ(std::string("alternate.png"), chart->ReferencedBmpTable.at(91), "alternative sequence image loaded");
  ASSERT_EQ(size_t(1), miss->BgaEvents.size(), "effective event retained");
  const auto &event = miss->BgaEvents.front();
  ASSERT_EQ(true, event.Condition == bms_parser::BgaEventCondition::Miss, "MISS condition retained");
  ASSERT_EQ(-3, event.Interval, "negative Java interval retained");
  ASSERT_EQ(size_t(2), event.Sequences.size(), "all id_set alternatives retained");
  ASSERT_EQ(-10LL, (*event.Sequences[0])[0].TimeMillis, "signed authored frame time retained");
  ASSERT_EQ(125LL, (*event.Sequences[0])[1].TimeMillis, "nonuniform frame time retained");
  ASSERT_EQ(77, (*event.Sequences[0])[1].BmpId, "metadata uses authored image IDs");
  ASSERT_EQ(true, (*event.Sequences[0])[2].IsEnd, "omitted ID retains END identity");
  ASSERT_EQ(9000000000LL, (*event.Sequences[1])[1].TimeMillis, "frame times exceed signed 32-bit");
  ASSERT_EQ(true, (*event.Sequences[1])[1].IsEnd, "explicit Java END retains identity");
  ASSERT_EQ(true, timelines.at(1000000)->BgaEvents.front().Condition == bms_parser::BgaEventCondition::Play, "PLAY metadata retained without playback invention");
  ASSERT_EQ(2, timelines.at(1000000)->BgaEvents.front().Interval, "PLAY interval retained");
  ASSERT_EQ(size_t(1), poor->BgaEvents.size(), "poor replaces conditional metadata");
  ASSERT_EQ(1, poor->BgaEvents.front().Interval, "poor uses reference interval one");
  ASSERT_EQ(77, (*poor->BgaEvents.front().Sequences[0])[0].BmpId, "poor metadata replaces layer sequence");
  ASSERT_EQ(size_t(0), timelines.at(2000000)->BgaEvents.front().Sequences[0]->size(), "empty authored sequence retained in metadata");
  ASSERT_EQ(500LL, (*timelines.at(2500000)->BgaEvents.front().Sequences[0])[1].TimeMillis, "fallback END time is 500 ms");
  ASSERT_EQ(size_t(0), timelines.at(3500000)->BgaEvents.front().Sequences.size(), "empty id_set retained in metadata");
  const auto snapshot = parser_test::chartSnapshot(*chart);
  parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
  std::unique_ptr<bms_parser::Chart> repeated(raw);
  ASSERT_EQ(snapshot, parser_test::chartSnapshot(*repeated), "BGA repeated decode equivalence");
  const auto scan = parser.Scan(bytesFromString(input), cancelled);
  ASSERT_EQ(true, scan.has_value() && scan->HasBga, "extended BGA scans successfully");
  ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta), parser_test::metadataSnapshot(scan->Meta), "extended BGA full and Scan agree");
  {
    const std::string duplicates = R"({"version":"1.0.0","info":{"init_bpm":120},"bga":{
      "bga_header":[{"id":42,"name":"image.png"}],
      "bga_events":[{"y":0,"id":42},{"y":240,"id":999}],
      "layer_events":[{"y":0,"id":42},{"y":240,"id":42},
        {"y":240,"id":42,"condition":"miss"},{"y":480,"id":42},
        {"y":480,"id":42,"condition":"play"},{"y":720,"id":999},
        {"y":960,"id":42},{"y":960,"id":999}]
    }})";
    parser.Parse(bytesFromString(duplicates), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> decoded(raw);
    ASSERT_EQ(true, bool(decoded), "same-pulse layer assignments parsed");
    int base = -1, layer = -1;
    for (auto *measure : decoded->Measures) for (auto *timeline : measure->TimeLines) {
      if (timeline->BgaBase >= 0) base = timeline->BgaBase;
      if (timeline->BgaLayer >= 0) layer = timeline->BgaLayer;
      if (timeline->Timing == 500000 || timeline->Timing == 1000000) {
        ASSERT_EQ(-1, timeline->BgaLayer, "later conditional layer removes same-pulse unconditional assignment");
      }
      ASSERT_EQ(42, base, "undefined base preserves previously playing image");
      ASSERT_EQ(42, layer, "undefined or conditional layer preserves earlier timeline image");
    }
  }
  {
    // Undefined sequence images must never be treated as header array indexes.
    // Last definitions/events win, and every effective alternative is loaded.
    const std::string edge = R"({"version":"1.0.0","info":{"init_bpm":120},"bga":{
      "bga_header":[{"id":42,"name":"first.png"},{"id":77,"name":"second.png"}],
      "bga_sequence":[null,{"id":100,"sequence":[{"id":42}]},
        {"id":100,"sequence":[{"id":1,"time":-9223372036854775808},
          {"id":999,"time":9223372036854775807},{"id":-1,"time":9007199254740993},
          {"time":500}]},{"id":101,"sequence":[]}],
      "layer_events":[{"y":0,"id":42,"condition":"miss"},
        {"y":0,"id":100,"condition":"play","interval":2147483647},
        {"y":240,"id_set":[101,77],"condition":"miss","interval":-2147483648},
        {"y":480,"id_set":[77,42],"condition":"unknown"},
        {"y":720,"id":100}]
    }})";
    parser.Parse(bytesFromString(edge), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> decoded(raw);
    ASSERT_EQ(true, bool(decoded), "signed metadata limits accepted");
    std::map<long long, bms_parser::TimeLine *> byTime;
    for (auto *measure : decoded->Measures) for (auto *timeline : measure->TimeLines)
      byTime[timeline->Timing] = timeline;
    const auto *first = byTime.at(0);
    ASSERT_EQ(false, first->BgaPoor.has_value(), "later PLAY replaces same-pulse MISS projection");
    const auto &event = first->BgaEvents.front();
    ASSERT_EQ(2147483647, event.Interval, "maximum signed interval retained");
    ASSERT_EQ(size_t(4), event.Sequences[0]->size(), "last duplicate sequence definition wins");
    ASSERT_EQ((-9223372036854775807LL - 1), (*event.Sequences[0])[0].TimeMillis, "minimum signed frame time retained");
    ASSERT_EQ(9223372036854775807LL, (*event.Sequences[0])[1].TimeMillis, "maximum signed frame time retained");
    ASSERT_EQ(9007199254740993LL, (*event.Sequences[0])[2].TimeMillis, "integer frame time beyond double precision retained exactly");
    for (size_t i = 0; i < 3; ++i) {
      ASSERT_EQ(bms_parser::BgaSequenceBlank, (*event.Sequences[0])[i].BmpId, "unresolved sequence image safely blank");
      ASSERT_EQ(false, (*event.Sequences[0])[i].IsEnd, "unresolved image distinct from END");
    }
    ASSERT_EQ((-2147483647 - 1), byTime.at(500000)->BgaEvents.front().Interval, "minimum signed interval retained");
    ASSERT_EQ(true, byTime.at(500000)->BgaPoor->Frames.empty(), "empty first alternative remains blank despite nonempty second");
    ASSERT_EQ(true, byTime.at(1000000)->BgaEvents.front().Condition == bms_parser::BgaEventCondition::Always, "unknown condition uses Java ALWAYS default");
    ASSERT_EQ(-1, byTime.at(1000000)->BgaLayer, "extended ALWAYS alternatives not reinterpreted as direct layer");
    ASSERT_EQ(-1, byTime.at(1500000)->BgaLayer, "sequence ID not used as direct layer resource");
    ASSERT_EQ(size_t(2), decoded->ReferencedBmpTable.size(), "resources reachable only through alternatives retained");
  }
  {
    const std::string legacy = R"({"info":{"initBPM":120},"bga":{
      "bgaHeader":[{"ID":42,"name":"legacy.png"}],
      "bgaNotes":[{"y":0,"ID":42}],"layerNotes":[{"y":0,"ID":42}],
      "poorNotes":[{"y":0,"ID":42}]}})";
    parser.Parse(bytesFromString(legacy), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> decoded(raw);
    ASSERT_EQ(true, bool(decoded), "legacy BGA schema retained");
    const auto *first = decoded->Measures.front()->TimeLines.front();
    ASSERT_EQ(42, first->BgaBase, "legacy direct base retained");
    ASSERT_EQ(42, first->BgaLayer, "legacy direct layer survives simultaneous poor event");
    ASSERT_EQ(42, first->BgaPoor->Frames.front(), "legacy poor fallback retains image");
    ASSERT_EQ(bms_parser::BgaSequenceBlank, first->BgaPoor->Frames.back(), "legacy poor fallback terminates");
  }
  {
    // Repeated references share timed metadata; the unavoidable legacy MISS
    // projection has a total-frame budget, checked before allocating copies.
    std::string sequence = "{\"id\":100,\"sequence\":[";
    for (int i = 0; i < 1024; ++i) {
      if (i) sequence += ',';
      sequence += "{\"time\":" + std::to_string(i) + ",\"id\":42}";
    }
    sequence += "]}";
    const auto source = [&](int references, const char *condition) {
      std::string text = "{\"version\":\"1.0.0\",\"info\":{\"init_bpm\":120},\"bga\":{";
      text += "\"bga_header\":[{\"id\":42,\"name\":\"image.png\"}],\"bga_sequence\":[" + sequence + "],\"layer_events\":[";
      for (int i = 0; i < references; ++i) {
        if (i) text += ',';
        text += "{\"y\":" + std::to_string(i) + ",\"id\":100,\"condition\":\"" + condition + "\"}";
      }
      return text + "]}}";
    };
    parser.Parse(bytesFromString(source(1024, "miss")), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> decoded(raw);
    ASSERT_EQ(true, bool(decoded), "MISS projection at expansion budget accepted");
    const auto *shared = decoded->Measures.front()->TimeLines.front()->BgaEvents.front().Sequences.front().get();
    size_t totalFrames = 0;
    bool sharesAll = true;
    for (auto *measure : decoded->Measures) for (auto *timeline : measure->TimeLines) {
      if (timeline->BgaEvents.empty()) continue;
      sharesAll = sharesAll && timeline->BgaEvents.front().Sequences.front().get() == shared;
      totalFrames += timeline->BgaPoor->Frames.size();
    }
    ASSERT_EQ(true, sharesAll, "reused sequences share immutable storage");
    ASSERT_EQ(size_t(1048576), totalFrames, "legacy projection copies bounded total frame count");
    const auto overBudget = bytesFromString(source(1025, "miss"));
    parser.Parse(overBudget, &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> rejected(raw);
    ASSERT_EQ(false, bool(rejected), "MISS expansion above budget rejected before publication");
    ASSERT_EQ(false, parser.Scan(overBudget, cancelled).has_value(), "Scan enforces identical MISS expansion budget");
    parser.Parse(bytesFromString(source(1025, "play")), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> play(raw);
    ASSERT_EQ(true, bool(play), "shared PLAY sequences need no legacy expansion limit");
  }
  {
    const std::vector<std::string> invalidBga = {
      R"("bga_sequence":[{"id":1}])",
      R"("bga_sequence":[{"id":1,"sequence":null}])",
      R"("bga_sequence":[{"id":1,"sequence":[null]}])",
      R"("bga_sequence":[{"id":1,"sequence":[{"time":9223372036854775808}]}])",
      R"("bga_sequence":[{"id":1,"sequence":[{"time":-9223372036854775809}]}])",
      R"("bga_sequence":[{"id":1,"sequence":[{"time":0.5}]}])",
      R"("bga_sequence":[{"id":1,"sequence":[{"id":2147483648}]}])",
      R"("bga_sequence":[{"id":1,"sequence":[{"id":-2147483649}]}])",
      R"("layer_events":[{"y":0,"interval":2147483648}])",
      R"("layer_events":[{"y":0,"interval":-2147483649}])",
      R"("layer_events":[{"y":0,"interval":0.5}])",
      R"("layer_events":[{"y":0,"id_set":false}])",
      R"("layer_events":[{"y":0,"id_set":[2147483648]}])",
      R"("bga_header":[{"id":42,"name":"../unsafe.png"}])"
    };
    for (const auto &bga : invalidBga) {
      const auto bytes = bytesFromString("{\"version\":\"1.0.0\",\"info\":{\"init_bpm\":120},\"bga\":{" + bga + "}}");
      parser.Parse(bytes, &raw, false, false, cancelled);
      std::unique_ptr<bms_parser::Chart> decoded(raw);
      ASSERT_EQ(false, bool(decoded), "invalid BGA metadata rejected without partial chart");
      ASSERT_EQ(false, parser.Scan(bytes, cancelled).has_value(), "invalid BGA Scan agrees");
    }
    cancelled = true;
    parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> decoded(raw);
    ASSERT_EQ(false, bool(decoded), "cancelled extended BGA publishes no chart");
    ASSERT_EQ(false, parser.Scan(bytesFromString(input), cancelled).has_value(), "cancelled extended BGA publishes no scan");
  }
  return 0;
}

// Both schemas must reach the same playable timeline through the public API.
int runBmsonOverlapTests() {
  const std::string tap = R"({"name":"tap.wav","notes":[{"x":1,"y":240}]})";
  const std::string hold = R"({"name":"hold.wav","notes":[{"x":1,"y":0,"l":480,"t":2}]})";
  for (bool holdFirst : {false, true}) {
    const std::string input = R"({"version":"1.0.0","info":{"init_bpm":120},"sound_channels":[)" +
        (holdFirst ? hold + "," + tap : tap + "," + hold) + "]}";
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    bms_parser::Chart *raw = nullptr;
    parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> chart(raw);
    ASSERT_EQ(true, bool(chart), "overlapping channels parsed");
    ASSERT_EQ(holdFirst ? 2 : 1, chart->Meta.TotalNotes, "channel precedence preserves tap or CN count");
    ASSERT_EQ(holdFirst ? 2 : 0, chart->Meta.TotalLongNotes, "only admitted CN endpoints count");
    ASSERT_EQ(holdFirst ? 1000000LL : 500000LL, chart->Meta.PlayLength, "rejected hold tail does not extend play length");
    int heads = 0, bgm = 0;
    for (auto *measure : chart->Measures) for (auto *timeline : measure->TimeLines) {
      if (auto *note = timeline->Notes[0]) {
        if (note->IsLongNote() && static_cast<bms_parser::LongNote *>(note)->IsTail()) continue;
        ++heads;
        ASSERT_EQ(holdFirst, note->IsLongNote(), "earlier channel wins overlap");
        ASSERT_EQ(std::string(holdFirst ? "hold.wav" : "tap.wav"),
            chart->WavTable.at(chart->WavSlices.at(note->Wav).front().SourceWav), "winner sound retained");
      }
      for (auto *note : timeline->BackgroundNotes) {
        ++bgm;
        ASSERT_EQ(holdFirst ? 500000LL : 0LL, timeline->Timing, "rejected note sounds at authored head");
        ASSERT_EQ(std::string(holdFirst ? "tap.wav" : "hold.wav"),
            chart->WavTable.at(chart->WavSlices.at(note->Wav).front().SourceWav), "loser moves to BGM");
      }
    }
    ASSERT_EQ(1, heads, "one playable head survives overlap");
    ASSERT_EQ(1, bgm, "one rejected head remains audible");
    const auto scan = parser.Scan(bytesFromString(input), cancelled);
    ASSERT_EQ(true, bool(scan), "overlap scan succeeds");
    ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta), parser_test::metadataSnapshot(scan->Meta), "overlap full/scan parity");
  }
  return 0;
}

int runBmsonReleaseTests() {
  const std::string hold = R"({"name":"hold.wav","notes":[{"x":1,"y":0,"l":480,"t":2}]})";
  const std::string first = R"({"name":"first.wav","notes":[{"x":1,"y":480,"up":true}]})";
  const std::string last = R"({"name":"last.wav","notes":[{"x":1,"y":480,"up":true}]})";
  // Java overwrites an existing tail. Before a head, its HashMap chooses one
  // pending release; use first-authored deterministically instead of layering.
  for (int order : {0, 1, 2, 3}) {
    const std::string channels = order == 0 ? hold + "," + first + "," + last :
        order == 1 ? first + "," + hold + "," + last :
        order == 2 ? first + "," + last + "," + hold : first + "," + hold;
    const std::string input = R"({"version":"1.0.0","info":{"init_bpm":120},"sound_channels":[)" + channels + "]}";
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    bms_parser::Chart *raw = nullptr;
    parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> chart(raw);
    ASSERT_EQ(true, bool(chart), "release channel orders parsed");
    auto *head = dynamic_cast<bms_parser::LongNote *>(chart->Measures.front()->TimeLines.front()->Notes[0]);
    ASSERT_EQ(true, head && head->Tail, "release has an admitted hold");
    const auto &slices = chart->WavSlices.at(head->Tail->Wav);
    ASSERT_EQ(size_t(1), slices.size(), "release assignment never layers competing sounds");
    ASSERT_EQ(std::string(order < 2 ? "last.wav" : "first.wav"), chart->WavTable.at(slices.front().SourceWav), "pending versus existing-tail precedence");
    ASSERT_EQ(2, chart->Meta.TotalNotes, "release sounds add no CN judgements");
    const auto scan = parser.Scan(bytesFromString(input), cancelled);
    ASSERT_EQ(true, bool(scan), "release scan succeeds");
    ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta), parser_test::metadataSnapshot(scan->Meta), "release full/scan parity");
  }
  return 0;
}

int runBmsonRecoveryTests() {
  // Each case independently catches whole-chart rejection of recoverable Java
  // fields. Keep explicit modern zero TOTAL and normal timing semantics intact.
  for (const auto &fields : {
      std::make_pair(std::string(), std::string(R"("bpm_events":[{"y":240,"bpm":0},{"y":480,"bpm":-120}],)")),
      std::make_pair(std::string(), std::string(R"("stop_events":[{"y":240,"duration":-240}],)")),
      std::make_pair(std::string(R"(,"ln_type":-1)"), std::string()),
      std::make_pair(std::string(R"(,"ln_type":2)"), std::string())}) {
    const bool charge = fields.first == R"(,"ln_type":2)";
    const std::string input = R"({"version":"1.0.0","info":{"init_bpm":120,"total":0)" + fields.first + "}," + fields.second +
        R"("sound_channels":[{"name":"hold.wav","notes":[{"x":1,"y":0,"l":480,"t":)" + std::string(charge ? "-1" : "0") + "}]}]}";
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    bms_parser::Chart *raw = nullptr;
    parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
    std::unique_ptr<bms_parser::Chart> chart(raw);
    ASSERT_EQ(true, bool(chart), "invalid timing/LN values recover");
    ASSERT_EQ(charge ? 2 : 0, chart->Meta.LnMode, "invalid global LN type defaults");
    ASSERT_EQ(charge ? 2 : 1, chart->Meta.TotalNotes, "invalid note type inherits global LN type");
    ASSERT_EQ(1000000LL, chart->Meta.PlayLength, "skipped events do not alter timing");
    ASSERT_EQ(120.0, chart->Meta.MinBpm, "skipped BPM does not alter extrema");
    ASSERT_EQ(0.0, chart->Meta.Total, "modern zero TOTAL stays authored");
    ASSERT_EQ(true, chart->Meta.TotalIsRelative, "modern TOTAL stays relative");
    const auto scan = parser.Scan(bytesFromString(input), cancelled);
    ASSERT_EQ(true, bool(scan), "recovered scan succeeds");
    ASSERT_EQ(false, scan->HasBpmStop, "negative stop has no effective stop");
    ASSERT_EQ(parser_test::metadataSnapshot(chart->Meta), parser_test::metadataSnapshot(scan->Meta), "recovery full/scan parity");
  }
  for (const std::string event : {
      R"("bpm_events":[{"y":-1,"bpm":0}])", R"("stop_events":[{"y":0.5,"duration":-1}])",
      R"("bpm_events":[{"y":4294967296,"bpm":-1}])", R"("stop_events":[{"y":"bad","duration":-1}])"}) {
    const std::string input = R"({"version":"1.0.0","info":{"init_bpm":120},)" + event + "}";
    bms_parser::Parser parser;
    std::atomic_bool cancelled{false};
    ASSERT_EQ(false, bool(parser.Scan(bytesFromString(input), cancelled)), "skipped event still validates pulse");
  }
  return 0;
}

int runBmsonLayerIdentityTests() {
  const std::string input = R"({"version":"1.0.0","info":{"init_bpm":120},"sound_channels":[
    {"name":"chord.wav","notes":[{"x":1,"y":0},{"x":2,"y":0}]},
    {"name":"layer.wav","notes":[{"x":1,"y":0},{"x":1,"y":0},{"x":3,"y":0}]},
    {"name":"third.wav","notes":[{"x":1,"y":0}]}]})";
  bms_parser::Parser parser;
  std::atomic_bool cancelled{false};
  bms_parser::Chart *raw = nullptr;
  parser.Parse(bytesFromString(input), &raw, false, false, cancelled);
  std::unique_ptr<bms_parser::Chart> chart(raw);
  ASSERT_EQ(true, bool(chart), "layered chord parsed");
  const auto *timeline = chart->Measures.front()->TimeLines.front();
  const auto &layers = chart->WavSlices.at(timeline->Notes[0]->Wav);
  const auto &chord = chart->WavSlices.at(timeline->Notes[1]->Wav);
  const auto &other = chart->WavSlices.at(timeline->Notes[2]->Wav);
  ASSERT_EQ(size_t(3), layers.size(), "same-pulse duplicate slice is not doubled");
  ASSERT_EQ(size_t(1), chord.size(), "layer accumulation does not mutate another chord key");
  ASSERT_EQ(size_t(1), other.size(), "second shared slice also remains isolated");
  ASSERT_EQ(chord.front().SliceId, layers[0].SliceId, "first shared slice keeps monophonic identity");
  ASSERT_EQ(other.front().SliceId, layers[1].SliceId, "second shared slice keeps monophonic identity");
  return 0;
}

int runBmsonTests() {
  if (const int result = runBmsonOverlapTests(); result != 0) return result;
  if (const int result = runBmsonReleaseTests(); result != 0) return result;
  if (const int result = runBmsonRecoveryTests(); result != 0) return result;
  if (const int result = runBmsonLayerIdentityTests(); result != 0) return result;
  if (const int result = runBmsonBgaTests(); result != 0) return result;
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
