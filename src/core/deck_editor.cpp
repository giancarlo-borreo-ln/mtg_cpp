// Deck editor operations implementation (M5.1): pure card-list mutations.
//
// These mirror the webapp reducer helpers so the state transitions are
// byte-for-byte the same ones its specs assert: adding a card already present
// increments quantity, setting a quantity to zero deletes the row, and every
// identity comparison goes through cardKey().

#include "core/deck_editor.h"

#include <algorithm>
#include <string>
#include <vector>

namespace mtgcpp::core {

int deckTotalCards(const std::vector<Card> &cards) {
  int total = 0;
  for (const Card &card : cards) {
    total += card.quantity;
  }
  return total;
}

int deckUniqueCards(const std::vector<Card> &cards) { return static_cast<int>(cards.size()); }

DeckTotals deckTotals(const std::vector<Card> &cards) {
  return {deckTotalCards(cards), deckUniqueCards(cards)};
}

std::vector<Card> addCardToDeck(std::vector<Card> cards, const Card &card) {
  const std::string key = cardKey(card);
  for (Card &existing : cards) {
    if (cardKey(existing) == key) {
      existing.quantity += 1; // already in the deck: bump the copy count
      return cards;
    }
  }
  Card added = card;
  added.quantity = 1; // a fresh row always holds exactly one copy
  cards.push_back(std::move(added));
  return cards;
}

std::vector<Card> removeCardFromDeck(std::vector<Card> cards, const std::string &cardId) {
  std::vector<Card> kept;
  kept.reserve(cards.size());
  for (Card &card : cards) {
    if (cardKey(card) != cardId) {
      kept.push_back(std::move(card));
    }
  }
  return kept;
}

std::vector<Card> setCardQuantity(std::vector<Card> cards, const std::string &cardId,
                                  int quantity) {
  if (quantity <= 0) {
    return removeCardFromDeck(std::move(cards), cardId); // zero deletes the row
  }
  for (Card &existing : cards) {
    if (cardKey(existing) == cardId) {
      existing.quantity = quantity;
      return cards;
    }
  }
  return cards; // unknown identity: no-op
}

} // namespace mtgcpp::core
