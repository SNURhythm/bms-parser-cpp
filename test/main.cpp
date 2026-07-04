#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if WITH_AMALGAMATION
#include "bms_parser.hpp"
#else
#include "../src/Chart.h"
#include "../src/LongNote.h"
#include "../src/Modifier.h"
#include "../src/Parser.h"

#endif

#define ASSERT_EQ(a, b, desc)                                                  \
  if (a != b) {                                                                \
    std::cerr << desc << std::endl;                                            \
    std::cerr << "\tExpected: " << a << std::endl;                             \
    std::cerr << "\tActual: " << b << std::endl;                               \
    return 1;                                                                  \
  } else {                                                                     \
    std::cout << "\t" << desc << " passed" << std::endl;                       \
  }
#define ASSERT_EQW(a, b, desc)                                                 \
  if (a != b) {                                                                \
    std::cerr << desc;                                                         \
    std::wcerr << "\tExpected: " << a << std::endl;                            \
    std::wcerr << "\tActual: " << b << std::endl;                              \
    return 1;                                                                  \
  } else {                                                                     \
    std::cout << "\t" << desc << " passed" << std::endl;                       \
  }
std::string ws2s(const std::wstring &wstr) {
  return std::string().assign(wstr.begin(), wstr.end());
}

std::string noteLanesByTimeline(const bms_parser::Chart *chart) {
  std::ostringstream output;
  bool isFirstTimeline = true;
  for (const auto *measure : chart->Measures) {
    for (const auto *timeline : measure->TimeLines) {
      std::ostringstream lanes;
      bool isFirstLane = true;
      for (size_t i = 0; i < timeline->Notes.size(); ++i) {
        if (timeline->Notes[i] == nullptr) {
          continue;
        }
        if (!isFirstLane) {
          lanes << "+";
        }
        lanes << i;
        isFirstLane = false;
      }
      const auto laneText = lanes.str();
      if (laneText.empty()) {
        continue;
      }
      if (!isFirstTimeline) {
        output << ";";
      }
      output << laneText;
      isFirstTimeline = false;
    }
  }
  return output.str();
}

std::string joinLaneIndices(const std::vector<int> &lanes) {
  std::ostringstream output;
  for (size_t i = 0; i < lanes.size(); ++i) {
    if (i > 0) {
      output << ",";
    }
    output << lanes[i];
  }
  return output.str();
}

std::vector<bms_parser::LongNote *> longNoteHeads(
    const bms_parser::Chart *chart) {
  std::vector<bms_parser::LongNote *> heads;
  for (const auto *measure : chart->Measures) {
    for (const auto *timeline : measure->TimeLines) {
      for (auto *note : timeline->Notes) {
        auto *longNote = dynamic_cast<bms_parser::LongNote *>(note);
        if (longNote != nullptr && !longNote->IsTail()) {
          heads.push_back(longNote);
        }
      }
    }
  }
  return heads;
}

std::unique_ptr<bms_parser::Chart> makeModifierTestChart(bool includeScratch) {
  auto chart = std::make_unique<bms_parser::Chart>();
  chart->Meta.KeyMode = 7;
  chart->Meta.IsDP = false;

  auto *measure = new bms_parser::Measure();
  const int laneEnd = includeScratch ? 8 : 7;
  for (int lane = 0; lane < laneEnd; ++lane) {
    auto *timeline = new bms_parser::TimeLine(16, false);
    timeline->Timing = lane * 1000000LL;
    timeline->BeatPosition = static_cast<double>(lane) / laneEnd;
    timeline->SetNote(lane, new bms_parser::Note(lane + 1));
    measure->TimeLines.push_back(timeline);
  }
  chart->Measures.push_back(measure);
  bms_parser::BaseModifier::RecalculateNoteCounts(*chart);
  return chart;
}

std::unique_ptr<bms_parser::Chart> makeDpModifierTestChart() {
  auto chart = std::make_unique<bms_parser::Chart>();
  chart->Meta.KeyMode = 14;
  chart->Meta.IsDP = true;

  auto *measure = new bms_parser::Measure();
  for (int lane = 0; lane < 16; ++lane) {
    auto *timeline = new bms_parser::TimeLine(16, false);
    timeline->Timing = lane * 1000000LL;
    timeline->BeatPosition = static_cast<double>(lane) / 16.0;
    timeline->SetNote(lane, new bms_parser::Note(lane + 1));
    measure->TimeLines.push_back(timeline);
  }
  chart->Measures.push_back(measure);
  bms_parser::BaseModifier::RecalculateNoteCounts(*chart);
  return chart;
}

