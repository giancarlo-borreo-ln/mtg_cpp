// Streamed validation of the Scryfall bulk JSONL (M2.3 CLI `mtg_cpp check-cards`).
//
// The bulk artifact is ~600 MB of one-JSON-object-per-line data. Everything
// here streams: each line is validated in isolation via the shared record
// grammar (card_record.h) and the results are just counters, so peak memory
// stays flat no matter the file size.

#include "core/check_cards.h"
#include "core/card_record.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace mtgcpp::core {

namespace {

constexpr std::size_t kMaxSampleLength = 200;

// Clip a (possibly multi-KB) JSONL line for reporting.
std::string truncated(const std::string &line) {
  if (line.size() <= kMaxSampleLength) {
    return line;
  }
  return line.substr(0, kMaxSampleLength) + "...";
}

} // namespace

CardIntegritySummary streamValidateCards(std::istream &stream, std::size_t maxSamples) {
  CardIntegritySummary summary;
  summary.malformed_samples.reserve(maxSamples);

  std::string line;
  while (std::getline(stream, line)) {
    // Strip a single trailing carriage return so CRLF files read cleanly.
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      summary.blank_lines += 1;
      continue;
    }
    summary.records_read += 1;

    std::string reason;
    const std::optional<nlohmann::json> record = parseCardRecord(line, reason);
    if (record.has_value()) {
      summary.records_valid += 1;
    } else {
      summary.records_malformed += 1;
      if (summary.malformed_samples.size() < maxSamples) {
        summary.malformed_samples.push_back({truncated(line), reason});
      }
    }
  }
  return summary;
}

std::optional<CardManifestInfo> readCardManifest(const std::filesystem::path &path) {
  std::ifstream in(path);
  if (!in) {
    return std::nullopt;
  }
  nlohmann::json doc;
  try {
    in >> doc;
  } catch (const nlohmann::json::parse_error &) {
    return std::nullopt;
  }
  if (!doc.is_object()) {
    return std::nullopt;
  }

  CardManifestInfo info;
  const auto readString = [&doc](const char *name, std::string &out) {
    if (doc.contains(name) && doc.at(name).is_string()) {
      out = doc.at(name).get<std::string>();
    }
  };
  const auto readBytes = [&doc](const char *name, std::uint64_t &out) {
    if (doc.contains(name) && doc.at(name).is_number_unsigned()) {
      out = doc.at(name).get<std::uint64_t>();
    }
  };
  readString("downloaded_at", info.downloaded_at);
  readString("scryfall_updated_at", info.scryfall_updated_at);
  readBytes("jsonl_bytes", info.jsonl_bytes);
  readBytes("gz_bytes", info.gz_bytes);
  return info;
}

std::string formatCheckCardsReport(const CardIntegritySummary &summary,
                                   const std::optional<CardManifestInfo> &manifest) {
  std::ostringstream out;
  out << "mtg_cpp check-cards\n";
  out << "records read:    " << summary.records_read << '\n';
  out << "valid:           " << summary.records_valid << '\n';
  out << "malformed:       " << summary.records_malformed << '\n';
  out << "blank lines:     " << summary.blank_lines << '\n';
  if (!summary.malformed_samples.empty()) {
    out << "malformed samples:\n";
    for (const MalformedCardLine &sample : summary.malformed_samples) {
      out << "  " << sample.text << '\n';
      out << "  reason: " << sample.reason << '\n';
    }
  }
  if (manifest.has_value()) {
    const CardManifestInfo &info = manifest.value();
    out << "manifest:\n";
    out << "  downloaded at:    " << info.downloaded_at << '\n';
    out << "  scryfall updated: " << info.scryfall_updated_at << '\n';
    out << "  jsonl bytes:      " << info.jsonl_bytes << '\n';
    out << "  gz bytes:         " << info.gz_bytes << '\n';
  } else {
    out << "manifest: (missing or unreadable)\n";
  }
  return out.str();
}

int runCheckCards(const std::filesystem::path &jsonlPath,
                  const std::filesystem::path &manifestPath) {
  std::ifstream in(jsonlPath, std::ios::binary);
  if (!in) {
    std::cerr << "check-cards: cannot open " << jsonlPath << '\n';
    return 1;
  }
  const CardIntegritySummary summary = streamValidateCards(in);
  const std::optional<CardManifestInfo> manifest = readCardManifest(manifestPath);
  std::cout << formatCheckCardsReport(summary, manifest);
  return 0;
}

} // namespace mtgcpp::core
