#include "core/card.h"

// Pure model helpers for the core card/deck types. None of these touch storage
// or the network; they exist so every consumer (parser, importer, repository,
// board minting) derives the same stable strings for a given Card.

namespace mtgcpp::core {

// Arena deck sections serialize as the lowercase wire strings the webapp and
// the import format use; the enum itself is never serialized directly.
std::string arenaSectionToString(ArenaSection section) {
  if (section == ArenaSection::Sideboard) {
    return "sideboard";
  }
  if (section == ArenaSection::Commander) {
    return "commander";
  }
  return "mainboard";
}

std::optional<ArenaSection> arenaSectionFromString(std::string_view value) {
  if (value == "sideboard") {
    return ArenaSection::Sideboard;
  }
  if (value == "commander") {
    return ArenaSection::Commander;
  }
  if (value == "mainboard") {
    return ArenaSection::Mainboard;
  }
  return std::nullopt;
}

// The four fixed, password-free profiles. Static storage keeps the returned
// span alive for the process lifetime; the array must stay ordered as listed
// (the UI picks from it in this order).
std::span<const PlayerProfile> playerProfiles() {
  static const std::array<PlayerProfile, 4> kProfiles{{
      {"carlo", "Carlo"},
      {"stefano", "Stefano"},
      {"giancarlo", "Giancarlo"},
      {"nicola", "Nicola"},
  }};
  return kProfiles;
}

std::string playerName(std::string_view playerId) {
  for (const PlayerProfile &profile : playerProfiles()) {
    if (profile.id == playerId) {
      return profile.name;
    }
  }
  // Unknown ids surface as-is rather than erroring; the wire id is always
  // valid enough to display.
  return std::string(playerId);
}

// Stable identity of a printing within a deck. Prefers the concrete
// set:collector_number when both are present (the "printing" fallback in the
// webapp); otherwise the name alone identifies the entry.
std::string cardKey(const Card &card) {
  if (card.hasPrinting()) {
    return card.set_code + ":" + card.collector_number;
  }
  return card.name;
}

// Preferred art URL for a card, in order: the card's own png, the front face
// png (double-faced cards), then the normal-size image, then empty. The caller
// decides what to render when the result is empty (procedural fallback).
std::string cardImage(const Card &card) {
  const auto &uris = card.image_uris;
  if (const auto it = uris.find("png"); it != uris.end() && !it->second.empty()) {
    return it->second;
  }
  if (!card.card_faces.empty()) {
    const auto &face_uris = card.card_faces.front().image_uris;
    if (const auto it = face_uris.find("png"); it != face_uris.end() && !it->second.empty()) {
      return it->second;
    }
  }
  if (const auto it = uris.find("normal"); it != uris.end() && !it->second.empty()) {
    return it->second;
  }
  return "";
}

} // namespace mtgcpp::core