std::vector<unsigned char> bytesFromString(const std::string &content) {
  return std::vector<unsigned char>(content.begin(), content.end());
}

bool hasReferencedWav(const bms_parser::Chart *chart, int wav) {
  return chart->ReferencedWavIds.find(wav) != chart->ReferencedWavIds.end();
}

bool hasReferencedBmp(const bms_parser::Chart *chart, int bmp) {
  return chart->ReferencedBmpIds.find(bmp) != chart->ReferencedBmpIds.end();
}

int runEncodingTests() {
  {
    const std::string koreanTitle =
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4";
    const std::string content = std::string("#TITLE ") + koreanTitle + "\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(koreanTitle, chart->Meta.Title,
              "parser_encoding_utf8_korean_title: ");
    delete chart;
  }
  {
    const std::string koreanTitle =
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4";
    const std::string content = std::string("#CHARSET UTF-8\n#TITLE ") +
                                koreanTitle + "\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(koreanTitle, chart->Meta.Title,
              "parser_encoding_declared_utf8_korean_title: ");
    delete chart;
  }
  {
    const std::string koreanTitle =
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4";
    const std::string content =
        std::string("\xef\xbb\xbf#TITLE ") + koreanTitle + "\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(koreanTitle, chart->Meta.Title,
              "parser_encoding_utf8_bom_korean_title: ");
    delete chart;
  }
  {
    const std::string koreanTitle =
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4";
    const std::vector<unsigned char> content = {
        0xff, 0xfe, 0x23, 0x00, 0x54, 0x00, 0x49, 0x00, 0x54,
        0x00, 0x4c, 0x00, 0x45, 0x00, 0x20, 0x00, 0x5c, 0xd5,
        0x6d, 0xad, 0xb4, 0xc5, 0x0a, 0x00,
    };
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(content, &chart, false, false, cancel);

    ASSERT_EQ(koreanTitle, chart->Meta.Title,
              "parser_encoding_utf16le_bom_korean_title: ");
    delete chart;
  }
  {
    const std::string koreanTitle =
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4";
    const std::vector<unsigned char> content = {
        0xfe, 0xff, 0x00, 0x23, 0x00, 0x54, 0x00, 0x49, 0x00,
        0x54, 0x00, 0x4c, 0x00, 0x45, 0x00, 0x20, 0xd5, 0x5c,
        0xad, 0x6d, 0xc5, 0xb4, 0x00, 0x0a,
    };
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(content, &chart, false, false, cancel);

    ASSERT_EQ(koreanTitle, chart->Meta.Title,
              "parser_encoding_utf16be_bom_korean_title: ");
    delete chart;
  }
  {
    const std::string koreanTitle =
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4";
    const std::string eucKrTitle = "\xc7\xd1\xb1\xb9\xbe\xee";
    const std::string content = std::string("#TITLE ") + eucKrTitle + "\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(koreanTitle, chart->Meta.Title,
              "parser_encoding_euckr_heuristic_korean_title: ");
    delete chart;
  }
  {
    const std::string koreanTitle =
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4";
    const std::string eucKrTitle = "\xc7\xd1\xb1\xb9\xbe\xee";
    const std::string content =
        std::string("#CHARSET EUC-KR\n#TITLE ") + eucKrTitle + "\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(koreanTitle, chart->Meta.Title,
              "parser_encoding_declared_euckr_korean_title: ");
    delete chart;
  }
  {
    const std::string shiftJisTitle = "\x83\x65\x83\x58\x83\x67";
    const std::string utf8Title =
        "\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88";
    const std::string content = std::string("#TITLE ") + shiftJisTitle + "\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(utf8Title, chart->Meta.Title,
              "parser_encoding_shiftjis_heuristic_title: ");
    delete chart;
  }
  {
    const std::string shiftJisTitle = "\x83\x65\x83\x58\x83\x67";
    const std::string utf8Title =
        "\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88";
    const std::string content = std::string("#CHARSET SHIFT_JIS\n#TITLE ") +
                                shiftJisTitle + "\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(utf8Title, chart->Meta.Title,
              "parser_encoding_declared_shiftjis_title: ");
    delete chart;
  }
  return 0;
}

