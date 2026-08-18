// M5.1 deck editor logic tests: the pure card-list operations (add/remove/set
// quantity + totals) ported from the webapp's deck.reducer.ts editor helpers.
// Mirrors the reducer spec cases: adding a card already present bumps its
// quantity instead of duplicating the row, quantity zero deletes, and totals
// are derived fresh from the rows.

#include "core/card.h"
#include "core/deck_editor.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace mtgcpp::core {
namespace {

// A minimal printing factory (mirrors the spec's factory: set sta, number 1).
Card card(std::string name, const std::string &set = "sta", const std::string &number = "1",
          int quantity = 1) {
  Card c;
  c.name = std::move(name);
  c.set_code = set;
  c.set_name = "Test Set";
  c.collector_number = number;
  c.quantity = quantity;
  c.type_line = "Instant";
  return c;
}

TEST(DeckEditor, AddAppendsANewCardRow) {
  const std::vector<Card> cards = addCardToDeck({}, card("Bolt"));
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards.at(0).name, "Bolt");
  EXPECT_EQ(cards.at(0).quantity, 1);
}

TEST(DeckEditor, AddIncrementsTheQuantityOfAnExistingCard) {
  std::vector<Card> cards = addCardToDeck({}, card("Bolt"));
  cards = addCardToDeck(cards, card("Bolt"));
  ASSERT_EQ(cards.size(), 1u); // never duplicated
  EXPECT_EQ(cards.at(0).quantity, 2);
}

TEST(DeckEditor, AddDistinguishesPrintingsBySetAndNumber) {
  // Same name, different printing: two separate rows.
  std::vector<Card> cards = addCardToDeck({}, card("Bolt", "sta", "49"));
  cards = addCardToDeck(cards, card("Bolt", "2x2", "82"));
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards.at(0).quantity, 1);
  EXPECT_EQ(cards.at(1).quantity, 1);
}

TEST(DeckEditor, RemoveDropsTheWholeRow) {
  std::vector<Card> cards = addCardToDeck({}, card("Bolt"));
  cards = removeCardFromDeck(cards, "sta:1");
  EXPECT_TRUE(cards.empty());
}

TEST(DeckEditor, RemoveLeavesUnknownIdsAlone) {
  std::vector<Card> cards = addCardToDeck({}, card("Bolt"));
  cards = removeCardFromDeck(cards, "zzz:99");
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards.at(0).name, "Bolt");
}

TEST(DeckEditor, SetQuantityUpdatesTheRow) {
  std::vector<Card> cards = addCardToDeck({}, card("Bolt"));
  cards = setCardQuantity(cards, "sta:1", 3);
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards.at(0).quantity, 3);
}

TEST(DeckEditor, SetQuantityToZeroRemovesTheRow) {
  std::vector<Card> cards = addCardToDeck({}, card("Bolt"));
  cards = setCardQuantity(cards, "sta:1", 0);
  EXPECT_TRUE(cards.empty());
}

TEST(DeckEditor, SetQuantityToNegativeRemovesTheRow) {
  std::vector<Card> cards = addCardToDeck({}, card("Bolt"));
  cards = setCardQuantity(cards, "sta:1", -4);
  EXPECT_TRUE(cards.empty());
}

TEST(DeckEditor, SetQuantityOnAnUnknownIdIsANoOp) {
  std::vector<Card> cards = addCardToDeck({}, card("Bolt"));
  const std::vector<Card> after = setCardQuantity(cards, "nope:1", 2);
  EXPECT_EQ(after, cards);
}

TEST(DeckEditor, TotalsSumQuantitiesAndCountUniqueRows) {
  std::vector<Card> cards = addCardToDeck({}, card("Forest", "war", "263"));
  cards = addCardToDeck(cards, card("Forest", "war", "263"));
  cards = addCardToDeck(cards, card("Forest", "war", "263"));
  cards = addCardToDeck(cards, card("Bolt", "sta", "49"));

  EXPECT_EQ(deckTotalCards(cards), 4);
  EXPECT_EQ(deckUniqueCards(cards), 2);
  const DeckTotals totals = deckTotals(cards);
  EXPECT_EQ(totals.total_cards, 4);
  EXPECT_EQ(totals.unique_cards, 2);
}

TEST(DeckEditor, TotalsOfAnEmptyDeckAreZero) {
  EXPECT_EQ(deckTotals({}).total_cards, 0);
  EXPECT_EQ(deckTotals({}).unique_cards, 0);
}

TEST(DeckEditor, IdentityFallsBackToTheNameWhenThereIsNoPrinting) {
  Card printingless = card("No Number");
  printingless.set_code.clear();
  printingless.collector_number.clear();

  std::vector<Card> cards = addCardToDeck({}, printingless);
  cards = addCardToDeck(cards, printingless);
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards.at(0).quantity, 2);

  cards = setCardQuantity(cards, "No Number", 7);
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards.at(0).quantity, 7);
}

} // namespace
} // namespace mtgcpp::core
