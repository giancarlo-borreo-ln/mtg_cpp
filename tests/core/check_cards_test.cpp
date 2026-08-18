// M2.3 check-cards tests: the streamed JSONL validator + manifest reader.
//
// The real bulk artifact (~600 MB) is validated by running the CLI; these tests
// exercise the same code paths against small synthetic fixtures so malformed
// records, blank lines and manifest shapes are deterministic.

#include "core/check_cards.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace mtgcpp::core {
namespace {

using nlohmann::json;

// A Scryfall-shaped record carrying the identity + typed fields the loader
// consumes.
json validRecord(std::string name = "Forest", std::string set = "war") {
  json record = {
      {"id", "abc-123"},
      {"name", std::move(name)},
      {"set", std::move(set)},
      {"set_name", "War of the Spark"},
      {"collector_number", "263"},
      {"mana_cost", "{G}"},
      {"cmc", 1.0},
      {"colors", json::array({"G"})},
      {"type_line", "Basic Land — Forest"},
      {"image_uris", json::object({{"png", "https://cards.example/1.png"}})},
  };
  return record;
}

std::string lineOf(const json &record) { return record.dump(); }

// Replace-or-insert `key` into a copy of `record`. nlohmann `operator[]` would
// trip the repo's bounds-check lint; find/emplace is the safe object accessor.
json withField(json record, const char *key, json value) {
  auto entry = record.find(key);
  if (entry != record.end()) {
    *entry = std::move(value);
  } else {
    record.emplace(key, std::move(value));
  }
  return record;
}

// Writes `content` to a temp file and removes it on destruction. Non-copyable
// (and non-movable) so the destructor's cleanup can never run twice.
class TempFile {
public:
  explicit TempFile(std::string_view content) {
    std::ofstream out(path_);
    out << content;
  }
  ~TempFile() { std::filesystem::remove(path_); }

  TempFile(const TempFile &) = delete;
  TempFile &operator=(const TempFile &) = delete;
  TempFile(TempFile &&) = delete;
  TempFile &operator=(TempFile &&) = delete;

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_ =
      std::filesystem::temp_directory_path() / "mtgcpp_check_cards_test.json";
};

TEST(StreamValidateCards, CountsValidRecords) {
  std::istringstream in(lineOf(validRecord()) + "\n" + lineOf(validRecord("Bolt")) + "\n");

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_read, 2u);
  EXPECT_EQ(summary.records_valid, 2u);
  EXPECT_EQ(summary.records_malformed, 0u);
  EXPECT_EQ(summary.blank_lines, 0u);
  EXPECT_TRUE(summary.malformed_samples.empty());
}

TEST(StreamValidateCards, ReportsMalformedLineWithoutAborting) {
  std::istringstream in(lineOf(validRecord()) + "\nthis is not json\n" +
                        lineOf(validRecord("Bolt")));

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_read, 3u);
  EXPECT_EQ(summary.records_valid, 2u);
  EXPECT_EQ(summary.records_malformed, 1u);
  ASSERT_EQ(summary.malformed_samples.size(), 1u);
  EXPECT_EQ(summary.malformed_samples.at(0).text, "this is not json");
  EXPECT_EQ(summary.malformed_samples.at(0).reason, "line is not valid JSON");
}

TEST(StreamValidateCards, RejectsANonObjectRecord) {
  std::istringstream in("[1, 2]");

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_valid, 0u);
  EXPECT_EQ(summary.records_malformed, 1u);
  EXPECT_EQ(summary.malformed_samples.at(0).reason, "record is not a JSON object");
}

TEST(StreamValidateCards, RejectsAWrongTypedNameField) {
  std::istringstream in(lineOf(withField(validRecord(), "name", 42)));

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_malformed, 1u);
  EXPECT_EQ(summary.malformed_samples.at(0).reason, "name is not a string");
}

TEST(StreamValidateCards, RejectsAMissingRequiredField) {
  json record = validRecord();
  record.erase("collector_number");
  std::istringstream in(lineOf(record));

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_malformed, 1u);
  EXPECT_EQ(summary.malformed_samples.at(0).reason, "collector_number is missing");
}

TEST(StreamValidateCards, RejectsAWrongTypedCmc) {
  std::istringstream in(lineOf(withField(validRecord(), "cmc", "one")));

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_malformed, 1u);
  EXPECT_EQ(summary.malformed_samples.at(0).reason, "cmc is not a number or null");
}

TEST(StreamValidateCards, AcceptsANullCmc) {
  std::istringstream in(lineOf(withField(validRecord(), "cmc", nullptr)));

  EXPECT_EQ(streamValidateCards(in).records_valid, 1u);
}

TEST(StreamValidateCards, RejectsAStringColorsField) {
  std::istringstream in(lineOf(withField(validRecord(), "colors", "G")));

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_malformed, 1u);
  EXPECT_EQ(summary.malformed_samples.at(0).reason, "colors is not an array");
}

TEST(StreamValidateCards, RejectsNonStringColorsEntries) {
  std::istringstream in(lineOf(withField(validRecord(), "colors", json::array({"G", 1}))));

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_malformed, 1u);
  EXPECT_EQ(summary.malformed_samples.at(0).reason, "colors contains a non-string");
}

TEST(StreamValidateCards, RejectsNonStringImageUriValue) {
  std::istringstream in(
      lineOf(withField(validRecord(), "image_uris", json::object({{"png", 42}}))));

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_malformed, 1u);
  EXPECT_EQ(summary.malformed_samples.at(0).reason, "image_uris contains a non-string");
}