int runReferencedWavTests() {
  const std::string content =
      "#WAV01 bgm.wav\n"
      "#WAV02 key.wav\n"
      "#WAV03 invisible.wav\n"
      "#WAV04 unused.wav\n"
      "#BMP00 default-poor.png\n"
      "#BMP01 base.png\n"
      "#BMP02 layer.png\n"
      "#BMP03 unused.png\n"
      "#00101:01\n"
      "#00111:02\n"
      "#00131:03\n"
      "#00104:01\n"
      "#00107:02\n";
  bms_parser::Chart *chart = nullptr;
  std::atomic_bool cancel = false;
  bms_parser::Parser parser;
  parser.Parse(bytesFromString(content), &chart, false, false, cancel);

  ASSERT_EQ(true, hasReferencedWav(chart, 1), "referenced_wav_background: ");
  ASSERT_EQ(true, hasReferencedWav(chart, 2), "referenced_wav_playable: ");
  ASSERT_EQ(true, hasReferencedWav(chart, 3), "referenced_wav_invisible: ");
  ASSERT_EQ(false, hasReferencedWav(chart, 4),
            "referenced_wav_unused: ");
  ASSERT_EQ(static_cast<size_t>(3), chart->ReferencedWavIds.size(),
            "referenced_wav_count: ");
  ASSERT_EQ(true, hasReferencedBmp(chart, 0), "referenced_bmp_default: ");
  ASSERT_EQ(true, hasReferencedBmp(chart, 1), "referenced_bmp_base: ");
  ASSERT_EQ(true, hasReferencedBmp(chart, 2), "referenced_bmp_layer: ");
  ASSERT_EQ(false, hasReferencedBmp(chart, 3), "referenced_bmp_unused: ");
  ASSERT_EQ(static_cast<size_t>(3), chart->ReferencedBmpIds.size(),
            "referenced_bmp_count: ");
  delete chart;
  return 0;
}

