// M5.2 import-preview tests: the preview state machine (select / replace /
// remove / dismiss / confirm) ported from the webapp's deck.reducer.spec.ts
// "import preview" describe block, using the same key format and semantics.

#include "core/arena.h"
#include "core/card.h"
#include "core/card_database.h"
#include "core/import_preview.h"
#include "core/importer.h"

#include <gtest/gtest.h>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mtgcpp::core {
namespace {

// Card factory mirroring the reducer spec's `card(...)` helper.
Card card(std::string name, const std::string &set = "sta", const std::string &number = "1",
          int quantity = 1, ArenaSection section = ArenaSection::Mainboard) {
  Card c;
  c.name = std::move(name);
  c.set_code = set;
  c.set_name = "Test Set";
  c.collector_number = number;
  c.quantity = quantity;
  c.section = section;
  c.type_line = "Instant";
  return c;
}

// MissingCard factory mirroring the spec's `missing(...)` helper.
MissingCard missing(std::string name, const std::string &set, const std::string &number,
                    int quantity = 1, ArenaSection section = ArenaSection::Mainboard) {
  return {std::move(name), set, number, quantity, section};
}

// The reducer spec's `withPreview()` fixture: one resolved card plus two
// flagged entries, mainboard Mystery A and sideboard Mystery B.
ImportPreview withPreview() {
  DeckParseResult result;
  result.cards = {card("Forest", "war", "263", 4)};
  result.not_found = {missing("Mystery A", "ZZZ", "1", 2),
                      missing("Mystery B", "YYY", "2", 1, ArenaSection::Sideboard)};
  return makeImportPreview(result);
}

TEST(ImportPreview, MakePreviewMirrorsTheImportResult) {
  const ImportPreview preview = withPreview();
  ASSERT_EQ(preview.cards.size(), 1u);
  EXPECT_EQ(preview.cards.at(0).name, "Forest");
  ASSERT_EQ(preview.missing.size(), 2u);
  EXPECT_EQ(preview.missing.at(0).name, "Mystery A");
  EXPECT_EQ(preview.missing.at(1).name, "Mystery B");
  EXPECT_FALSE(preview.activeMissing.has_value());
}

TEST(ImportPreview, SelectMissingMarksTheActiveReplacement) {
  const ImportPreview preview = withPreview();
  const MissingCard target = preview.missing.at(0);

  const ImportPreview next = selectMissing(preview, target);
  if (next.activeMissing.has_value()) {
    const MissingCard &active = next.activeMissing.value();
    EXPECT_EQ(active, target);
  } else {
    FAIL() << "expected the selected entry to become active";
  }
}

TEST(ImportPreview, ReplaceMissingKeepsTheFlaggedQuantityAndSection) {
  const ImportPreview preview = withPreview();
  const MissingCard target = preview.missing.at(0);
  const Card bolt = card("Lightning Bolt", "sta", "49", 1);

  const ImportPreview next = replaceMissing(preview, "mainboard|ZZZ|1", bolt);

  ASSERT_EQ(next.missing.size(), 1u);
  ASSERT_EQ(next.cards.size(), 2u);
  EXPECT_EQ(next.cards.at(0).name, "Forest");
  EXPECT_EQ(next.cards.at(1).name, "Lightning Bolt");
  EXPECT_EQ(next.cards.at(1).quantity, 2); // inherited from Mystery A
  EXPECT_EQ(next.cards.at(1).section, ArenaSection::Mainboard);
  EXPECT_FALSE(next.activeMissing.has_value());
}

TEST(ImportPreview, ReplaceMissingIgnoresAnUnknownKey) {
  const ImportPreview preview = withPreview();
  const ImportPreview next = replaceMissing(preview, "nope|nope|nope", card("Bolt"));
  EXPECT_EQ(next, preview); // value equality: a no-op leaves it unchanged
}

TEST(ImportPreview, RemoveMissingDropsOneFlaggedEntry) {
  const ImportPreview preview = withPreview();
  const ImportPreview next = removeMissing(preview, "mainboard|ZZZ|1");

  ASSERT_EQ(next.missing.size(), 1u);
  EXPECT_EQ(next.missing.at(0).name, "Mystery B");
  ASSERT_EQ(next.cards.size(), 1u); // resolved cards untouched
  EXPECT_FALSE(next.activeMissing.has_value());
}

TEST(ImportPreview, RemoveMissingKeepsTheActiveSelectionCleared) {
  const ImportPreview preview = selectMissing(withPreview(), withPreview().missing.at(0));
  const ImportPreview next = removeMissing(preview, "sideboard|YYY|2");

  ASSERT_EQ(next.missing.size(), 1u);
  EXPECT_EQ(next.missing.at(0).name, "Mystery A");
  EXPECT_FALSE(next.activeMissing.has_value());
}

TEST(ImportPreview, DismissClearsTheActiveReplacementWithoutChangingLists) {
  const ImportPreview preview = selectMissing(withPreview(), withPreview().missing.at(0));
  const ImportPreview next = dismissMissing(preview);

  EXPECT_FALSE(next.activeMissing.has_value());
  ASSERT_EQ(next.missing.size(), 2u);
  ASSERT_EQ(next.cards.size(), 1u);
}

TEST(ImportPreview, ConfirmIsBlockedWhileFlaggedCardsRemain) {
  const ImportPreview preview = withPreview();
  EXPECT_FALSE(canConfirmImport(preview));
  EXPECT_FALSE(confirmImport(preview).has_value());
}

TEST(ImportPreview, ConfirmAppliesTheResolvedCardsOnceClean) {
  ImportPreview preview = withPreview();
  preview = removeMissing(preview, "mainboard|ZZZ|1");
  preview = removeMissing(preview, "sideboard|YYY|2");

  EXPECT_TRUE(canConfirmImport(preview));
  const std::optional<std::vector<Card>> committed = confirmImport(preview);
  if (!committed.has_value()) {
    FAIL() << "expected the preview to be committable";
    return;
  }
  const std::vector<Card> &cards = committed.value();
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards.at(0).name, "Forest");
  EXPECT_EQ(cards.at(0).quantity, 4);
}

