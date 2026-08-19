// Shared structure-aware fuzz driver (M11.1).
//
// Every fuzz target implements `LLVMFuzzerTestOneInput`, so the targets are
// drop-in libFuzzer binaries when built with clang's `-fsanitize=fuzzer` (CI).
// Without libFuzzer (e.g. a GCC build) the same target links this driver's
// `main`, which replays a seed corpus and then runs a fixed-budget mutation
// loop (bit flips / byte splices / truncations / boundary sizes) — so
// "no crashes/UB for a fixed corpus + time budget" holds on any toolchain.
//
// The whole build runs under ASan/UBSan, so a crash or UB inside a target
// aborts the process and fails the run.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace mtgcpp::fuzz {

// The per-target input function (identical to libFuzzer's entry point). Must
// never leak or throw an uncaught exception.
using FuzzFn = int (*)(const uint8_t *data, std::size_t size);

// Replay every regular file under `path` (a file or a directory, traversed
// non-recursively) through `fn`. Returns the number of inputs fed.
std::size_t replayCorpus(FuzzFn fn, const std::filesystem::path &path);

// Load every corpus file under `dir` as a seed byte buffer.
std::vector<std::vector<uint8_t>> loadCorpus(const std::filesystem::path &dir);

// One mutation of `input` (bit flip / random byte / truncate / splice / length
// boundary), always returning a bounded-size candidate.
std::vector<uint8_t> mutate(const std::vector<uint8_t> &input, std::mt19937 &rng);

// Run the mutation loop for `budget`, seeded from the corpus + `seed`. Replays
// the corpus first, then mutates. Returns the number of iterations executed; a
// crash / UB aborts the process.
std::uint64_t runFuzz(FuzzFn fn, const std::filesystem::path &corpusDir,
                      std::chrono::seconds budget, std::uint32_t seed);

// The `main` glue for non-libFuzzer builds: parses a driver-style argv
// (`-max_total_time=<sec>` = fixed-budget mutation run over `defaultCorpus`;
// `-seed=<n>`; otherwise every remaining argument is a file/dir to replay) and
// runs `fn` accordingly. Returns the process exit code.
int runDriverMain(FuzzFn fn, int argc, char **argv, const std::filesystem::path &defaultCorpus);

} // namespace mtgcpp::fuzz