TEST(StreamValidateCards, RejectsANonObjectCardFace) {
  json record = validRecord();
  record.erase("image_uris");
  std::istringstream in(lineOf(withField(record, "card_faces", json::array({"not an object"}))));

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_malformed, 1u);
  EXPECT_EQ(summary.malformed_samples.at(0).reason, "card_faces entry is not an object");
}

TEST(StreamValidateCards, AcceptsRecordsMissingOptionalFields) {
  json record = {{"id", "x"}, {"name", "Sol Ring"}, {"set", "lea"}, {"collector_number", "1"}};
  std::istringstream in(lineOf(record));

  EXPECT_EQ(streamValidateCards(in).records_valid, 1u);
}

TEST(StreamValidateCards, CountsBlankLinesWithoutCountingThemAsRecords) {
  std::istringstream in("\n" + lineOf(validRecord()) + "\n\n" + lineOf(validRecord("Bolt")) + "\n");

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.blank_lines, 2u);
  EXPECT_EQ(summary.records_read, 2u);
  EXPECT_EQ(summary.records_valid, 2u);
}

TEST(StreamValidateCards, HandlesCarriageReturnLineEndings) {
  std::istringstream in(lineOf(validRecord()) + "\r\n" + lineOf(validRecord("Bolt")) + "\r\n");

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.blank_lines, 0u);
  EXPECT_EQ(summary.records_read, 2u);
  EXPECT_EQ(summary.records_valid, 2u);
}

TEST(StreamValidateCards, CapsTheRetainedMalformedSamples) {
  std::istringstream in("bad\nbad\nbad\nbad\nbad\nbad\n");

  const CardIntegritySummary summary = streamValidateCards(in, 3);

  EXPECT_EQ(summary.records_read, 6u);
  EXPECT_EQ(summary.records_malformed, 6u);
  EXPECT_EQ(summary.malformed_samples.size(), 3u);
}

TEST(StreamValidateCards, HandlesAnEmptyStream) {
  std::istringstream in("");

  const CardIntegritySummary summary = streamValidateCards(in);

  EXPECT_EQ(summary.records_read, 0u);
  EXPECT_EQ(summary.records_valid, 0u);
  EXPECT_EQ(summary.records_malformed, 0u);
  EXPECT_TRUE(summary.malformed_samples.empty());
}

TEST(ReadCardManifest, ReadsTheProvenanceFields) {
  json doc = {{"downloaded_at", "2026-08-18T10:03:11Z"},
              {"scryfall_updated_at", "2026-08-18T09:05:22.474+00:00"},
              {"jsonl_bytes", 623955189u},
              {"gz_bytes", 77517951u}};
  TempFile file(doc.dump());

  const std::optional<CardManifestInfo> result = readCardManifest(file.path());

  if (result.has_value()) {
    const CardManifestInfo &info = result.value();
    EXPECT_EQ(info.downloaded_at, "2026-08-18T10:03:11Z");
    EXPECT_EQ(info.scryfall_updated_at, "2026-08-18T09:05:22.474+00:00");
    EXPECT_EQ(info.jsonl_bytes, 623955189u);
    EXPECT_EQ(info.gz_bytes, 77517951u);
  } else {
    FAIL() << "expected the manifest to be readable";
  }
}

TEST(ReadCardManifest, ReturnsNulloptForAMissingFile) {
  const std::filesystem::path missing =
      std::filesystem::temp_directory_path() / "mtgcpp_definitely_missing_manifest.json";
  std::filesystem::remove(missing);

  EXPECT_FALSE(readCardManifest(missing).has_value());
}

TEST(ReadCardManifest, ReturnsNulloptForInvalidJson) {
  TempFile file("not a manifest");

  EXPECT_FALSE(readCardManifest(file.path()).has_value());
}

TEST(FormatCheckCardsReport, IncludesCountsAndManifest) {
  CardIntegritySummary summary;
  summary.records_read = 10;
  summary.records_valid = 9;
  summary.records_malformed = 1;
  summary.malformed_samples.push_back({"junk line", "line is not valid JSON"});
  const CardManifestInfo manifest{.downloaded_at = "2026-08-18T10:03:11Z",
                                  .scryfall_updated_at = "2026-08-18T09:05:22.474+00:00",
                                  .jsonl_bytes = 623955189,
                                  .gz_bytes = 77517951};

  const std::string report = formatCheckCardsReport(summary, manifest);

  EXPECT_NE(report.find("records read:    10"), std::string::npos);
  EXPECT_NE(report.find("valid:           9"), std::string::npos);
  EXPECT_NE(report.find("malformed:       1"), std::string::npos);
  EXPECT_NE(report.find("junk line"), std::string::npos);
  EXPECT_NE(report.find("reason: line is not valid JSON"), std::string::npos);
  EXPECT_NE(report.find("downloaded at:    2026-08-18T10:03:11Z"), std::string::npos);
  EXPECT_NE(report.find("jsonl bytes:      623955189"), std::string::npos);
}

TEST(FormatCheckCardsReport, NotesAMissingManifest) {
  const std::string report = formatCheckCardsReport(CardIntegritySummary{}, std::nullopt);

  EXPECT_NE(report.find("manifest: (missing or unreadable)"), std::string::npos);
}

} // namespace
} // namespace mtgcpp::core
