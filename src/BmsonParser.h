#pragma once

#include "Parser.h"

namespace bms_parser::bmson {
bool IsSource(const std::vector<unsigned char> &bytes,
              const std::filesystem::path &sourcePath);
std::unique_ptr<Chart> Parse(const std::vector<unsigned char> &bytes,
                             bool addReadyMeasure, bool metaOnly,
                             std::atomic_bool &cancelled,
                             ChartScanResult *scan = nullptr);
}
