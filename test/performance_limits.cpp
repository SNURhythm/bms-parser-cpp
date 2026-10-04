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
std::size_t liveBytes = 0, peakBytes = 0, allocationCount = 0;
std::atomic_bool *cancelTarget = nullptr;
std::size_t cancelAtAllocation = 0;
// The allocation prefix must preserve ordinary operator new's ABI alignment;
// on macOS ARM this can exceed alignof(std::max_align_t).
struct alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) Allocation { std::size_t measuredBytes; };
}

void *operator new(std::size_t bytes) {
  auto *allocation = static_cast<Allocation *>(
      std::malloc(sizeof(Allocation) + std::max(std::size_t{1}, bytes)));
  if (!allocation) throw std::bad_alloc();
  allocation->measuredBytes = measuring ? bytes : 0;
  if (measuring) {
    ++allocationCount;
    if (cancelTarget && allocationCount == cancelAtAllocation) *cancelTarget = true;
  }
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
    allocationCount = 0;
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
              << ": notes=" << notes << " peak_live_bytes=" << peakBytes
              << " allocations=" << allocationCount << '\n';
    // A redundant timeline index used to allocate another tree node for each
    // note. Bound allocation churn as well as retained heap on this workload.
    if (notes != 256000 || peakBytes > 8 * 1024 * 1024 || liveBytes != 0 ||
        allocationCount > 3 * 256000) {
      std::cerr << "Expected 256000 notes, <=8 MiB peak live heap, <=3 allocations per note and no retained allocations\n";
      return 1;
    }
  }
  // Interrupt late in a dense measure. Allocation count provides a repeatable
  // trigger without a wall-clock deadline or a production-only test hook.
  std::string denseText = "#BPM 150\n#00111:";
  for (int note = 0; note < 256000; ++note) denseText += "01";
  denseText += '\n';
  const std::vector<unsigned char> denseBytes(denseText.begin(), denseText.end());
  for (bool metadataOnly : {false, true}) {
    allocationCount = 0;
    cancelAtAllocation = 400000;
    cancelled = false;
    cancelTarget = &cancelled;
    measuring = true;
    bool returnedChart = false;
    {
      bms_parser::Parser parser;
      if (metadataOnly) {
        bms_parser::Chart *raw = nullptr;
        parser.Parse(denseBytes, &raw, false, true, cancelled);
        const std::unique_ptr<bms_parser::Chart> chart(raw);
        returnedChart = chart != nullptr;
      } else {
        returnedChart = parser.Scan(denseBytes, cancelled).has_value();
      }
    }
    measuring = false;
    cancelTarget = nullptr;
    std::cout << (metadataOnly ? "Metadata" : "Scan")
              << ": cancelled=" << cancelled << " allocations_after_cancel="
              << (allocationCount > cancelAtAllocation ? allocationCount - cancelAtAllocation : 0)
              << '\n';
    if (!cancelled || returnedChart || liveBytes != 0 ||
        allocationCount > cancelAtAllocation + 1024) {
      std::cerr << "Cancellation must stop allocation-heavy work without publishing a partial chart\n";
      return 1;
    }
  }
  const auto checkFullCancellation = [&](const std::vector<unsigned char> &input,
                                         int expectedNotes,
                                         std::size_t cancelBeforeEnd) {
    std::size_t fullAllocations = 0;
    for (bool interrupt : {false, true}) {
      allocationCount = 0;
      cancelAtAllocation = interrupt ? fullAllocations - cancelBeforeEnd : 0;
      cancelled = false;
      cancelTarget = interrupt ? &cancelled : nullptr;
      measuring = true;
      int notes = -1;
      {
        bms_parser::Parser parser;
        bms_parser::Chart *raw = nullptr;
        parser.Parse(input, &raw, false, false, cancelled);
        const std::unique_ptr<bms_parser::Chart> chart(raw);
        if (chart) notes = chart->Meta.TotalNotes;
      }
      measuring = false;
      cancelTarget = nullptr;
      if (!interrupt) {
        fullAllocations = allocationCount;
        if (notes != expectedNotes || fullAllocations <= cancelBeforeEnd || liveBytes != 0)
          return false;
        continue;
      }
      std::cout << "Full: expected_notes=" << expectedNotes
                << " cancelled=" << cancelled << " allocations_after_cancel="
                << (allocationCount > cancelAtAllocation ? allocationCount - cancelAtAllocation : 0)
                << '\n';
      if (!cancelled || notes != -1 || liveBytes != 0 ||
          allocationCount > cancelAtAllocation + 1024) return false;
    }
    return true;
  };
  // Full parses allocate notes during publication; long holds also move their
  // interior normal notes to BGM when the eventual tail is decoded.
  std::string heldText = "#BPM 150\n#00051:01\n#00111:";
  for (int note = 0; note < 256000; ++note) heldText += "01";
  heldText += "\n#00251:01\n";
  const std::vector<unsigned char> heldBytes(heldText.begin(), heldText.end());
  const bool publicationCancelled = checkFullCancellation(denseBytes, 256000, 128000);
  const bool holdCloseCancelled = checkFullCancellation(heldBytes, 1, 256000);
  if (!publicationCancelled || !holdCloseCancelled) {
    std::cerr << "Cancellation must stop full-parse note publication and hold closure\n";
    return 1;
  }
}
