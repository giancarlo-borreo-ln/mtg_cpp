// M11.1 fuzz-corpus regression: the seed corpora + a fixed set of adversarial
// inputs are replayed through the deck parser and the network envelope/framing
// under the normal test suite, so every CI runner proves the same "no crashes /
// no UB" property the fuzz targets assert under a time budget (the deterministic
// cases also pin expected outcomes).

#include "core/deck_parser.h"
#include "net/envelope.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace mtgcpp {
namespace {

std::filesystem::path seedDir(const std::string &target) {
  return std::filesystem::path(MTG_CPP_SOURCE_DIR) / "src" / "fuzz" / "seed_corpus" / target;
}

// Read a whole file as bytes (binary-safe).
std::vector<uint8_t> readBytes(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> bytes;
  for (std::istreambuf_iterator<char> it(in), end; it != end; ++it) {
    bytes.push_back(static_cast<uint8_t>(static_cast<unsigned char>(*it)));
  }
  return bytes;
}

std::string textOf(const std::vector<uint8_t> &bytes) {
  std::string text(bytes.size(), '\0');
  if (!bytes.empty()) {
    std::memcpy(text.data(), bytes.data(), bytes.size());
  }
  return text;
}

// Replay a corpus directory through the deck parser (the resilient path: a
// text with no parseable lines raises DeckParseError, which is expected).
void replayDeckParser(const std::filesystem::path &dir) {
  for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string text = textOf(readBytes(entry.path()));
    try {
      const core::ParsedSections sections = core::parseArenaSections(text);
      for (const auto &[section, entries] : sections) {
        (void)section;
        core::aggregateEntries(entries);
      }
    } catch (const core::DeckParseError &) {
      // Expected and documented: a text with zero parseable card lines.
      (void)0;
    }
  }
}

// Replay a corpus directory through the envelope + frame decoder.
void replayEnvelope(const std::filesystem::path &dir) {
  for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string text = textOf(readBytes(entry.path()));
    (void)net::parseEnvelope(text);
    net::FrameDecoder decoder;
    decoder.push(text);
    for (;;) {
      if (!decoder.next().has_value()) {
        break;
      }
    }
  }
}

TEST(FuzzCorpus, DeckParserSeedCorpusIsClean) {
  const std::filesystem::path dir = seedDir("deck_parser");
  ASSERT_TRUE(std::filesystem::is_directory(dir));
  EXPECT_NO_FATAL_FAILURE(replayDeckParser(dir));
}

TEST(FuzzCorpus, EnvelopeSeedCorpusIsClean) {
  const std::filesystem::path dir = seedDir("envelope");
  ASSERT_TRUE(std::filesystem::is_directory(dir));
  EXPECT_NO_FATAL_FAILURE(replayEnvelope(dir));
}

TEST(FuzzCorpus, DeckParserParsesTheValidSeedDeterministically) {
  const std::string text = textOf(readBytes(seedDir("deck_parser") / "valid_export.txt"));
  const core::ParsedSections sections = core::parseArenaSections(text);
  EXPECT_EQ(sections.at(core::ArenaSection::Mainboard).size(), 3u);
  EXPECT_EQ(sections.at(core::ArenaSection::Sideboard).size(), 1u);
  EXPECT_EQ(sections.at(core::ArenaSection::Commander).size(), 1u);
}

TEST(FuzzCorpus, OversizeLengthHeaderIsRejected) {
  const std::vector<uint8_t> bytes = readBytes(seedDir("envelope") / "oversize_header.bin");
  ASSERT_EQ(bytes.size(), 4u);
  const std::string text = textOf(bytes);
  net::FrameDecoder decoder;
  decoder.push(text);
  EXPECT_FALSE(decoder.next().has_value());
  EXPECT_TRUE(decoder.oversize());
}

TEST(FuzzCorpus, TruncatedAndGarbageInputsNeverCrash) {
  const std::string garbage = textOf(readBytes(seedDir("envelope") / "garbage.bin"));
  EXPECT_FALSE(net::parseEnvelope(garbage).has_value());

  net::FrameDecoder decoder;
  decoder.push(garbage);
  EXPECT_FALSE(decoder.next().has_value());
  EXPECT_FALSE(decoder.oversize());
  // A partial 4-byte header is buffered, not a frame, and never a crash.
  EXPECT_EQ(decoder.buffered(), garbage.size());
}

TEST(FuzzCorpus, AdversarialParserInputsAreSafe) {
  // A single oversized line, null bytes, non-ASCII, and an almost-valid
  // printing line with a huge quantity must all be handled without a crash.
  // Some inputs have no parseable card line and legitimately raise
  // DeckParseError — that is the documented contract, not a failure.
  const std::vector<std::string> adversarial = {
      std::string(1024 * 1024, 'a'),
      "4 Forest (WAR) 99999999999999999999\n",
      std::string("1\0\0Land\0(WAR) 263\n", 18),
      "Deck\n4 \u00e9clair (SET) 1\n",
      "Sideboard\n2 cards\n",
      std::string("4 ", 2) + std::string(60000, 'X'),
  };
  for (const std::string &text : adversarial) {
    EXPECT_NO_FATAL_FAILURE({
      try {
        (void)core::parseArenaText(text);
      } catch (const core::DeckParseError &) {
        // Expected and documented: a text with zero parseable card lines.
        (void)0;
      }
    });
  }
}

} // namespace
} // namespace mtgcpp
