#include <string>
#include <filesystem>
#include <atomic>
#include <fstream>
#include <iostream>
#if WITH_AMALGAMATION
#include "bms_parser.hpp"
#else
#include "../src/Parser.h"
#include "../src/Chart.h"
#endif

#define ASSERT_EQ(a, b, desc) \
    if (a != b)               \
    {                         \
        std::cerr << desc << std::endl;    \
        std::cerr << "\tExpected: " << a << std::endl; \
        std::cerr << "\tActual: " << b << std::endl; \
        return 1;             \
    } else { \
        std::cout << "\t" << desc << " passed" << std::endl; \
    }
#define ASSERT_EQW(a, b, desc) \
    if (a != b)               \
    {                         \
        std::cerr << desc;    \
        std::wcerr << "\tExpected: " << a << std::endl; \
        std::wcerr << "\tActual: " << b << std::endl; \
        return 1;             \
    } else { \
        std::cout << "\t" << desc << " passed" << std::endl; \
    }
std::string ws2s(const std::wstring &wstr)
{
  return std::string().assign(wstr.begin(), wstr.end());
}
int main()
{
    // read inputs from ./testcases/*.bme
    std::vector<std::filesystem::path> inputs;
    for (auto &p : std::filesystem::directory_iterator("./testcases"))
    {
        if (p.path().extension() == ".bme")
        {
            inputs.push_back(p.path());
        }
    }

    for (auto &input : inputs)
    {
        std::filesystem::path output_path = input;
        output_path.replace_extension(".output");
        if (std::filesystem::exists(output_path))
        {
            std::cout << "Testing " << input << "..." << std::endl;
            bms_parser::Chart *chart;
            std::atomic_bool cancel = false;
            bms_parser::Parser parser;
            parser.Parse(input, &chart, false, false, cancel);
            std::ifstream ifs(output_path);
            std::string line;
            while (std::getline(ifs, line))
            {
                if (line.rfind("md5: ", 0) == 0)
                {
                    auto out = line.substr(5);
                    ASSERT_EQ(out, chart->Meta.MD5, "md5: ");
                }
                else if (line.rfind("sha256: ", 0) == 0)
                {
                    auto out = line.substr(8);
                    ASSERT_EQ(out, chart->Meta.SHA256, "sha256: ");
                }
                else if (line.rfind("title: ", 0) == 0)
                {
                    auto out = line.substr(7);
                    ASSERT_EQ(out, chart->Meta.Title, "title: ");
                }
                else if (line.rfind("artist: ", 0) == 0)
                {
                    auto out = line.substr(8);
                    ASSERT_EQ(out, chart->Meta.Artist, "artist: ");
                }
                else if (line.rfind("genre: ", 0) == 0)
                {
                    auto out = line.substr(7);
                    ASSERT_EQ(out, chart->Meta.Genre, "genre: ");
                }
                else if (line.rfind("subartist: ", 0) == 0)
                {
                    auto out = line.substr(11);
                    ASSERT_EQ(out, chart->Meta.SubArtist, "subartist: ");
                }
                else if (line.rfind("total: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(7));
                    ASSERT_EQ(out, chart->Meta.Total, "total: ");
                }
                else if (line.rfind("total_notes: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(13));
                    ASSERT_EQ(out, chart->Meta.TotalNotes, "total_notes: ");
                }
                else if (line.rfind("total_backspin_notes: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(22));
                    ASSERT_EQ(out, chart->Meta.TotalBackSpinNotes, "total_backspin_notes: ");
                }
                else if (line.rfind("total_long_notes: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(18));
                    ASSERT_EQ(out, chart->Meta.TotalLongNotes, "total_long_notes: ");
                }
                else if (line.rfind("total_scratch_notes: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(21));
                    ASSERT_EQ(out, chart->Meta.TotalScratchNotes, "total_scratch_notes: ");
                }
                else if (line.rfind("total_landmine_notes: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(22));
                    ASSERT_EQ(out, chart->Meta.TotalLandmineNotes, "total_landmine_notes: ");
                }
                else if (line.rfind("min_bpm: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(9));
                    ASSERT_EQ(out, chart->Meta.MinBpm, "min_bpm: ");
                }
                else if (line.rfind("max_bpm: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(9));
                    ASSERT_EQ(out, chart->Meta.MaxBpm, "max_bpm: ");
                }
                else if (line.rfind("bpm: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(5));
                    ASSERT_EQ(out, chart->Meta.Bpm, "bpm: ");
                }
                else if (line.rfind("minbpm: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(8));
                    ASSERT_EQ(out, chart->Meta.MinBpm, "minbpm: ");
                }
                else if (line.rfind("maxbpm: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(8));
                    ASSERT_EQ(out, chart->Meta.MaxBpm, "maxbpm: ");
                }
                else if (line.rfind("is_dp: ", 0) == 0)
                {
                    auto out = line.substr(7) == "true";
                    ASSERT_EQ(out, chart->Meta.IsDP, "is_dp: ");
                }
                else if (line.rfind("key_mode: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(10));
                    ASSERT_EQ(out, chart->Meta.KeyMode, "key_mode: ");
                }
                else if (line.rfind("difficulty: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(12));
                    ASSERT_EQ(out, chart->Meta.Difficulty, "difficulty: ");
                }
                else if (line.rfind("playlevel: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(11));
                    ASSERT_EQ(out, chart->Meta.PlayLevel, "playlevel: ");
                }
                else if (line.rfind("player: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(8));
                    ASSERT_EQ(out, chart->Meta.Player, "player: ");
                }
                else if (line.rfind("rank: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(6));
                    ASSERT_EQ(out, chart->Meta.Rank, "rank: ");
                }
                else if (line.rfind("playlength: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(11));
                    ASSERT_EQ(out, chart->Meta.PlayLength, "playlength: ");
                }
            }
            delete chart;
            std::cout << "\tPass" << std::endl;
        }
    }

    return 0;
}