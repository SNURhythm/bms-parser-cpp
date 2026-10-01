#pragma once

#include <cstddef>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <map>

// Semantic snapshots deliberately enumerate fields instead of copying object
// representations (padding, addresses and container layout are not behavior).
namespace parser_test {
struct Snapshot {
  std::string bytes;
  template <typename T,
            std::enable_if_t<std::is_arithmetic_v<T> || std::is_enum_v<T>, int> = 0>
  void add(const T &value) {
    bytes.append(reinterpret_cast<const char *>(&value), sizeof(value));
  }
  void add(const std::string &value) {
    add(value.size());
    bytes.append(value);
  }
  void add(const std::filesystem::path &value) { add(value.generic_string()); }
  template <typename T> void add(const std::optional<T> &value) {
    add(value.has_value());
    if (value) add(*value);
  }
};

inline std::string metadataSnapshot(const bms_parser::ChartMeta &m) {
  Snapshot out;
#define META(field) out.add(m.field)
  META(SHA256); META(MD5); META(BmsPath); META(Folder);
  META(Artist); META(SubArtist); META(Bpm); META(Genre); META(Title); META(SubTitle);
  META(Rank); META(RankType); META(Total); META(HasTotal);
  META(PlayLength); META(TotalLength); META(Banner); META(StageFile);
  META(BackBmp); META(Preview); META(BgaPoorDefault); META(Difficulty);
  META(PlayLevel); META(PlayLevelText); META(MinBpm); META(MaxBpm);
  META(MostPrevalentBpm); META(GuessedBeatBpm); META(GuessedBeatsPerMeasure);
  META(Player); META(KeyMode); META(IsDP); META(TotalNotes); META(TotalLongNotes);
  META(TotalScratchNotes); META(TotalBackSpinNotes); META(TotalLandmineNotes);
  META(LnMode); META(RandomSeed); META(RandomPrng);
#undef META
  out.add(m.RandomValues.size());
  for (const auto value : m.RandomValues) out.add(value);
  return std::move(out.bytes);
}

inline std::string chartSnapshot(const bms_parser::Chart &chart) {
  Snapshot out;
  out.add(metadataSnapshot(chart.Meta));
  for (const auto *table : {&chart.WavTable, &chart.ReferencedWavTable,
                            &chart.BmpTable, &chart.ReferencedBmpTable}) {
    const std::map<int, std::string> ordered(table->begin(), table->end());
    out.add(ordered.size());
    for (const auto &entry : ordered) { out.add(entry.first); out.add(entry.second); }
  }
  std::unordered_map<const bms_parser::TimeLine *, size_t> timelineIds;
  std::unordered_map<const bms_parser::Note *, size_t> noteIds;
  for (const auto *measure : chart.Measures) {
    for (const auto *timeline : measure->TimeLines) {
      timelineIds.emplace(timeline, timelineIds.size() + 1);
      for (const auto *notes : {&timeline->Notes, &timeline->InvisibleNotes,
                                &timeline->BackgroundNotes}) {
        for (const auto *note : *notes)
          if (note) noteIds.emplace(note, noteIds.size() + 1);
      }
      for (const auto *note : timeline->LandmineNotes)
        if (note) noteIds.emplace(note, noteIds.size() + 1);
    }
  }
  const auto noteId = [&](const bms_parser::Note *note) {
    if (!note) return size_t{0};
    const auto found = noteIds.find(note);
    return found == noteIds.end() ? size_t(-1) : found->second;
  };
  const auto addNote = [&](bms_parser::Note *note) {
    out.add(noteId(note));
    if (!note) return;
    out.add(note->Lane); out.add(note->Wav);
    out.add(note->IsPlayed); out.add(note->IsDead); out.add(note->PlayedTime);
    const auto timeline = timelineIds.find(note->Timeline);
    out.add(note->Timeline == nullptr ? size_t{0} :
            timeline == timelineIds.end() ? size_t(-1) : timeline->second);
    out.add(note->IsLongNote()); out.add(note->IsLandmineNote());
    if (note->IsLongNote()) {
      const auto *ln = static_cast<const bms_parser::LongNote *>(note);
      out.add(noteId(ln->Head)); out.add(noteId(ln->Tail));
      out.add(ln->IsHolding); out.add(ln->Type); out.add(ln->ReleaseTime);
    }
    if (note->IsLandmineNote())
      out.add(static_cast<const bms_parser::LandmineNote *>(note)->Damage);
  };
  out.add(chart.Measures.size());
  for (const auto *measure : chart.Measures) {
    out.add(measure->Scale); out.add(measure->Timing); out.add(measure->Pos);
    out.add(measure->TimeLines.size());
    for (const auto *t : measure->TimeLines) {
#define TIMELINE(field) out.add(t->field)
      TIMELINE(Bpm); TIMELINE(BpmChange); TIMELINE(BpmChangeApplied);
      TIMELINE(ScrollChange); TIMELINE(HasSpeedObject); TIMELINE(BgaBase);
      TIMELINE(BgaLayer); TIMELINE(StopLength); TIMELINE(Scroll); TIMELINE(Speed);
      TIMELINE(Timing); TIMELINE(BeatPosition); TIMELINE(IsFirstInMeasure);
#undef TIMELINE
      out.add(t->BgaPoor.has_value());
      if (t->BgaPoor) {
        out.add(t->BgaPoor->Frames.size());
        for (const auto frame : t->BgaPoor->Frames) out.add(frame);
      }
      for (const auto *notes : {&t->Notes, &t->InvisibleNotes, &t->BackgroundNotes}) {
        out.add(notes->size());
        for (auto *note : *notes) addNote(note);
      }
      out.add(t->LandmineNotes.size());
      for (auto *note : t->LandmineNotes) addNote(note);
    }
  }
  return std::move(out.bytes);
}
} // namespace parser_test
