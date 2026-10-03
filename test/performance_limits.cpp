// Resource-budget regression: scanning an ordinary chart must not retain a
// full timeline/note graph. Inputs are allocated before measurement; requested
// live C++ heap is tracked separately from any throughput benchmark.
#include "../src/Parser.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>

namespace {
bool measuring = false;
std::size_t liveBytes = 0, peakBytes = 0;
struct alignas(std::max_align_t) Allocation { std::size_t measuredBytes; };
}

void *operator new(std::size_t bytes) {
  auto *allocation = static_cast<Allocation *>(
      std::malloc(sizeof(Allocation) + std::max(std::size_t{1}, bytes)));
  if (!allocation) throw std::bad_alloc();
  allocation->measuredBytes = measuring ? bytes : 0;
  liveBytes += allocation->measuredBytes;
  peakBytes = std::max(peakBytes, liveBytes);
  return allocation + 1;
}
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void *pointer) noexcept {
  if (!pointer) return;
  auto *allocation = static_cast<Allocation *>(pointer) - 1;
  liveBytes -= allocation->measuredBytes;
  std::free(allocation);
}
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { ::operator delete(pointer); }

int main() {
  std::string text = "#TITLE scan memory budget\n#BPM 150\n#WAV01 note.wav\n";
  for (int measure = 0; measure < 256; ++measure) {
    char channel[16];
    std::snprintf(channel, sizeof(channel), "#%03d11:", measure);
    text += channel;
    for (int note = 0; note < 1000; ++note) text += "01";
    text += '\n';
  }
  const std::vector<unsigned char> bytes(text.begin(), text.end());
  std::atomic_bool cancelled{false};
  for (bool metadataOnly : {false, true}) {
    peakBytes = liveBytes;
    measuring = true;
    int notes = -1;
    {
      bms_parser::Parser parser;
      if (metadataOnly) {
        bms_parser::Chart *raw = nullptr;
        parser.Parse(bytes, &raw, false, true, cancelled);
        const std::unique_ptr<bms_parser::Chart> chart(raw);
        if (chart) notes = chart->Meta.TotalNotes;
      } else {
        const auto chart = parser.Scan(bytes, cancelled);
        if (chart) notes = chart->Meta.TotalNotes;
      }
    }
    measuring = false;
    std::cout << (metadataOnly ? "Metadata" : "Scan")
              << ": notes=" << notes << " peak_live_bytes=" << peakBytes << '\n';
    if (notes != 256000 || peakBytes > 8 * 1024 * 1024 || liveBytes != 0) {
      std::cerr << "Expected 256000 notes, <=8 MiB peak live heap and no retained allocations\n";
      return 1;
    }
  }
}
