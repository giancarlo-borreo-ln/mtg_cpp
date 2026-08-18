// M2.4 card database tests: streamed JSONL load, printing + name indexes,
// malformed-record rejection and nullopt lookups. Mirrors the importer's needs
// (set/collector case-insensitivity, MDFC letter suffixes, name fallback).

#include "core/card_database.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace mtgcpp::core {
namespace {

using nlohmann::json;

// A minimal valid Scryfall-shaped record for a concrete printing.
json record(std::string name, std::string set, std::string number) {
  json r = {
      {"id", "id-" + set + "-" + number},
      {"name", std::move(name)},
      {"set", std::move(set)},
      {"set_name", "Test Set"},
      {"collector_number", std::move(number)},
      {"mana_cost", "{R}"},
      {"cmc", 1.0},
      {"colors", json::array({"R"})},
      {"type_line", "Creature — Goblin"},
      {"image_uris", json::object({{"png", "https://img/1.png"}})},
  };
  return r;
}

// A double-faced record exercising card_faces + a letter-suffixed number.
json richRecord() {
  const json frontFace = {{"name", "Akoum Warrior"},
                          {"image_uris", json::object({{"png", "https://img/f1.png"}})}};
  const json backFace = {{"name", "Akoum Teeth"},
                         {"image_uris", json::object({{"png", "https://img/f2.png"}})}};
  json r = {
      {"id", "id-rich"},
      {"name", "Akoum Warrior // Akoum Teeth"},
      {"set", "znr"},
      {"set_name", "Zendikar Rising"},
      {"collector_number", "51a"},
      {"mana_cost", "{3}{R}"},
      {"cmc", 4.0},
      {"colors", json::array({"R"})},
      {"type_line", "Creature — Human Warrior // Land"},
      {"image_uris", json::object({{"png", "https://img/a.png"}})},
      {"card_faces", json::array({frontFace, backFace})},
  };
  return r;
}

std::string lineOf(const json &r) { return r.dump(); }

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

// Look up a printing and fail the test on a miss.
Card mustFind(const CardDatabase &db, std::string_view set, std::string_view number) {
  const std::optional<Card> found = db.findByPrinting(set, number);
  if (found.has_value()) {
    return found.value();
  }
  ADD_FAILURE() << "expected printing " << set << "/" << number << " to be found";
  return Card{};
}

// Look up a name and fail the test on a miss.
Card mustFindName(const CardDatabase &db, std::string_view name) {
  const std::optional<Card> found = db.findByName(name);
  if (found.has_value()) {
    return found.value();
  }
  ADD_FAILURE() << "expected name " << name << " to be found";
  return Card{};
}

TEST(CardDatabase, LoadIndexesPrintingsAndReportsCounts) {
  std::istringstream in(lineOf(record("Forest", "war", "263")) + "\n" +
                        lineOf(record("Bolt", "war", "103")) + "\n" +
                        lineOf(record("Sol Ring", "lea", "265")) + "\n");

  CardDatabase db;
  const CardDatabase::LoadResult result = db.load(in);

  EXPECT_EQ(result.loaded, 3u);
  EXPECT_EQ(result.rejected, 0u);
  EXPECT_EQ(db.size(), 3u);
  EXPECT_FALSE(db.empty());
}

TEST(CardDatabase, FindByPrintingMatchesCaseInsensitively) {
  std::istringstream in(lineOf(record("Forest", "war", "263")) + "\n");

  CardDatabase db;
  db.load(in);

  const Card viaLower = mustFind(db, "war", "263");
  const Card viaUpper = mustFind(db, "WAR", "263");
  EXPECT_EQ(viaLower.name, "Forest");
  EXPECT_EQ(viaUpper.name, "Forest");
}

TEST(CardDatabase, FindByPrintingKeepsLetterSuffixedCollectorNumbers) {
  std::istringstream in(lineOf(record("Akoum Warrior // Akoum Teeth", "znr", "51a")) + "\n");

  CardDatabase db;
  db.load(in);

  EXPECT_EQ(mustFind(db, "znr", "51a").name, "Akoum Warrior // Akoum Teeth");
  EXPECT_EQ(mustFind(db, "ZNR", "51A").name, "Akoum Warrior // Akoum Teeth");
}

TEST(CardDatabase, FindByPrintingReturnsNulloptForAnUnknownPrinting) {
  CardDatabase db;
  EXPECT_FALSE(db.findByPrinting("zzz", "1").has_value());
}

TEST(CardDatabase, FindByNameMatchesCaseInsensitively) {
  std::istringstream in(lineOf(record("Sol Ring", "lea", "265")) + "\n");

  CardDatabase db;
  db.load(in);

  EXPECT_EQ(mustFindName(db, "sol ring").set_code, "lea");
  EXPECT_EQ(mustFindName(db, "SOL RING").collector_number, "265");
}

TEST(CardDatabase, FindByNameReturnsTheFirstLoadedPrinting) {
  std::istringstream in(lineOf(record("Forest", "aaa", "1")) + "\n" +
                        lineOf(record("Forest", "zzz", "2")) + "\n");

  CardDatabase db;
  db.load(in);

  EXPECT_EQ(mustFindName(db, "Forest").set_code, "aaa");
}

TEST(CardDatabase, FindByNameHandlesDoubleFacedNames) {
  std::istringstream in(lineOf(richRecord()) + "\n");

  CardDatabase db;
  db.load(in);

  EXPECT_EQ(mustFindName(db, "Akoum Warrior // Akoum Teeth").collector_number, "51a");
}

TEST(CardDatabase, FindByNameReturnsNulloptForAnUnknownName) {
  CardDatabase db;
  EXPECT_FALSE(db.findByName("No Such Card").has_value());
}

TEST(CardDatabase, LoadSkipsMalformedRecordsSafely) {
  std::istringstream in(lineOf(record("Forest", "war", "263")) + "\nnot json\n" +
                        lineOf(record("Bolt", "war", "103")) + "\n");

  CardDatabase db;
  const CardDatabase::LoadResult result = db.load(in);

  EXPECT_EQ(result.loaded, 2u);
  EXPECT_EQ(result.rejected, 1u);
  EXPECT_EQ(db.size(), 2u);
  EXPECT_EQ(mustFind(db, "war", "263").name, "Forest");
  EXPECT_EQ(mustFind(db, "war", "103").name, "Bolt");
}

TEST(CardDatabase, LoadRejectsWrongTypedRecords) {
  json bad = record("Not A Number", "war", "1");
  bad.erase("name");
  std::istringstream in(lineOf(bad) + "\n");

  CardDatabase db;
  const CardDatabase::LoadResult result = db.load(in);

  EXPECT_EQ(result.loaded, 0u);
  EXPECT_EQ(result.rejected, 1u);
  EXPECT_TRUE(db.empty());
}

TEST(CardDatabase, LoadIgnoresBlankLines) {
  std::istringstream in("\n" + lineOf(record("Forest", "war", "263")) + "\n\n" +
                        lineOf(record("Bolt", "war", "103")) + "\n");

  CardDatabase db;
  const CardDatabase::LoadResult result = db.load(in);

  EXPECT_EQ(result.loaded, 2u);
  EXPECT_EQ(result.rejected, 0u);
  EXPECT_EQ(db.size(), 2u);
}

TEST(CardDatabase, LoadHandlesCarriageReturnLineEndings) {
  std::istringstream in(lineOf(record("Forest", "war", "263")) + "\r\n" +
                        lineOf(record("Bolt", "war", "103")) + "\r\n");

  CardDatabase db;
  const CardDatabase::LoadResult result = db.load(in);

  EXPECT_EQ(result.loaded, 2u);
  EXPECT_EQ(result.rejected, 0u);
}

TEST(CardDatabase, DuplicatePrintingKeyKeepsTheLastLoadedRecord) {
  std::istringstream in(lineOf(record("First", "war", "263")) + "\n" +
                        lineOf(record("Second", "war", "263")) + "\n");

  CardDatabase db;
  const CardDatabase::LoadResult result = db.load(in);

  EXPECT_EQ(result.loaded, 2u);
  EXPECT_EQ(db.size(), 1u);
  EXPECT_EQ(mustFind(db, "war", "263").name, "Second");
}

TEST(CardDatabase, LoadHandlesAnEmptyStream) {
  std::istringstream in("");

  CardDatabase db;
  const CardDatabase::LoadResult result = db.load(in);

  EXPECT_EQ(result.loaded, 0u);
  EXPECT_EQ(result.rejected, 0u);
  EXPECT_TRUE(db.empty());
  EXPECT_EQ(db.size(), 0u);
}

TEST(CardDatabase, ToCardCopiesEveryConsumedField) {
  std::istringstream in(lineOf(richRecord()) + "\n");

  CardDatabase db;
  db.load(in);

  const Card card = mustFind(db, "ZNR", "51A");
  EXPECT_EQ(card.scryfall_id, "id-rich");
  EXPECT_EQ(card.name, "Akoum Warrior // Akoum Teeth");
  EXPECT_EQ(card.set_code, "znr");
  EXPECT_EQ(card.set_name, "Zendikar Rising");
  EXPECT_EQ(card.collector_number, "51a");
  EXPECT_EQ(card.mana_cost, "{3}{R}");
  EXPECT_EQ(card.cmc.value_or(-1.0f), 4.0f);
  EXPECT_EQ(card.colors, (std::vector<std::string>{"R"}));
  EXPECT_EQ(card.type_line, "Creature — Human Warrior // Land");
  EXPECT_EQ(card.image_uris.at("png"), "https://img/a.png");

  ASSERT_EQ(card.card_faces.size(), 2u);
  EXPECT_EQ(card.card_faces.at(0).name, "Akoum Warrior");
  EXPECT_EQ(card.card_faces.at(0).image_uris.at("png"), "https://img/f1.png");
  EXPECT_EQ(card.card_faces.at(1).name, "Akoum Teeth");
  EXPECT_EQ(card.card_faces.at(1).image_uris.at("png"), "https://img/f2.png");
}

TEST(CardDatabase, ToCardLeavesCmcEmptyWhenTheRecordHasNullCmc) {
  json noCmc = record("Forest", "war", "263");
  noCmc.erase("cmc");
  json nullCmc = withField(record("Bolt", "war", "103"), "cmc", nullptr);
  std::istringstream in(lineOf(noCmc) + "\n" + lineOf(nullCmc) + "\n");

  CardDatabase db;
  db.load(in);

  EXPECT_FALSE(mustFind(db, "war", "263").cmc.has_value());
  EXPECT_FALSE(mustFind(db, "war", "103").cmc.has_value());
}

// ---------------------------------------------------------------------------
// CardDatabase::search (M5.1): the local Scryfall-search equivalent.
// ---------------------------------------------------------------------------

// A database with a small, name-overlapping fixture so search ordering and
// filtering are observable: three distinct names across two sets.
CardDatabase searchFixture() {
  std::istringstream in(lineOf(record("Lightning Bolt", "sta", "49")) + "\n" +
                        lineOf(record("Bolt Hound", "m20", "136")) + "\n" +
                        lineOf(record("Ghalta, Primal Hunger", "rna", "176")) + "\n" +
                        lineOf(record("Forest", "war", "263")) + "\n" +
                        lineOf(record("Forest", "m20", "276")) + "\n" +
                        lineOf(record("Sol Ring", "lea", "265")) + "\n");
  CardDatabase db;
  db.load(in);
  return db;
}

TEST(CardDatabase, SearchMatchesANameSubstringCaseInsensitively) {
  const CardDatabase db = searchFixture();

  // "bolt" matches two distinct names; one representative printing each.
  const std::vector<Card> bolts = db.search("bolt");
  ASSERT_EQ(bolts.size(), 2u);
  EXPECT_EQ(bolts.at(0).name, "Bolt Hound");
  EXPECT_EQ(bolts.at(1).name, "Lightning Bolt");

  const std::vector<Card> upper = db.search("BOLT");
  ASSERT_EQ(upper.size(), 2u);
  EXPECT_EQ(upper.at(0).name, "Bolt Hound");
}

TEST(CardDatabase, SearchResultsAreOrderedByName) {
  const CardDatabase db = searchFixture();

  const std::vector<Card> forest = db.search("forest");
  ASSERT_EQ(forest.size(), 1u); // distinct names: two printings collapse
  EXPECT_EQ(forest.at(0).name, "Forest");
  EXPECT_EQ(forest.at(0).set_code, "war"); // first loaded wins as representative
}

TEST(CardDatabase, SearchRequiresEveryNameToken) {
  const CardDatabase db = searchFixture();

  // Two tokens must BOTH appear in the name — nothing is named "lightning ghalta".
  EXPECT_TRUE(db.search("lightning ghalta").empty());
  // A token that is a substring of more than one word still works.
  const std::vector<Card> ring = db.search("sol ring");
  ASSERT_EQ(ring.size(), 1u);
  EXPECT_EQ(ring.at(0).name, "Sol Ring");
}

TEST(CardDatabase, SearchResolvesAParenthesizedSetPlusNumberToAnExactPrinting) {
  const CardDatabase db = searchFixture();

  const std::vector<Card> exact = db.search("(WAR) 263");
  ASSERT_EQ(exact.size(), 1u);
  EXPECT_EQ(exact.at(0).name, "Forest");
  EXPECT_EQ(exact.at(0).set_code, "war");

  // Case-insensitive set code; letter-suffixed numbers work too.
  const std::vector<Card> lower = db.search("(war) 263");
  ASSERT_EQ(lower.size(), 1u);
  EXPECT_EQ(lower.at(0).name, "Forest");
}

TEST(CardDatabase, SearchReturnsNothingForAnUnknownExactPrinting) {
  const CardDatabase db = searchFixture();
  EXPECT_TRUE(db.search("(ZZZ) 999").empty());
}

TEST(CardDatabase, SearchWithASetCodeButNoNumberFiltersByName) {
  const CardDatabase db = searchFixture();

  // Forest is the only name printed in war here; "(WAR) forest" still matches
  // it, and "(WAR) bolt" matches nothing (no bolt in war in the fixture).
  const std::vector<Card> warForest = db.search("(WAR) forest");
  ASSERT_EQ(warForest.size(), 1u);
  EXPECT_EQ(warForest.at(0).name, "Forest");

  EXPECT_TRUE(db.search("(WAR) bolt").empty());
}

TEST(CardDatabase, SearchCapsResultsAtTheLimit) {
  // Five printings, one per distinct name, each containing the letter "a".
  CardDatabase db;
  std::istringstream in(
      lineOf(record("Alpha", "aaa", "1")) + "\n" + lineOf(record("Beta", "aaa", "2")) + "\n" +
      lineOf(record("Gamma", "aaa", "3")) + "\n" + lineOf(record("Delta", "aaa", "4")) + "\n" +
      lineOf(record("Apex", "aaa", "5")) + "\n");
  db.load(in);

  const std::vector<Card> all = db.search("");
  EXPECT_TRUE(all.empty()); // a blank query matches nothing

  // "a" is a substring of every name; cap at two results.
  const std::vector<Card> capped = db.search("a", 2);
  ASSERT_EQ(capped.size(), 2u);
}

TEST(CardDatabase, SearchOfAnEmptyDatabaseReturnsNothing) {
  CardDatabase db;
  EXPECT_TRUE(db.search("anything").empty());
}

} // namespace
} // namespace mtgcpp::core
