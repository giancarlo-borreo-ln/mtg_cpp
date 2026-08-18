// Pure deck-editing operations (M5.1), ported from the editor helpers of the
// webapp's deck.reducer.ts (upsertCard / removeCard / setQuantity /
// deckTotals).
//
// The deck being built in the editor is just a `std::vector<Card>`: one row per
// distinct printing, where a printing is identified by `cardKey` (set_code +
// collector_number, or the name alone when there is no printing). Every
// operation is a pure function with value semantics — it takes a copy and
// returns the new list — so the caller (App) owns the live deck and tests can
// drive the exact state transitions the webapp's reducer tests cover.
//
// Invariant: rows are unique by `cardKey`. `addCardToDeck` enforces it by
// bumping quantity instead of duplicating; `removeCardFromDeck` and
// `setCardQuantity` remove by that same identity, never by position.
#pragma once

#include "core/card.h"

#include <string>
#include <vector>

namespace mtgcpp::core {

// Aggregate totals of a deck's card list (the reducer's deckTotals): total =
// sum of every row quantity, unique = number of distinct printing rows.
struct DeckTotals {
  int total_cards = 0;
  int unique_cards = 0;

  bool operator==(const DeckTotals &) const = default;
};

// Sum of the rows' quantities (total cards in the deck).
int deckTotalCards(const std::vector<Card> &cards);

// Number of distinct printing rows in the deck.
int deckUniqueCards(const std::vector<Card> &cards);

// Convenience wrapper returning both totals at once.
DeckTotals deckTotals(const std::vector<Card> &cards);

// Add one copy of `card` to `cards`. A row for the same printing identity
// (cardKey) gets its quantity bumped by 1; otherwise the card is appended as a
// new row with quantity 1 — exactly the reducer's upsertCard.
std::vector<Card> addCardToDeck(std::vector<Card> cards, const Card &card);

// Remove the whole row whose printing identity matches `cardId`. Unknown ids
// leave the list unchanged.
std::vector<Card> removeCardFromDeck(std::vector<Card> cards, const std::string &cardId);

// Set the quantity of the row matching `cardId` to `quantity`; a quantity of 0
// or less removes the row (the reducer's setQuantity, which treats zero as a
// delete). Unknown ids leave the list unchanged.
std::vector<Card> setCardQuantity(std::vector<Card> cards, const std::string &cardId, int quantity);

} // namespace mtgcpp::core
