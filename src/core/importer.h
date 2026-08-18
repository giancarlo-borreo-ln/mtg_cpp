// Arena import resolver, ported from backend/app/services/deck_importer.py (M2.5).
//
// The Python importer resolved printing lines against Scryfall's HTTP batch
// endpoint and fell back to fuzzy name lookups; this local version replaces
// both with the offline CardDatabase indexes — printing (set, collector_number)
// first, then name. Fully offline: no network at runtime.
#pragma once

#include "core/card.h"
#include "core/card_database.h"

#include <string>

namespace mtgcpp::core {

// Resolve Arena deck text against a local card database into a DeckParseResult.
// Aggregation happens per section (the same printing can live in both the
// mainboard and the sideboard without its quantities merging). Printings
// resolve by (set, collector_number) first; anything unresolved — including
// name-only lines and letter-suffixed MDFC printings the database lacks — falls
// back to a name match. Cards matching neither become `not_found` entries.
// Throws DeckParseError when the text contains no card lines.
DeckParseResult importDeck(const std::string &arenaText, const CardDatabase &db);

} // namespace mtgcpp::core