TEST(ImportPreview, ReplaceResolvesEveryFlaggedEntryThenConfirms) {
  // Full flow: replace the first flagged entry with a real card, remove the
  // second, then confirm — the preview yields both resolved cards.
  ImportPreview preview = withPreview();
  preview = replaceMissing(preview, "mainboard|ZZZ|1", card("Lightning Bolt", "sta", "49", 1));
  preview = removeMissing(preview, "sideboard|YYY|2");

  ASSERT_TRUE(preview.missing.empty());
  const std::optional<std::vector<Card>> committed = confirmImport(preview);
  if (!committed.has_value()) {
    FAIL() << "expected the preview to be committable";
    return;
  }
  const std::vector<Card> &cards = committed.value();
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards.at(0).name, "Forest");
  EXPECT_EQ(cards.at(1).name, "Lightning Bolt");
  EXPECT_EQ(cards.at(1).quantity, 2);
}

// ---------------------------------------------------------------------------
// M5.3 arena round-trip: import -> preview -> confirm -> export -> re-import.
// ---------------------------------------------------------------------------

using nlohmann::json;

// A minimal valid Scryfall record for a concrete printing.
json record(std::string name, std::string set, std::string number) {
  json r = {
      {"id", "id-" + set + "-" + number},
      {"name", std::move(name)},
      {"set", std::move(set)},
      {"set_name", "Test Set"},
      {"collector_number", std::move(number)},
      {"type_line", "Instant"},
      {"image_uris", json::object({{"png", "https://img/1.png"}})},
  };
  return r;
}

TEST(DeckRoundTrip, ImportPreviewExportReimportIsIdentical) {
  // A small local database covering the export lines.
  std::istringstream in(record("Forest", "war", "263").dump() + "\n" +
                        record("Lightning Bolt", "sta", "49").dump() + "\n");
  CardDatabase db;
  db.load(in);

  // An Arena export with a mainboard and a sideboard.
  const std::string arena = "Deck\n4 Forest (WAR) 263\nSideboard\n2 Lightning Bolt (STA) 49\n";

  const DeckParseResult first = importDeck(arena, db);
  ASSERT_TRUE(first.not_found.empty());
  ASSERT_EQ(first.cards.size(), 2u);
  EXPECT_EQ(first.total_cards, 6);

  // Preview + confirm: the committed cards are exactly the imported ones.
  const ImportPreview preview = makeImportPreview(first);
  const std::optional<std::vector<Card>> committed = confirmImport(preview);
  if (!committed.has_value()) {
    FAIL() << "expected the preview to be committable";
    return;
  }
  const std::vector<Card> &committedCards = committed.value();
  ASSERT_EQ(committedCards.size(), 2u);
  EXPECT_EQ(committedCards.at(1).section, ArenaSection::Sideboard);

  // Export the committed deck, then re-import the export: identical cards.
  const std::string exported = toArenaText(committedCards);
  EXPECT_EQ(exported, "Deck\n4 Forest (WAR) 263\nSideboard\n2 Lightning Bolt (STA) 49");

  const DeckParseResult second = importDeck(exported, db);
  ASSERT_TRUE(second.not_found.empty());
  EXPECT_EQ(second.cards, first.cards);
  EXPECT_EQ(second.total_cards, first.total_cards);
  EXPECT_EQ(second.unique_cards, first.unique_cards);
}

TEST(DeckRoundTrip, CommittingAndExportingKeepsQuantities) {
  std::istringstream in(record("Sol Ring", "lea", "265").dump() + "\n");
  CardDatabase db;
  db.load(in);

  const DeckParseResult result = importDeck("Deck\n3 Sol Ring (LEA) 265\n", db);
  const ImportPreview preview = makeImportPreview(result);
  const std::optional<std::vector<Card>> committed = confirmImport(preview);
  if (!committed.has_value()) {
    FAIL() << "expected the preview to be committable";
    return;
  }
  const std::vector<Card> &committedCards = committed.value();
  EXPECT_EQ(toArenaText(committedCards), "Deck\n3 Sol Ring (LEA) 265");
}

} // namespace
} // namespace mtgcpp::core
