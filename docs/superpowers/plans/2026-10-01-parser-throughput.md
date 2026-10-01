# Parser Throughput Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Improve concurrent parser throughput while preserving existing results.
**Architecture:** Shared parser implementation with explicit output modes; optimize private bookkeeping and portable hashes while preserving public chart ownership. Add a lightweight scan result for clients that need metadata/features.
**Tech Stack:** Dependency-free C++17, Python validation drivers, existing Make tests.
**Spec:** docs/superpowers/specs/2026-10-01-parser-throughput-design.md

## Global Constraints

- Existing full and metadata Parse behavior, encoding detection and hashes stay unchanged.
- C++17, no third-party dependencies, no internal worker threads or shared allocation pools.
- Keep public vector types and delete-based ownership.
- Preserve unrelated consumer work.

## Review Focus

- Duplicate events and reduced rational positions must preserve timing/statistics.
- LNOBJ, channel long notes, undefined resources and malformed cells retain current counting/linkage.
- Scan must reproduce full results for late background/invisible events, conditional BMP declarations, overwritten stops and effective scroll changes.
- Hash padding/chunk boundaries and all input bytes preserve digests.
- Independent parser instances, cancellation and ownership remain safe across threads.

### Task 1: Characterization and reproducible validation

**Files:** test/ChartSnapshot.h, test/performance.cpp, test/main.cpp, scripts/check_parser_equivalence.py
**Interfaces:** snapshot helpers and a CLI for full/meta/scan corpus checks and parallel benchmarks.
- [x] Capture an immutable baseline build and add complete semantic snapshot helpers.
- [x] Add hash boundary/streaming checks and edge fixtures covering duplicate timeline positions, long notes, malformed cells and locale-sensitive headers.
- [x] Run the unchanged baseline tests and generate corpus reference fingerprints.

### Task 2: Private parser bookkeeping and allocation

**Files:** src/Parser.cpp, test/main.cpp
**Interfaces:** existing Parse methods unchanged; snapshots from Task 1.
- [x] Replace repeated map lookup/insertion with one insertion result.
- [x] Replace per-event temporary set nodes with reusable compact storage; preserve rational equality and order-independent membership.
- [x] Use explicit lane state to avoid temporary Note/LongNote allocation in metaOnly mode.
- [x] Optimize guarded cell/header checks, retaining locale behavior and fallback.
- [x] Verify baseline snapshots, tests and allocation reduction.

### Task 3: Portable hashing

**Files:** src/SHA256.cpp, src/MD5.cpp, test/main.cpp
**Interfaces:** existing digest classes/functions unchanged.
- [x] Verify known vectors, binary inputs, padding boundaries and chunked updates before editing.
- [x] Unroll SHA-256 working-state rounds and directly encode lowercase digest hex.
- [x] Run independent hashlib equivalence and measure hash/full throughput.

### Task 4: Lightweight scan output

**Files:** src/Parser.h, src/Parser.cpp, test/main.cpp, README.md
**Interfaces:** ChartScanResult containing ChartMeta and three flags; optional Scan(byte/path, cancellation) methods.
- [x] Add failing full-versus-scan assertions over all metadata and the three feature flags.
- [x] Add Scan via the shared parser; process all full-mode events but avoid constructing notes and retained chart timelines.
- [x] Preserve legacy metaOnly separately; add file path/cancellation coverage and parallel scan checks.
- [x] Compare real corpus and fixtures against full parse; document API semantics.

### Task 5: Consumer integration and final verification

**Files:** AsoBMaShow/src/ChartLibraryScanner.cpp and generated parser files; benchmark tools/docs as needed.
**Interfaces:** Task 4 Scan API consumed by the existing scanner.
- [x] Regenerate amalgamation and update only scanner parsing/feature extraction.
- [x] Run normal/amalgamation suites, sanitizer checks, corpus baseline equivalence and consumer tests/build.
- [x] Measure alternating baseline/candidate full and scan throughput at multiple worker counts, with allocation counts separately.
- [x] Obtain an independent code review and address correctness findings before finishing.
