// Streamed sanity check for the downloaded Scryfall bulk JSONL (M2.3).
//
// `check-cards` is the cheap gate between M0.3's fetch and M2.4's loader: it
// streams data/default-cards.jsonl line by line, validates the typed fields the
// loader will consume, and reports counts plus the provenance manifest. A
// malformed line is reported, never fatal — the whole file is still walked, so
// one bad record cannot hide the others in a ~600 MB artifact.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace mtgcpp::core {

// A rejected record: the offending line (truncated) and why it was rejected.
struct MalformedCardLine {
  std::string text;
  std::string reason;

  bool operator==(const MalformedCardLine &) const = default;
};

// Running totals from one pass over the bulk JSONL stream.
struct CardIntegritySummary {
  std::uint64_t records_read = 0;
  std::uint64_t records_valid = 0;
  std::uint64_t records_malformed = 0;
  std::uint64_t blank_lines = 0;
  std::vector<MalformedCardLine> malformed_samples;

  bool operator==(const CardIntegritySummary &) const = default;
};

// Provenance fields the fetch script writes to data/manifest.json (M0.3).
struct CardManifestInfo {
  std::string downloaded_at;
  std::string scryfall_updated_at;
  std::uint64_t jsonl_bytes = 0;
  std::uint64_t gz_bytes = 0;

  bool operator==(const CardManifestInfo &) const = default;
};

// Stream the bulk JSONL, validating every record's typed fields. At most
// `maxSamples` malformed lines are retained (truncated) so a corrupt file is
// diagnosable without buffering the whole artifact in memory.
CardIntegritySummary streamValidateCards(std::istream &stream, std::size_t maxSamples = 5);

// Read the provenance manifest. Returns nullopt when the file is missing or not
// the expected JSON shape.
std::optional<CardManifestInfo> readCardManifest(const std::filesystem::path &path);

// Human-readable report text printed by `mtg_cpp check-cards`.
std::string formatCheckCardsReport(const CardIntegritySummary &summary,
                                   const std::optional<CardManifestInfo> &manifest);

// The `mtg_cpp check-cards` CLI: stream the JSONL, print counts + manifest.
// Returns 0 when the file was walked (malformed lines are reported, not fatal);
// 1 when the JSONL cannot be opened.
int runCheckCards(const std::filesystem::path &jsonlPath,
                  const std::filesystem::path &manifestPath);

} // namespace mtgcpp::core