int runParserRandomTests() {
  {
    const std::string content =
        "#TITLE base\n"
        "#RANDOM 2\n"
        "#IF 1\n"
        "#RANDOM 3\n"
        "#ENDRANDOM\n"
        "#TITLE one\n"
        "#ENDIF\n"
        "#IF 2\n"
        "#TITLE two\n"
        "#RANDOM 3\n"
        "#IF 1\n"
        "#ARTIST nested-one\n"
        "#ENDIF\n"
        "#IF 3\n"
        "#ARTIST nested-three\n"
        "#ENDIF\n"
        "#ENDRANDOM\n"
        "#ENDIF\n"
        "#ENDRANDOM\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.SetRandomValues({2, 3});
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(std::string("two"), chart->Meta.Title,
              "parser_random_selected_title: ");
    ASSERT_EQ(std::string("nested-three"), chart->Meta.Artist,
              "parser_random_selected_nested_artist: ");
    ASSERT_EQ(std::string("2,3"), joinLaneIndices(chart->Meta.RandomValues),
              "parser_random_selected_values: ");
    delete chart;
  }
  {
    const std::string content =
        "#TITLE base\n"
        "#RANDOM 2\n"
        "#IF 1\n"
        "#RANDOM 2\n"
        "#IF 1\n"
        "#TITLE leaked\n"
        "#ENDIF\n"
        "#ENDRANDOM\n"
        "#ENDIF\n"
        "#IF 2\n"
        "#TITLE active\n"
        "#ENDIF\n"
        "#ENDRANDOM\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.SetRandomValues({2});
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(std::string("active"), chart->Meta.Title,
              "parser_random_inactive_nested_branch_not_parsed: ");
    ASSERT_EQ(std::string("2"), joinLaneIndices(chart->Meta.RandomValues),
              "parser_random_inactive_nested_values_not_consumed: ");
    delete chart;
  }
  {
    const std::string content =
        "#TITLE base\n"
        "#RANDOM 3\n"
        "#IF 1\n"
        "#TITLE one\n"
        "#ELSEIF 2\n"
        "#TITLE two\n"
        "#ELSE\n"
        "#TITLE fallback\n"
        "#ENDIF\n"
        "#ENDRANDOM\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.SetRandomValues({3});
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(std::string("fallback"), chart->Meta.Title,
              "parser_random_elseif_before_else: ");
    delete chart;
  }
  {
    const std::string content =
        "#TITLE base\n"
        "#RANDOM 2\n"
        "#IF 1\n"
        "#TITLE first\n"
        "#ENDIF\n"
        "#RANDOM 2\n"
        "#IF 2\n"
        "#ARTIST second\n"
        "#ENDIF\n"
        "#ENDRANDOM\n"
        "#IF 2\n"
        "#GENRE after\n"
        "#ENDIF\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.SetRandomValues({1, 2});
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    ASSERT_EQ(std::string("first"), chart->Meta.Title,
              "parser_random_implicit_sibling_first: ");
    ASSERT_EQ(std::string("second"), chart->Meta.Artist,
              "parser_random_implicit_sibling_second: ");
    ASSERT_EQ(std::string("after"), chart->Meta.Genre,
              "parser_random_implicit_sibling_closes_parent: ");
    ASSERT_EQ(std::string("1,2"), joinLaneIndices(chart->Meta.RandomValues),
              "parser_random_implicit_sibling_values: ");
    delete chart;
  }
  {
    const std::string content =
        "#TITLE base\n"
        "#RANDOM 2\n"
        "#IF 1\n"
        "#TITLE one\n"
        "#ENDIF\n"
        "#IF 2\n"
        "#TITLE two\n"
        "#ENDIF\n"
        "#ENDRANDOM\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.SetRandomSeed(12345);
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);

    const auto randomValues = joinLaneIndices(chart->Meta.RandomValues);
    const bool randomValueInRange = randomValues == "1" || randomValues == "2";
    ASSERT_EQ(true, randomValueInRange,
              "parser_random_generated_value_range: ");
    delete chart;
  }
  return 0;
}

