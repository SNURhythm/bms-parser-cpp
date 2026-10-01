#if WITH_AMALGAMATION
#include <bms_parser.hpp>
#else
#include "../src/Parser.h"
#include "../src/LongNote.h"
#include "../src/SHA256.h"
#endif
#include "ChartSnapshot.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <ctime>
#include <cstdlib>
#if defined(__APPLE__) || defined(__unix__)
#include <sys/resource.h>
#endif

#ifdef BMS_BENCH_ALLOCATIONS
namespace {
thread_local bool countAllocations = false;
thread_local size_t allocationCount = 0, allocationBytes = 0;
}
void *operator new(size_t bytes) {
  void *memory = std::malloc(bytes == 0 ? 1 : bytes);
  if (memory == nullptr) throw std::bad_alloc();
  if (countAllocations) { ++allocationCount; allocationBytes += bytes; }
  return memory;
}
void *operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, size_t) noexcept { std::free(memory); }
#endif

// Inputs are preloaded; each job owns its parser/chart and includes destruction.
// Snapshot work is enabled separately, never mixed with throughput measurements.
int main(int argc, char **argv) {
  if (argc < 5 || argc > 7) {
    std::cerr << "usage: performance MANIFEST full|metadata|ready|file|scan WORKERS ROUNDS [SNAPSHOTS] [SEED]\n";
    return 1;
  }
  struct Input { std::filesystem::path path; std::vector<unsigned char> bytes; };
  std::ifstream manifest(argv[1]);
  if (!manifest) return 1;
  std::vector<Input> inputs;
  std::string path;
  size_t inputBytes = 0;
  while (std::getline(manifest, path)) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { std::cerr << "cannot read " << path << '\n'; return 1; }
    inputs.push_back({path, {std::istreambuf_iterator<char>(file), {}}});
    inputBytes += inputs.back().bytes.size();
  }
  const std::string mode(argv[2]);
  if (mode != "full" && mode != "metadata" && mode != "ready" &&
      mode != "file" && mode != "scan-reference"
#ifdef BMS_BENCH_SCAN
      && mode != "scan"
#endif
      ) {
    std::cerr << "unsupported mode: " << mode << '\n';
    return 1;
  }
  const int workers = std::stoi(argv[3]), rounds = std::stoi(argv[4]);
  if (inputs.empty() || workers < 1 || rounds < 1) return 1;
  const bool snapshots = argc > 5;
  const unsigned int seed = argc > 6 ? static_cast<unsigned int>(std::stoul(argv[6])) : 12345;
  std::vector<std::string> results(inputs.size());
  std::cout << "charts=" << inputs.size() << " bytes=" << inputBytes << '\n';
  for (int round = 0; round < rounds; ++round) {
    std::atomic<size_t> next{0}, notes{0};
    std::atomic_bool failed{false};
#ifdef BMS_BENCH_ALLOCATIONS
    std::atomic<size_t> allocations{0}, requestedBytes{0};
#endif
    std::vector<std::thread> threads;
    threads.reserve(workers);
    const auto start = std::chrono::steady_clock::now();
    const auto cpuStart = std::clock();
    for (int worker = 0; worker < workers; ++worker) {
      threads.emplace_back([&] {
        size_t localNotes = 0;
#ifdef BMS_BENCH_ALLOCATIONS
        allocationCount = allocationBytes = 0;
#endif
        for (;;) {
          const size_t i = next.fetch_add(1, std::memory_order_relaxed);
          if (i >= inputs.size()) break;
#ifdef BMS_BENCH_ALLOCATIONS
          countAllocations = true;
#endif
          try {
            bms_parser::Parser parser;
            parser.SetRandomSeed(seed);
            std::atomic_bool cancelled{false};
            std::string snapshot;
#ifdef BMS_BENCH_SCAN
            if (mode == "scan") {
              const auto scan = parser.Scan(inputs[i].bytes, cancelled);
              if (!scan) throw std::runtime_error("no scan result");
              localNotes += scan->Meta.TotalNotes;
              if (snapshots) {
                parser_test::Snapshot out;
                out.add(parser_test::metadataSnapshot(scan->Meta));
                out.add(scan->HasBga); out.add(scan->HasBpmStop); out.add(scan->HasScrollChange);
                snapshot = std::move(out.bytes);
              }
            } else
#endif
            {
              bms_parser::Chart *raw = nullptr;
              if (mode == "file") parser.Parse(inputs[i].path, &raw, false, false, cancelled);
              else parser.Parse(inputs[i].bytes, &raw, mode == "ready", mode == "metadata", cancelled);
              const std::unique_ptr<bms_parser::Chart> chart(raw);
              if (!chart) throw std::runtime_error("no chart");
              localNotes += chart->Meta.TotalNotes;
              if (snapshots) {
                if (mode == "scan-reference") {
                  parser_test::Snapshot out;
                  out.add(parser_test::metadataSnapshot(chart->Meta));
                  bool stop = false, scroll = false;
                  for (const auto *measure : chart->Measures)
                    for (const auto *timeline : measure->TimeLines) {
                      stop = stop || timeline->StopLength > 0;
                      scroll = scroll || timeline->Scroll != 1.0;
                    }
                  out.add(!chart->BmpTable.empty()); out.add(stop); out.add(scroll);
                  snapshot = std::move(out.bytes);
                } else snapshot = parser_test::chartSnapshot(*chart);
              }
            }
            if (snapshots)
              results[i] = bms_parser::sha256({snapshot.begin(), snapshot.end()});
          } catch (const std::exception &error) {
            std::cerr << inputs[i].path << ": " << error.what() << '\n';
            failed = true;
          }
#ifdef BMS_BENCH_ALLOCATIONS
          countAllocations = false;
#endif
        }
        notes.fetch_add(localNotes, std::memory_order_relaxed);
#ifdef BMS_BENCH_ALLOCATIONS
        allocations.fetch_add(allocationCount, std::memory_order_relaxed);
        requestedBytes.fetch_add(allocationBytes, std::memory_order_relaxed);
#endif
      });
    }
    for (auto &thread : threads) thread.join();
    if (failed) return 1;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "round=" << round << " mode=" << mode << " workers=" << workers
              << " wall_ms=" << ms << " cpu_ms=" << 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC
              << " charts_sec=" << inputs.size() * 1000.0 / ms << " notes=" << notes;
#if defined(__APPLE__) || defined(__unix__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
      std::cout << " peak_rss_mb=" << usage.ru_maxrss /
#ifdef __APPLE__
          1048576.0;
#else
          1024.0;
#endif
#endif
#ifdef BMS_BENCH_ALLOCATIONS
    std::cout << " allocations=" << allocations << " requested_bytes=" << requestedBytes;
#endif
    std::cout << std::endl;
  }
  if (snapshots) {
    std::ofstream output(argv[5]);
    if (!output) return 1;
    for (size_t i = 0; i < inputs.size(); ++i)
      output << i << '\t' << results[i] << '\t' << inputs[i].path.generic_string() << '\n';
  }
}
