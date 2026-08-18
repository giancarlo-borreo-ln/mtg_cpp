// Core card/deck data models, ported from the webapp's core/models.ts.
//
// These are pure value types — no ownership, no I/O — shared by the importer,
// the deck repository, the board minting code and the session state. Equality
// is structural (`operator==` defaulted), which is what lets the state layer
// detect no-op mutations by comparing before/after values.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mtgcpp::core {

// Deck sections Arena exports use (lowercase wire values).
enum class ArenaSection { Mainboard, Sideboard, Commander };

std::string arenaSectionToString(ArenaSection section);
std::optional<ArenaSection> arenaSectionFromString(std::string_view value);

struct ScryfallFace {
  std::string name;
  std::map<std::string, std::string> image_uris;

  bool operator==(const ScryfallFace &) const = default;
};

// A resolved card printing in a deck. Absent optional fields are represented by
// empty values (and `std::nullopt` for `cmc`), matching the webapp's model.
struct Card {
  std::string scryfall_id;
  std::string name;
  std::string set_code;
  std::string set_name;
  std::string collector_number;
  int quantity = 1;
  ArenaSection section = ArenaSection::Mainboard;
  std::string mana_cost;
  std::optional<float> cmc;
  std::vector<std::string> colors;
  std::string type_line;
  std::map<std::string, std::string> image_uris;
  std::vector<ScryfallFace> card_faces;

  // True when the card names a concrete printing (set + collector number).
  bool hasPrinting() const { return !set_code.empty() && !collector_number.empty(); }

  bool operator==(const Card &) const = default;
};

// The shape used by the hand-reveal consent flow.
struct RevealCard {
  std::string id;
  std::string name;
  std::string image_url;

  bool operator==(const RevealCard &) const = default;
};

struct DeckSummary {
  std::string id;
  std::string name;
  std::string format;
  int total_cards = 0;
  int unique_cards = 0;
  std::optional<std::string> preview_image;
  std::string created_at;
  std::string updated_at;

  bool operator==(const DeckSummary &) const = default;
};

struct Deck : DeckSummary {
  std::vector<Card> cards;

  bool operator==(const Deck &) const = default;
};

// A card line the importer could not resolve to a real printing.
struct MissingCard {
  std::string name;
  std::string set;
  std::string number;
  int quantity = 1;
  ArenaSection section = ArenaSection::Mainboard;

  bool operator==(const MissingCard &) const = default;
};

struct DeckParseResult {
  std::vector<Card> cards;
  std::vector<MissingCard> not_found;
  int total_cards = 0;
  int unique_cards = 0;

  bool operator==(const DeckParseResult &) const = default;
};

// The fixed, password-free player identities available in this sandbox.
struct PlayerProfile {
  std::string id;
  std::string name;

  bool operator==(const PlayerProfile &) const = default;
};

// The four fixed profiles (carlo, stefano, giancarlo, nicola).
std::span<const PlayerProfile> playerProfiles();

// Human-readable name for a player id; falls back to the id itself.
std::string playerName(std::string_view playerId);

// Stable identity of a card printing within the editor deck.
std::string cardKey(const Card &card);

// Preferred card art URL (png > front face png > normal > empty).
std::string cardImage(const Card &card);

} // namespace mtgcpp::core