int runModifierTests() {
  {
    auto chart = makeModifierTestChart(false);
    bms_parser::MirrorModifier modifier;
    modifier.Modify(*chart);
    ASSERT_EQ("6;5;4;3;2;1;0", noteLanesByTimeline(chart.get()),
              "mirror_modifier_lanes: ");
    ASSERT_EQ("7,6,5,4,3,2,1,0",
              joinLaneIndices(modifier.GetLaneOrder(chart->Meta)),
              "mirror_modifier_lane_order: ");
  }
  {
    auto chart = makeModifierTestChart(false);
    bms_parser::RandomModifier modifier(12345);
    modifier.Modify(*chart);
    ASSERT_EQ("5;2;4;6;1;0;3", noteLanesByTimeline(chart.get()),
              "random_modifier_lanes: ");
  }
  {
    auto chart = makeModifierTestChart(false);
    bms_parser::RRandomModifier modifier(12345);
    modifier.Modify(*chart);
    ASSERT_EQ("4;3;2;1;0;6;5", noteLanesByTimeline(chart.get()),
              "r_random_modifier_lanes: ");
  }
  {
    auto chart = makeModifierTestChart(true);
    bms_parser::RandomExModifier modifier(12345);
    modifier.Modify(*chart);
    ASSERT_EQ("6;5;0;1;7;2;3;4", noteLanesByTimeline(chart.get()),
              "random_ex_modifier_lanes: ");
  }
  {
    auto chart = makeModifierTestChart(true);
    bms_parser::LaneAssignModifier modifier("S1234567");
    modifier.Modify(*chart);
    ASSERT_EQ("0;1;2;3;4;5;6;7", noteLanesByTimeline(chart.get()),
              "lane_assign_modifier_identity_lanes: ");
  }
  {
    auto chart = makeModifierTestChart(true);
    bms_parser::LaneAssignModifier modifier("1234567S");
    modifier.Modify(*chart);
    ASSERT_EQ("7;0;1;2;3;4;5;6", noteLanesByTimeline(chart.get()),
              "lane_assign_modifier_rotated_scratch_lanes: ");
    ASSERT_EQ("0,1,2,3,4,5,6,7",
              joinLaneIndices(modifier.GetLaneOrder(chart->Meta)),
              "lane_assign_modifier_rotated_scratch_order: ");
  }
  {
    auto chart = makeDpModifierTestChart();
    bms_parser::LaneAssignModifier modifier("L123456789ABCDER");
    modifier.Modify(*chart);
    ASSERT_EQ("0;1;2;3;4;5;6;7;8;9;10;11;12;13;14;15",
              noteLanesByTimeline(chart.get()),
              "lane_assign_modifier_dp_identity_lanes: ");
  }
  {
    auto chart = makeDpModifierTestChart();
    bms_parser::LaneAssignModifier modifier("R123456789ABCDEL");
    modifier.Modify(*chart);
    ASSERT_EQ("0;1;2;3;4;5;6;15;8;9;10;11;12;13;14;7",
              noteLanesByTimeline(chart.get()),
              "lane_assign_modifier_dp_scratch_swap_lanes: ");
    ASSERT_EQ("15,0,1,2,3,4,5,6,8,9,10,11,12,13,14,7",
              joinLaneIndices(modifier.GetLaneOrder(chart->Meta)),
              "lane_assign_modifier_dp_scratch_swap_order: ");
  }
  {
    auto chart = makeModifierTestChart(true);
    std::string error;
    ASSERT_EQ(false,
              bms_parser::ValidateLaneAssignNotation(chart->Meta, "S1234566",
                                                     &error),
              "lane_assign_validation_duplicate: ");
    ASSERT_EQ(std::string("Duplicate lane symbol: 6"), error,
              "lane_assign_validation_duplicate_error: ");
  }
  {
    auto chart = makeModifierTestChart(true);
    std::string error;
    ASSERT_EQ(false,
              bms_parser::ValidateLaneAssignNotation(chart->Meta, "S123456",
                                                     &error),
              "lane_assign_validation_missing_lane: ");
    ASSERT_EQ(std::string("Expected 8 lane symbols."), error,
              "lane_assign_validation_missing_lane_error: ");
  }
  {
    auto chart = makeDpModifierTestChart();
    std::string error;
    ASSERT_EQ(false,
              bms_parser::ValidateLaneAssignNotation(
                  chart->Meta, "L123456789ABCDEE", &error),
              "lane_assign_validation_dp_duplicate: ");
    ASSERT_EQ(std::string("Duplicate lane symbol: E"), error,
              "lane_assign_validation_dp_duplicate_error: ");
  }
  {
    auto chart = makeModifierTestChart(false);
    bms_parser::SRandomModifier modifier(12345);
    modifier.Modify(*chart);
    ASSERT_EQ("5;4;0;1;4;6;5", noteLanesByTimeline(chart.get()),
              "s_random_modifier_lanes: ");
  }
  {
    auto chart = makeModifierTestChart(false);
    bms_parser::SpiralModifier modifier(12345);
    modifier.Modify(*chart);
    ASSERT_EQ("2;5;1;4;0;3;6", noteLanesByTimeline(chart.get()),
              "spiral_modifier_lanes: ");
  }
  {
    auto chart = makeModifierTestChart(false);
    bms_parser::HRandomModifier modifier(12345);
    modifier.Modify(*chart);
    ASSERT_EQ("5;4;0;1;4;6;5", noteLanesByTimeline(chart.get()),
              "h_random_modifier_lanes: ");
  }
  {
    auto chart = makeModifierTestChart(true);
    bms_parser::AllScratchModifier modifier(12345);
    modifier.Modify(*chart);
    ASSERT_EQ("7;7;7;7;7;7;7;7", noteLanesByTimeline(chart.get()),
              "all_scr_modifier_lanes: ");
  }
  {
    auto modifier = bms_parser::CreatePlayOptionModifier("S-RANDOM-EX", 12345);
    const bool modifierCreated = modifier != nullptr;
    ASSERT_EQ(true, modifierCreated, "modifier_factory_s_random_ex: ");
    ASSERT_EQ(std::string("S-RANDOM-EX"), std::string(modifier->Name()),
              "modifier_factory_name: ");
    auto chart = makeModifierTestChart(true);
    modifier->Modify(*chart);
    ASSERT_EQ("2;1;1;6;7;0;5;3", noteLanesByTimeline(chart.get()),
              "s_random_ex_modifier_lanes: ");
  }
  {
    auto modifier = bms_parser::CreatePlayOptionModifier("NORMAL");
    const bool modifierCreated = modifier != nullptr;
    ASSERT_EQ(true, modifierCreated, "modifier_factory_normal: ");
    auto chart = makeModifierTestChart(true);
    modifier->Modify(*chart);
    ASSERT_EQ("7,0,1,2,3,4,5,6",
              joinLaneIndices(modifier->GetLaneOrder(chart->Meta)),
              "modifier_factory_normal_lane_order: ");
  }
  {
    auto modifier =
        bms_parser::CreatePlayOptionModifier("ASSIGN:1234567S", 12345);
    const bool modifierCreated = modifier != nullptr;
    ASSERT_EQ(true, modifierCreated, "modifier_factory_lane_assign: ");
    ASSERT_EQ(std::string("ASSIGN:1234567S"), std::string(modifier->Name()),
              "modifier_factory_lane_assign_name: ");
    auto chart = makeModifierTestChart(true);
    modifier->Modify(*chart);
    ASSERT_EQ("7;0;1;2;3;4;5;6", noteLanesByTimeline(chart.get()),
              "modifier_factory_lane_assign_lanes: ");
  }
  {
    auto modifier =
        bms_parser::CreatePlayOptionModifier("L123456789ABCDER", 12345);
    const bool modifierCreated = modifier != nullptr;
    ASSERT_EQ(true, modifierCreated, "modifier_factory_dp_lane_assign: ");
    ASSERT_EQ(std::string("ASSIGN:L123456789ABCDER"),
              std::string(modifier->Name()),
              "modifier_factory_dp_lane_assign_name: ");
    auto chart = makeDpModifierTestChart();
    modifier->Modify(*chart);
    ASSERT_EQ("0;1;2;3;4;5;6;7;8;9;10;11;12;13;14;15",
              noteLanesByTimeline(chart.get()),
              "modifier_factory_dp_lane_assign_lanes: ");
  }
  return 0;
}

