#include "Parser.h"
#include "LongNote.h"
#include "Modifier.h"
#include "ChartSnapshot.h"
#include <iomanip>
#include <iostream>
#include <memory>

using namespace bms_parser;

int main(int argc, char **argv) {
  Parser parser;
  parser.SetRandomSeed(1);
  parser.SetRandomValues(std::vector<int>(10000, 1));
  std::atomic_bool cancelled{false};
  std::cout << std::setprecision(17);
  for (int i = 1; i < argc; ++i) {
    const std::filesystem::path path(argv[i]);
    Chart *raw = nullptr;
    parser.Parse(path, &raw, false, false, cancelled);
    std::unique_ptr<Chart> chart(raw);
    const auto scan = parser.Scan(path, cancelled);
    std::cout << "FILE " << path.filename().string() << '\n';
    if (bool(chart) != scan.has_value()) return 2;
    if (!chart) {
      std::cout << "NULL\n";
      continue;
    }
    if (parser_test::metadataSnapshot(chart->Meta) !=
        parser_test::metadataSnapshot(scan->Meta)) {
      std::cerr << path << ": Scan metadata differs from Parse\n";
      return 3;
    }
    const auto before = parser_test::metadataSnapshot(chart->Meta);
    BaseModifier::RecalculateNoteCounts(*chart);
    if (before != parser_test::metadataSnapshot(chart->Meta)) {
      std::cerr << path << ": effective note counts disagree\n";
      return 4;
    }
    const auto &meta = chart->Meta;
    std::cout << "META notes=" << meta.TotalNotes << " mode=" << meta.KeyMode
              << " difficulty=" << meta.Difficulty << " bpm=" << meta.Bpm
              << " min=" << meta.MinBpm << " max=" << meta.MaxBpm
              << " player=" << meta.Player << " lnmode=" << meta.LnMode
              << " total=" << meta.Total << " rank=" << meta.Rank
              << " ranktype=" << static_cast<int>(meta.RankType) << '\n';
    std::cout << "TEXT title=" << std::quoted(meta.Title)
              << " subtitle=" << std::quoted(meta.SubTitle)
              << " genre=" << std::quoted(meta.Genre)
              << " artist=" << std::quoted(meta.Artist)
              << " subartist=" << std::quoted(meta.SubArtist)
              << " playlevel=" << std::quoted(meta.PlayLevelText)
              << " stage=" << std::quoted(meta.StageFile.generic_string())
              << " banner=" << std::quoted(meta.Banner.generic_string())
              << " back=" << std::quoted(meta.BackBmp.generic_string())
              << " preview=" << std::quoted(meta.Preview.generic_string()) << '\n';
    const auto wav = [&](int id) {
      const auto it = chart->WavTable.find(id);
      return it == chart->WavTable.end() ? std::string("-") : it->second;
    };
    const auto bmp = [&](int id) {
      const auto it = chart->BmpTable.find(id);
      return it == chart->BmpTable.end() ? std::string("-") : it->second;
    };
    for (const auto *measure : chart->Measures) {
      for (const auto *timeline : measure->TimeLines) {
        std::cout << "TL pos=" << timeline->BeatPosition << " time=" << timeline->Timing
                  << " bpm=" << timeline->Bpm << " scroll=" << timeline->Scroll
                  << " stop=" << timeline->GetStopDuration() << '\n';
        if (timeline->BgaBase != -1 || timeline->BgaLayer != -1)
          std::cout << "BGA base=" << std::quoted(bmp(timeline->BgaBase))
                    << " layer=" << std::quoted(bmp(timeline->BgaLayer)) << '\n';
        if (timeline->BgaPoor) {
          const auto &frames = timeline->BgaPoor->Frames;
          for (size_t frame = 0; frame < frames.size(); ++frame)
            std::cout << "POOR frame=" << frame << " wav=" << std::quoted(bmp(frames[frame])) << '\n';
        }
        for (size_t lane = 0; lane < timeline->Notes.size(); ++lane) {
          const auto *note = timeline->Notes[lane];
          if (!note) continue;
          const auto *ln = dynamic_cast<const LongNote *>(note);
          const auto *mine = dynamic_cast<const LandmineNote *>(note);
          std::cout << "NOTE lane=" << lane << " kind="
                    << (ln ? "LongNote" : mine ? "MineNote" : "NormalNote")
                    << " wav=" << std::quoted(wav(note->Wav));
          if (mine) std::cout << " damage=" << mine->Damage;
          if (ln) {
            const auto *pair = ln->Head ? ln->Head : ln->Tail;
            if (pair && (ln->Head ? pair->Tail : pair->Head) != ln) {
              std::cerr << path << ": invalid LN pair\n";
              return 5;
            }
            std::cout << " pair=";
            if (pair) std::cout << pair->Timeline->BeatPosition;
            else std::cout << "null";
            std::cout << " type=" << static_cast<int>(ln->Type)
                      << " end=" << ln->IsTail();
          }
          std::cout << '\n';
        }
        for (size_t lane = 0; lane < timeline->InvisibleNotes.size(); ++lane) {
          if (const auto *note = timeline->InvisibleNotes[lane])
            std::cout << "HIDDEN lane=" << lane << " wav=" << std::quoted(wav(note->Wav)) << '\n';
        }
        for (const auto *note : timeline->BackgroundNotes)
          std::cout << "BG wav=" << std::quoted(wav(note->Wav)) << '\n';
      }
    }
  }
}
