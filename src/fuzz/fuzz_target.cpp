// Shared structure-aware fuzz driver implementation (M11.1).

#include "fuzz/fuzz_target.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace mtgcpp::fuzz {

namespace {

// Hard cap on any single input, so a mutated candidate can never exhaust
// memory inside a fuzz loop.
constexpr std::size_t kMaxInputSize = 1024 * 1024;

// Feed `fn` a bounded copy of `bytes` (truncated to the cap).
void feed(FuzzFn fn, const std::vector<uint8_t> &bytes) {
  if (bytes.size() > kMaxInputSize) {
    const std::vector<uint8_t> capped(bytes.begin(), bytes.begin() + kMaxInputSize);
    fn(capped.data(), capped.size());
    return;
  }
  fn(bytes.data(), bytes.size());
}

// Read a whole file as bytes (binary-safe, explicit char -> uint8_t).
std::vector<uint8_t> readBytes(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> bytes;
  for (std::istreambuf_iterator<char> it(in), end; it != end; ++it) {
    bytes.push_back(static_cast<uint8_t>(static_cast<unsigned char>(*it)));
  }
  return bytes;
}

// A uniform index in [0, n) from the RNG (n >= 1).
std::size_t pickSeed(std::mt19937 &rng, std::size_t n) {
  return static_cast<std::size_t>(rng() % static_cast<std::uint32_t>(n));
}

// Non-throwing integer parse for driver flags; `fallback` on garbage.
int parseSeconds(const std::string &value, int fallback) {
  if (value.empty() || !std::all_of(value.begin(), value.end(),
                                    [](unsigned char c) { return std::isdigit(c) != 0; })) {
    return fallback;
  }
  try {
    return std::stoi(value);
  } catch (const std::exception &) {
    return fallback;
  }
}

std::uint32_t parseSeed(const std::string &value) {
  if (value.empty() || !std::all_of(value.begin(), value.end(),
                                    [](unsigned char c) { return std::isdigit(c) != 0; })) {
    return 1;
  }
  try {
    return static_cast<std::uint32_t>(std::stoul(value));
  } catch (const std::exception &) {
    return 1;
  }
}

} // namespace

std::size_t replayCorpus(FuzzFn fn, const std::filesystem::path &path) {
  std::size_t count = 0;
  std::error_code ec;
  if (std::filesystem::is_regular_file(path, ec) && !ec) {
    const std::vector<uint8_t> bytes = readBytes(path);
    feed(fn, bytes);
    ++count;
    return count;
  }
  ec.clear();
  if (std::filesystem::is_directory(path, ec) && !ec) {
    for (std::filesystem::directory_iterator it(path, ec), end; it != end && !ec;
         it.increment(ec)) {
      if (it->is_regular_file()) {
        count += replayCorpus(fn, it->path());
      }
    }
  }
  return count;
}

std::vector<std::vector<uint8_t>> loadCorpus(const std::filesystem::path &dir) {
  std::vector<std::vector<uint8_t>> seeds;
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec) || ec) {
    return seeds;
  }
  for (std::filesystem::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
    if (!it->is_regular_file()) {
      continue;
    }
    seeds.push_back(readBytes(it->path()));
  }
  return seeds;
}

std::vector<uint8_t> mutate(const std::vector<uint8_t> &input, std::mt19937 &rng) {
  std::vector<uint8_t> out = input;
  const auto pick = [&rng](std::size_t n) {
    return n == 0u ? 0u : static_cast<std::size_t>(rng() % static_cast<std::uint32_t>(n));
  };
  const unsigned op = static_cast<unsigned>(rng() % 6u);
  switch (op) {
  case 0u: // flip one bit
    if (!out.empty()) {
      out.at(pick(out.size())) ^= static_cast<uint8_t>(1u << (rng() % 8u));
    }
    break;
  case 1u: // overwrite one byte
    if (!out.empty()) {
      out.at(pick(out.size())) = static_cast<uint8_t>(rng() & 0xFFu);
    }
    break;
  case 2u: // truncate (drives length-boundary handling in the framing)
    if (!out.empty()) {
      out.resize(pick(out.size() + 1u));
    }
    break;
  case 3u: // duplicate a chunk
    if (!out.empty()) {
      const std::size_t at = pick(out.size());
      const std::size_t len = pick(out.size() - at + 1u);
      out.insert(out.begin() + static_cast<std::ptrdiff_t>(at),
                 out.begin() + static_cast<std::ptrdiff_t>(at),
                 out.begin() + static_cast<std::ptrdiff_t>(at + len));
    }
    break;
  case 4u: // delete a chunk
    if (!out.empty()) {
      const std::size_t at = pick(out.size());
      const std::size_t len = pick(out.size() - at + 1u);
      out.erase(out.begin() + static_cast<std::ptrdiff_t>(at),
                out.begin() + static_cast<std::ptrdiff_t>(at + len));
    }
    break;
  default: // weird sizes (exercise the 4-byte length header boundaries)
    out.resize(pick(64u));
    break;
  }
  if (out.size() > kMaxInputSize) {
    out.resize(kMaxInputSize);
  }
  return out;
}

std::uint64_t runFuzz(FuzzFn fn, const std::filesystem::path &corpusDir,
                      std::chrono::seconds budget, std::uint32_t seed) {
  std::vector<std::vector<uint8_t>> seeds = loadCorpus(corpusDir);
  if (seeds.empty()) {
    seeds.emplace_back(); // the empty input is always interesting
  }
  // Replay the seed corpus once before mutating.
  for (const std::vector<uint8_t> &input : seeds) {
    feed(fn, input);
  }
  std::mt19937 rng(seed);
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + budget;
  std::uint64_t iterations = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const std::vector<uint8_t> &base = seeds.at(pickSeed(rng, seeds.size()));
    feed(fn, mutate(base, rng));
    ++iterations;
  }
  return iterations;
}

int runDriverMain(FuzzFn fn, int argc, char **argv, const std::filesystem::path &defaultCorpus) {
  std::chrono::seconds budget{0};
  std::uint32_t seed = 1;
  std::vector<std::filesystem::path> inputs;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("-max_total_time=", 0) == 0) {
      budget = std::chrono::seconds(parseSeconds(arg.substr(16), 1));
    } else if (arg.rfind("-seed=", 0) == 0) {
      seed = parseSeed(arg.substr(6));
    } else {
      inputs.emplace_back(arg);
    }
  }
  if (budget.count() > 0) {
    const std::filesystem::path corpus = inputs.empty() ? defaultCorpus : inputs.front();
    runFuzz(fn, corpus, budget, seed);
    return 0;
  }
  if (inputs.empty()) {
    inputs.push_back(defaultCorpus);
  }
  std::size_t fed = 0;
  for (const std::filesystem::path &input : inputs) {
    fed += replayCorpus(fn, input);
  }
  return fed == 0u ? 1 : 0;
}

} // namespace mtgcpp::fuzz