int runLongNoteTypeTests() {
  {
    const std::string content =
        "#TITLE undefined-ln\n"
        "#WAV01 a.wav\n"
        "#WAV02 b.wav\n"
        "#00151:0102\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);
    auto heads = longNoteHeads(chart);
    ASSERT_EQ(static_cast<size_t>(1), heads.size(),
              "long_note_type_undefined_count: ");
    ASSERT_EQ(static_cast<int>(bms_parser::LongNoteType::Undefined),
              static_cast<int>(heads[0]->Type),
              "long_note_type_undefined_head: ");
    ASSERT_EQ(static_cast<int>(bms_parser::LongNoteType::Undefined),
              static_cast<int>(heads[0]->Tail->Type),
              "long_note_type_undefined_tail: ");
    delete chart;
  }
  {
    const std::string content =
        "#TITLE lnmode-hcn\n"
        "#LNMODE 3\n"
        "#WAV01 a.wav\n"
        "#WAV02 b.wav\n"
        "#00151:0102\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);
    auto heads = longNoteHeads(chart);
    ASSERT_EQ(static_cast<size_t>(1), heads.size(),
              "long_note_type_hcn_count: ");
    ASSERT_EQ(static_cast<int>(bms_parser::LongNoteType::HellChargeNote),
              static_cast<int>(heads[0]->Type),
              "long_note_type_hcn_head: ");
    ASSERT_EQ(static_cast<int>(bms_parser::LongNoteType::HellChargeNote),
              static_cast<int>(heads[0]->Tail->Type),
              "long_note_type_hcn_tail: ");
    delete chart;
  }
  {
    const std::string content =
        "#TITLE lnobj\n"
        "#LNOBJ 02\n"
        "#WAV01 a.wav\n"
        "#WAV02 b.wav\n"
        "#00111:0102\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);
    auto heads = longNoteHeads(chart);
    ASSERT_EQ(static_cast<size_t>(1), heads.size(),
              "long_note_type_lnobj_count: ");
    ASSERT_EQ(static_cast<int>(bms_parser::LongNoteType::Undefined),
              static_cast<int>(heads[0]->Type),
              "long_note_type_lnobj_head: ");
    ASSERT_EQ(static_cast<int>(bms_parser::LongNoteType::Undefined),
              static_cast<int>(heads[0]->Tail->Type),
              "long_note_type_lnobj_tail: ");
    delete chart;
  }
  {
    const std::string content =
        "#TITLE lnmode-hcn-lnobj\n"
        "#LNMODE 3\n"
        "#LNOBJ 02\n"
        "#WAV01 a.wav\n"
        "#WAV02 b.wav\n"
        "#00111:0102\n";
    bms_parser::Chart *chart = nullptr;
    std::atomic_bool cancel = false;
    bms_parser::Parser parser;
    parser.Parse(bytesFromString(content), &chart, false, false, cancel);
    auto heads = longNoteHeads(chart);
    ASSERT_EQ(static_cast<size_t>(1), heads.size(),
              "long_note_type_lnobj_hcn_count: ");
    ASSERT_EQ(static_cast<int>(bms_parser::LongNoteType::HellChargeNote),
              static_cast<int>(heads[0]->Type),
              "long_note_type_lnobj_hcn_head: ");
    ASSERT_EQ(static_cast<int>(bms_parser::LongNoteType::HellChargeNote),
              static_cast<int>(heads[0]->Tail->Type),
              "long_note_type_lnobj_hcn_tail: ");
    delete chart;
  }
  return 0;
}

