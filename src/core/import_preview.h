// Pure import-preview state (M5.2), ported from the preview handlers of the
// webapp's deck.reducer.ts.
//
// After an Arena import, the resolved cards are NOT applied to the deck
// directly: they land in a preview where the user reviews them, resolves any
// flagged (unresolved) lines, and only then confirms. This file is that state
// machine with value semantics — every operation takes an `ImportPreview` and
// returns the next one, so the App owns a single `ImportPreview` value and the
// screen just renders it.
//
// Semantics match the reducer exactly:
//   * a flagged card is selected (activeMissing) before it can be replaced,
//   * replacing keeps the flagged entry's quantity and section,
//   * removal / dismissal clear the active selection,
//   * confirming is only possible with zero flagged cards left, and replaces
//     the whole editor deck with the preview's resolved cards.
#pragma once

#include "core/card.h"
#include "core/importer.h"

#include <optional>
#include <string>
#include <vector>

namespace mtgcpp::core {

// A pending Arena import: resolved cards (ready to commit) plus flagged lines
// the importer could not resolve, and the one flagged entry being replaced.
struct ImportPreview {
  std::vector<Card> cards;
  std::vector<MissingCard> missing;
  std::optional<MissingCard> activeMissing;

  bool operator==(const ImportPreview &) const = default;
};

// Build the initial preview from an importDeck result: resolved cards + the
// not-found lines, nothing active yet.
ImportPreview makeImportPreview(const DeckParseResult &result);

// Mark `missing` as the flagged entry being replaced. The value is stored
// verbatim (even if it was not one of the preview's flagged entries), exactly
// like the reducer's previewSelectMissing.
ImportPreview selectMissing(ImportPreview preview, const MissingCard &missing);

// Replace the flagged entry matching `key` (missingCardKey) with `card`,
// keeping the flagged entry's quantity and section, and clear the active
// selection. An unknown key is a no-op (returns `preview` unchanged).
ImportPreview replaceMissing(ImportPreview preview, const std::string &key, const Card &card);

// Drop the flagged entry matching `key` and clear the active selection.
ImportPreview removeMissing(ImportPreview preview, const std::string &key);

// Clear the active selection without changing the resolved or flagged lists.
ImportPreview dismissMissing(ImportPreview preview);

// True when the preview has no flagged cards left, so confirming is legal.
bool canConfirmImport(const ImportPreview &preview);

// The resolved cards to commit into the editor deck, or nullopt while flagged
// cards remain (the reducer's confirmImport guard).
std::optional<std::vector<Card>> confirmImport(const ImportPreview &preview);

} // namespace mtgcpp::core
