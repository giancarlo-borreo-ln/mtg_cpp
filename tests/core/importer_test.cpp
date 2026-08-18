// M2.5 importer tests: resolve Arena export text against a local CardDatabase
// fixture. Mirrors backend/tests/test_deck_importer.py, with Scryfall's HTTP
// collection/named lookups replaced by the offline printing + name indexes.

#include "core/card_database.h"
#include "core/deck_parser.h"
#include "core/importer.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace mtgcpp::core {
namespace {

using nlohmann::json;

// A Scryfall-shaped record for a concrete printing, matching the Python test
// fixtures' field shapes.
json makeRecord(const std::string &name, const std::string &set, const std::string &number,
                double cmc, const std::vector<std::string> &colors, const std::string &manaCost,
                const std::string &typeLine) {
  json r = {
      {"id", "id-" + set + "-" + number},
      {"name", name},
      {"set", set},
      {"set_name", "Test Set"},
      {"collector_number", number},
      {"mana_cost", manaCost},
      {"cmc", cmc},
      {"colors", json(colors)},
      {"type_line", typeLine},
      {"image_uris", json::object({{"png", "https://img/" + name + ".png"}})},
  };
  return r;
}

// A double-faced record with card_faces and no top-level art.
json dfcRecord() {
  const json frontFace = {{"name", "Akoum Warrior"},
                          {"image_uris", json::object({{"png", "https://img/front.png"}})}};
  const json backFace = {{"name", "Akoum Teeth"},
                         {"image_uris", json::object({{"png", "https://img/back.png"}})}};
  json r = {
      {"id", "id-dfc"},
      {"name", "Akoum Warrior // Akoum Teeth"},
      {"set", "znr"},
      {"set_name", "Zendikar Rising"},
      {"collector_number", "134"},
      {"mana_cost", "{4}{R}{R}"},
      {"cmc", 6.0},
      {"colors", json::array({"R"})},
      {"type_line", "Creature — Minotaur"},
      {"card_faces", json::array({frontFace, backFace})},
  };
  return r;
}

std::string lineOf(const json &r) { return r.dump(); }

// The synthetic local database the imports resolve against.
CardDatabase fixture() {
  const std::string forestType = "Basic Land — Forest";
  std::istringstream in(
      lineOf(makeRecord("Forest", "war", "263", 0.0, {}, "", forestType)) + "\n" +
      lineOf(
          makeRecord("Llanowar Elves", "m19", "314", 1.0, {"G"}, "{G}", "Creature — Elf Druid")) +
      "\n" + lineOf(makeRecord("Abrade", "akr", "136", 2.0, {"R"}, "{1}{R}", "Instant")) + "\n" +
      lineOf(makeRecord("Ghalta, Primal Hunger", "rix", "130", 12.0, {"G"}, "{10}{G}{G}",
                        "Legendary Creature — Elder Dinosaur")) +
      "\n" + lineOf(dfcRecord()) + "\n" +
      lineOf(makeRecord("Clockwork Percussionist", "dsk", "137", 2.0, {"R"}, "{1}{R}",
                        "Creature — Human Artificer")) +
      "\n" + lineOf(makeRecord("Mountain", "mkm", "273", 0.0, {}, "", "Basic Land — Mountain")) +
      "\n");
  CardDatabase db;
  db.load(in);
  return db;
}

TEST(ImportDeck, ResolvesPrintingsAndCopiesCardFields) {
  const DeckParseResult result =
      importDeck("4 Forest (WAR) 263\n3 Llanowar Elves (M19) 314\n", fixture());

  EXPECT_EQ(result.total_cards, 7);
  EXPECT_EQ(result.unique_cards, 2);
  EXPECT_TRUE(result.not_found.empty());

  const Card &forest = result.cards.at(0);
  EXPECT_EQ(forest.name, "Forest");
  EXPECT_EQ(forest.quantity, 4);
  EXPECT_EQ(forest.set_code, "war");
  EXPECT_EQ(forest.set_name, "Test Set");
  EXPECT_EQ(forest.collector_number, "263");
  EXPECT_EQ(forest.type_line, "Basic Land — Forest");
  EXPECT_EQ(forest.cmc.value_or(-1.0f), 0.0f);
  EXPECT_EQ(forest.image_uris.at("png"), "https://img/Forest.png");

  const Card &elves = result.cards.at(1);
  EXPECT_EQ(elves.quantity, 3);
  EXPECT_EQ(elves.colors, (std::vector<std::string>{"G"}));
}

TEST(ImportDeck, SumsDuplicateLines) {
  const DeckParseResult result = importDeck("2 Forest (WAR) 263\n3 Forest (WAR) 263\n", fixture());

  ASSERT_EQ(result.cards.size(), 1u);
  EXPECT_EQ(result.cards.at(0).quantity, 5);
  EXPECT_EQ(result.total_cards, 5);
  EXPECT_EQ(result.unique_cards, 1);
}

TEST(ImportDeck, ExtractsCardFacesForDoubleFacedCards) {
  const DeckParseResult result = importDeck("1 Akoum Warrior (ZNR) 134\n", fixture());

  ASSERT_EQ(result.cards.size(), 1u);
  const Card &card = result.cards.at(0);
  EXPECT_EQ(card.name, "Akoum Warrior // Akoum Teeth");
  ASSERT_EQ(card.card_faces.size(), 2u);
  EXPECT_EQ(card.card_faces.at(0).name, "Akoum Warrior");
  EXPECT_EQ(card.card_faces.at(0).image_uris.at("png"), "https://img/front.png");
  EXPECT_EQ(card.card_faces.at(1).name, "Akoum Teeth");
  EXPECT_EQ(card.card_faces.at(1).image_uris.at("png"), "https://img/back.png");
  EXPECT_EQ(card.image_uris.at("png"), "https://img/front.png");
}

TEST(ImportDeck, FallsBackToNameLookupForUnmatchedPrinting) {
  // The deck names printing (znr, 222a), which the database lacks; the MDFC
  // still resolves by its full exported name to the (znr, 134) printing.
  const DeckParseResult result =
      importDeck("1 Akoum Warrior // Akoum Teeth (ZNR) 222a\n", fixture());

  ASSERT_EQ(result.cards.size(), 1u);
  EXPECT_EQ(result.cards.at(0).name, "Akoum Warrior // Akoum Teeth");
  EXPECT_EQ(result.cards.at(0).quantity, 1);
  EXPECT_EQ(result.cards.at(0).collector_number, "134");
  EXPECT_EQ(result.cards.at(0).card_faces.size(), 2u);
  EXPECT_TRUE(result.not_found.empty());
}

TEST(ImportDeck, ReportsMissingCards) {
  const DeckParseResult result = importDeck("2 Mystery Card (ZZZ) 1\n", fixture());

  EXPECT_TRUE(result.cards.empty());
  EXPECT_EQ(result.total_cards, 2);
  EXPECT_EQ(result.unique_cards, 0);
  ASSERT_EQ(result.not_found.size(), 1u);
  const MissingCard &missing = result.not_found.at(0);
  EXPECT_EQ(missing.name, "Mystery Card");
  EXPECT_EQ(missing.set, "ZZZ");
  EXPECT_EQ(missing.number, "1");
  EXPECT_EQ(missing.quantity, 2);
}

TEST(ImportDeck, ReportsPartialNotFound) {
  const DeckParseResult result =
      importDeck("1 Forest (WAR) 263\n1 Mystery Card (ZZZ) 1\n", fixture());

  ASSERT_EQ(result.cards.size(), 1u);
  EXPECT_EQ(result.cards.at(0).name, "Forest");
  ASSERT_EQ(result.not_found.size(), 1u);
  EXPECT_EQ(result.not_found.at(0).name, "Mystery Card");
}

TEST(ImportDeck, ThrowsDeckParseErrorForNonDeckText) {
  EXPECT_THROW(importDeck("this is not a deck at all\n", fixture()), DeckParseError);
}

TEST(ImportDeck, PreservesSectionMembership) {
  const std::string text = "Deck\n4 Forest (WAR) 263\n3 Llanowar Elves (M19) 314\n"
                           "Sideboard\n2 Abrade (AKR) 136\n";
  const DeckParseResult result = importDeck(text, fixture());

  EXPECT_EQ(result.total_cards, 9);
  EXPECT_EQ(result.unique_cards, 3);
  EXPECT_TRUE(result.not_found.empty());

  std::vector<const Card *> mainboard;
  std::vector<const Card *> sideboard;
  for (const Card &card : result.cards) {
    if (card.section == ArenaSection::Mainboard) {
      mainboard.push_back(&card);
    } else {
      sideboard.push_back(&card);
    }
  }
  ASSERT_EQ(mainboard.size(), 2u);
  EXPECT_EQ(mainboard.at(0)->name, "Forest");
  EXPECT_EQ(mainboard.at(0)->quantity, 4);
  EXPECT_EQ(mainboard.at(1)->name, "Llanowar Elves");
  EXPECT_EQ(mainboard.at(1)->quantity, 3);
  ASSERT_EQ(sideboard.size(), 1u);
  EXPECT_EQ(sideboard.at(0)->name, "Abrade");
  EXPECT_EQ(sideboard.at(0)->quantity, 2);
}

TEST(ImportDeck, KeepsSamePrintingInMainAndSideSeparate) {
  const std::string text = "Deck\n2 Forest (WAR) 263\nSideboard\n1 Forest (WAR) 263\n";
  const DeckParseResult result = importDeck(text, fixture());

  ASSERT_EQ(result.cards.size(), 2u);
  EXPECT_EQ(result.cards.at(0).section, ArenaSection::Mainboard);
  EXPECT_EQ(result.cards.at(0).quantity, 2);
  EXPECT_EQ(result.cards.at(1).section, ArenaSection::Sideboard);
  EXPECT_EQ(result.cards.at(1).quantity, 1);
  EXPECT_EQ(result.total_cards, 3);
  EXPECT_EQ(result.unique_cards, 1);
}

TEST(ImportDeck, ResolvesCommanderSection) {
  const std::string text = "Deck\n1 Ghalta, Primal Hunger (RIX) 130\n99 Forest (WAR) 263\n"
                           "Commander\n1 Ghalta, Primal Hunger (RIX) 130\n";
  const DeckParseResult result = importDeck(text, fixture());

  std::vector<const Card *> commander;
  for (const Card &card : result.cards) {
    if (card.section == ArenaSection::Commander) {
      commander.push_back(&card);
    }
  }
  ASSERT_EQ(commander.size(), 1u);
  EXPECT_EQ(commander.at(0)->name, "Ghalta, Primal Hunger");
  EXPECT_EQ(result.total_cards, 101);
  EXPECT_EQ(result.unique_cards, 2);
}

TEST(ImportDeck, ReportsMissingCardSection) {
  const std::string text = "Deck\n1 Mystery A (ZZZ) 1\nSideboard\n2 Mystery B (ZZZ) 2\n";
  const DeckParseResult result = importDeck(text, fixture());

  EXPECT_TRUE(result.cards.empty());
  ASSERT_EQ(result.not_found.size(), 2u);
  EXPECT_EQ(result.not_found.at(0).name, "Mystery A");
  EXPECT_EQ(result.not_found.at(0).section, ArenaSection::Mainboard);
  EXPECT_EQ(result.not_found.at(1).name, "Mystery B");
  EXPECT_EQ(result.not_found.at(1).section, ArenaSection::Sideboard);
  EXPECT_EQ(result.total_cards, 3);
}

TEST(ImportDeck, ResolvesNameOnlyLinesByName) {
  const std::string text = "Deck\n4 Clockwork Percussionist\n2 Mountain\nSideboard\n1 Negate\n";
  const DeckParseResult result = importDeck(text, fixture());

  EXPECT_EQ(result.total_cards, 7);
  ASSERT_EQ(result.cards.size(), 2u);
  EXPECT_EQ(result.cards.at(0).name, "Clockwork Percussionist");
  EXPECT_EQ(result.cards.at(0).quantity, 4);
  EXPECT_EQ(result.cards.at(0).section, ArenaSection::Mainboard);
  EXPECT_EQ(result.cards.at(1).name, "Mountain");
  EXPECT_EQ(result.cards.at(1).quantity, 2);

  ASSERT_EQ(result.not_found.size(), 1u);
  EXPECT_EQ(result.not_found.at(0).name, "Negate");
  EXPECT_TRUE(result.not_found.at(0).set.empty());
  EXPECT_TRUE(result.not_found.at(0).number.empty());
  EXPECT_EQ(result.not_found.at(0).section, ArenaSection::Sideboard);
}

TEST(ImportDeck, ReportsMissingNameOnlyCardWithEmptyPrinting) {
  const DeckParseResult result = importDeck("Deck\n2 Totally Fake Card\n", fixture());

  EXPECT_TRUE(result.cards.empty());
  ASSERT_EQ(result.not_found.size(), 1u);
  EXPECT_EQ(result.not_found.at(0).name, "Totally Fake Card");
  EXPECT_EQ(result.not_found.at(0).quantity, 2);
  EXPECT_TRUE(result.not_found.at(0).set.empty());
  EXPECT_TRUE(result.not_found.at(0).number.empty());
}

} // namespace
} // namespace mtgcpp::core