int main() {
  const int encodingTestResult = runEncodingTests();
  if (encodingTestResult != 0) {
    return encodingTestResult;
  }

  const int longNoteTypeTestResult = runLongNoteTypeTests();
  if (longNoteTypeTestResult != 0) {
    return longNoteTypeTestResult;
  }

  // read inputs from ./testcases/*.bme
  std::vector<std::filesystem::path> inputs;
  for (auto &p : std::filesystem::directory_iterator("./testcases")) {
    if (p.path().extension() == ".bme") {
      inputs.push_back(p.path());
    }
  }

  for (auto &input : inputs) {
    std::filesystem::path output_path = input;
    output_path.replace_extension(".output");
    if (std::filesystem::exists(output_path)) {
      std::cout << "Testing " << input << "..." << std::endl;
      bms_parser::Chart *chart;
      std::atomic_bool cancel = false;
      bms_parser::Parser parser;
      parser.Parse(input.wstring(), &chart, false, false, cancel);
      std::ifstream ifs(output_path);
      std::string line;
      while (std::getline(ifs, line)) {
        if (line.rfind("md5: ", 0) == 0) {
          auto out = line.substr(5);
          ASSERT_EQ(out, chart->Meta.MD5, "md5: ");
        } else if (line.rfind("sha256: ", 0) == 0) {
          auto out = line.substr(8);
          ASSERT_EQ(out, chart->Meta.SHA256, "sha256: ");
        } else if (line.rfind("title: ", 0) == 0) {
          auto out = line.substr(7);
          ASSERT_EQ(out, chart->Meta.Title, "title: ");
        } else if (line.rfind("artist: ", 0) == 0) {
          auto out = line.substr(8);
          ASSERT_EQ(out, chart->Meta.Artist, "artist: ");
        } else if (line.rfind("genre: ", 0) == 0) {
          auto out = line.substr(7);
          ASSERT_EQ(out, chart->Meta.Genre, "genre: ");
        } else if (line.rfind("subartist: ", 0) == 0) {
          auto out = line.substr(11);
          ASSERT_EQ(out, chart->Meta.SubArtist, "subartist: ");
        } else if (line.rfind("total: ", 0) == 0) {
          auto out = std::stod(line.substr(7));
          ASSERT_EQ(out, chart->Meta.Total, "total: ");
        } else if (line.rfind("total_notes: ", 0) == 0) {
          auto out = std::stoi(line.substr(13));
          ASSERT_EQ(out, chart->Meta.TotalNotes, "total_notes: ");
        } else if (line.rfind("total_backspin_notes: ", 0) == 0) {
          auto out = std::stoi(line.substr(22));
          ASSERT_EQ(out, chart->Meta.TotalBackSpinNotes,
                    "total_backspin_notes: ");
        } else if (line.rfind("total_long_notes: ", 0) == 0) {
          auto out = std::stoi(line.substr(18));
          ASSERT_EQ(out, chart->Meta.TotalLongNotes, "total_long_notes: ");
        } else if (line.rfind("total_scratch_notes: ", 0) == 0) {
          auto out = std::stoi(line.substr(21));
          ASSERT_EQ(out, chart->Meta.TotalScratchNotes,
                    "total_scratch_notes: ");
        } else if (line.rfind("total_landmine_notes: ", 0) == 0) {
          auto out = std::stoi(line.substr(22));
          ASSERT_EQ(out, chart->Meta.TotalLandmineNotes,
                    "total_landmine_notes: ");
        } else if (line.rfind("min_bpm: ", 0) == 0) {
          auto out = std::stod(line.substr(9));
          ASSERT_EQ(out, chart->Meta.MinBpm, "min_bpm: ");
        } else if (line.rfind("max_bpm: ", 0) == 0) {
          auto out = std::stod(line.substr(9));
          ASSERT_EQ(out, chart->Meta.MaxBpm, "max_bpm: ");
        } else if (line.rfind("bpm: ", 0) == 0) {
          auto out = std::stod(line.substr(5));
          ASSERT_EQ(out, chart->Meta.Bpm, "bpm: ");
        } else if (line.rfind("minbpm: ", 0) == 0) {
          auto out = std::stod(line.substr(8));
          ASSERT_EQ(out, chart->Meta.MinBpm, "minbpm: ");
        } else if (line.rfind("maxbpm: ", 0) == 0) {
          auto out = std::stod(line.substr(8));
          ASSERT_EQ(out, chart->Meta.MaxBpm, "maxbpm: ");
        } else if (line.rfind("is_dp: ", 0) == 0) {
          auto out = line.substr(7) == "true";
          ASSERT_EQ(out, chart->Meta.IsDP, "is_dp: ");
        } else if (line.rfind("key_mode: ", 0) == 0) {
          auto out = std::stoi(line.substr(10));
          ASSERT_EQ(out, chart->Meta.KeyMode, "key_mode: ");
        } else if (line.rfind("difficulty: ", 0) == 0) {
          auto out = std::stoi(line.substr(12));
          ASSERT_EQ(out, chart->Meta.Difficulty, "difficulty: ");
        } else if (line.rfind("playlevel: ", 0) == 0) {
          auto out = std::stoi(line.substr(11));
          ASSERT_EQ(out, chart->Meta.PlayLevel, "playlevel: ");
        } else if (line.rfind("player: ", 0) == 0) {
          auto out = std::stoi(line.substr(8));
          ASSERT_EQ(out, chart->Meta.Player, "player: ");
        } else if (line.rfind("rank: ", 0) == 0) {
          auto out = std::stoi(line.substr(6));
          ASSERT_EQ(out, chart->Meta.Rank, "rank: ");
        } else if (line.rfind("playlength: ", 0) == 0) {
          auto out = std::stoi(line.substr(11));
          ASSERT_EQ(out, chart->Meta.PlayLength, "playlength: ");
        } else if (line.rfind("note_lanes_by_timeline: ", 0) == 0) {
          const std::string prefix = "note_lanes_by_timeline: ";
          auto out = line.substr(prefix.length());
          ASSERT_EQ(out, noteLanesByTimeline(chart),
                    "note_lanes_by_timeline: ");
        } else if (line.rfind("total_lane_indices: ", 0) == 0) {
          const std::string prefix = "total_lane_indices: ";
          auto out = line.substr(prefix.length());
          ASSERT_EQ(out, joinLaneIndices(chart->Meta.GetTotalLaneIndices()),
                    "total_lane_indices: ");
        }
      }
      delete chart;
      std::cout << "\tPass" << std::endl;
    }
  }

  if (const int result = runParserRandomTests(); result != 0) {
    return result;
  }
  if (const int result = runReferencedWavTests(); result != 0) {
    return result;
  }
  return runModifierTests();
}
