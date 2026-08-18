// Card/Deck <-> JSON serialization (M7.3).
//
// Shared by the deck repository (stored deck documents) and the session layer
// (`deck_selected` payloads carry the full Deck). The stored/on-wire document
// mirrors the webapp's ParsedCard shape, so a deck round-trips without
// re-resolving against the card database.
#pragma once

#include "core/card.h"

#include <nlohmann/json_fwd.hpp>

namespace mtgcpp::core {

nlohmann::json cardToJson(const Card &card);
Card cardFromJson(const nlohmann::json &obj);

nlohmann::json deckToJson(const Deck &deck);
Deck deckFromJson(const nlohmann::json &obj);

} // namespace mtgcpp::core
