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
            parser.Parse(input.wstring(), &chart, false, false, cancel);
            std::wifstream ifs(output_path);
            std::wstring line;
            while (std::getline(ifs, line))
            {
                if (line.rfind(L"md5: ", 0) == 0)
                {
                    auto out = line.substr(5);
                    ASSERT_EQ(ws2s(out), chart->Meta.MD5, "md5: ");
                }
                else if (line.rfind(L"sha256: ", 0) == 0)
                {
                    auto out = line.substr(8);
                    ASSERT_EQ(ws2s(out), chart->Meta.SHA256, "sha256: ");
                }
                else if (line.rfind(L"title: ", 0) == 0)
                {
                    auto out = std::wstring(line.begin() + 7, line.end());
                    ASSERT_EQW(out, chart->Meta.Title, "title: ");
                }
                else if (line.rfind(L"artist: ", 0) == 0)
                {
                    auto out = std::wstring(line.begin() + 8, line.end());
                    ASSERT_EQW(out, chart->Meta.Artist, "artist: ");
                }
                else if (line.rfind(L"genre: ", 0) == 0)
                {
                    auto out = std::wstring(line.begin() + 7, line.end());
                    ASSERT_EQW(out, chart->Meta.Genre, "genre: ");
                }
                else if (line.rfind(L"subartist: ", 0) == 0)
                {
                    auto out = std::wstring(line.begin() + 11, line.end());
                    ASSERT_EQW(out, chart->Meta.SubArtist, "subartist: ");
                }
                else if (line.rfind(L"total: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(7));
                    ASSERT_EQ(out, chart->Meta.Total, "total: ");
                }
                else if (line.rfind(L"total_notes: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(13));
                    ASSERT_EQ(out, chart->Meta.TotalNotes, "total_notes: ");
                }
                else if (line.rfind(L"total_backspin_notes: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(22));
                    ASSERT_EQ(out, chart->Meta.TotalBackSpinNotes, "total_backspin_notes: ");
                }
                else if (line.rfind(L"total_long_notes: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(18));
                    ASSERT_EQ(out, chart->Meta.TotalLongNotes, "total_long_notes: ");
                }
                else if (line.rfind(L"total_scratch_notes: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(21));
                    ASSERT_EQ(out, chart->Meta.TotalScratchNotes, "total_scratch_notes: ");
                }
                else if (line.rfind(L"min_bpm: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(9));
                    ASSERT_EQ(out, chart->Meta.MinBpm, "min_bpm: ");
                }
                else if (line.rfind(L"max_bpm: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(9));
                    ASSERT_EQ(out, chart->Meta.MaxBpm, "max_bpm: ");
                }
                else if (line.rfind(L"bpm: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(5));
                    ASSERT_EQ(out, chart->Meta.Bpm, "bpm: ");
                }
                else if (line.rfind(L"minbpm: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(8));
                    ASSERT_EQ(out, chart->Meta.MinBpm, "minbpm: ");
                }
                else if (line.rfind(L"maxbpm: ", 0) == 0)
                {
                    auto out = std::stod(line.substr(8));
                    ASSERT_EQ(out, chart->Meta.MaxBpm, "maxbpm: ");
                }
                else if (line.rfind(L"is_dp: ", 0) == 0)
                {
                    auto out = line.substr(7) == L"true";
                    ASSERT_EQ(out, chart->Meta.IsDP, "is_dp: ");
                }
                else if (line.rfind(L"key_mode: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(10));
                    ASSERT_EQ(out, chart->Meta.KeyMode, "key_mode: ");
                }
                else if (line.rfind(L"difficulty: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(12));
                    ASSERT_EQ(out, chart->Meta.Difficulty, "difficulty: ");
                }
                else if (line.rfind(L"playlevel: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(11));
                    ASSERT_EQ(out, chart->Meta.PlayLevel, "playlevel: ");
                }
                else if (line.rfind(L"player: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(8));
                    ASSERT_EQ(out, chart->Meta.Player, "player: ");
                }
                else if (line.rfind(L"rank: ", 0) == 0)
                {
                    auto out = std::stoi(line.substr(6));
                    ASSERT_EQ(out, chart->Meta.Rank, "rank: ");
                }
                else if (line.rfind(L"playlength: ", 0) == 0)
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