#include "core/board.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <system_error>
#include <utility>

// All board manipulation here follows the webapp's two hard rules:
//   1. Value semantics — every mutation returns a *new* SeatBoard; the input is
//      never modified in place. Callers compare the result against the input to
//      detect a no-op (unknown card id) and skip re-rendering.
//   2. Instance identity is carried by BoardCard::id, never by pointer/position.
//      Moves re-parent the same instance (it keeps its id), so a card can never
//      exist in two piles at once — a move is always a detach followed by an
//      append. This is what keeps the two synced boards drift-free.

namespace mtgcpp::core {

namespace {

// Wire values for the six zones, indexed by the PlayerZone enum. Stored once in
// static storage so callers can hold std::string_views over them safely.
constexpr std::array<const char *, kPlayerZoneCount> kZoneNames{
    "lands", "creatures", "instants_sorceries", "graveyard", "exile", "artifacts"};

} // namespace

// Seat/zone string forms are the stable wire + drop-list identifiers; the enum
// values themselves never cross a serialization boundary.

std::string playerSeatToString(PlayerSeat seat) {
  return seat == PlayerSeat::Host ? "host" : "guest";
}

std::optional<PlayerSeat> playerSeatFromString(std::string_view value) {
  if (value == "host") {
    return PlayerSeat::Host;
  }
  if (value == "guest") {
    return PlayerSeat::Guest;
  }
  return std::nullopt;
}

std::string playerZoneToString(PlayerZone zone) {
  return kZoneNames.at(static_cast<std::size_t>(zone));
}

std::optional<PlayerZone> playerZoneFromString(std::string_view value) {
  if (value == "lands") {
    return PlayerZone::Lands;
  }
  if (value == "creatures") {
    return PlayerZone::Creatures;
  }
  if (value == "instants_sorceries") {
    return PlayerZone::InstantsSorceries;
  }
  if (value == "graveyard") {
    return PlayerZone::Graveyard;
  }
  if (value == "exile") {
    return PlayerZone::Exile;
  }
  if (value == "artifacts") {
    return PlayerZone::Artifacts;
  }
  return std::nullopt;
}

ZoneCards emptyZones() { return {}; }

SeatBoard emptySeatBoard() { return {}; }

PlayerSeat oppositeSeat(PlayerSeat seat) {
  return seat == PlayerSeat::Host ? PlayerSeat::Guest : PlayerSeat::Host;
}

namespace {

// Expand every card into one BoardCard per copy, assigning seat-prefixed
// sequential ids in deck order. Quantities of 0 are treated as a single copy,
// mirroring the webapp (`Math.max(1, quantity || 1)`).
std::vector<BoardCard> expandInstances(const std::vector<Card> &cards, PlayerSeat prefix) {
  const std::string prefix_str = playerSeatToString(prefix);
  std::vector<BoardCard> instances;
  std::size_t n = 0;
  for (const Card &card : cards) {
    const int copies = std::max(1, card.quantity);
    for (int i = 0; i < copies; ++i) {
      instances.push_back(BoardCard{
          .id = prefix_str + "-" + std::to_string(++n),
          .scryfall_id = card.scryfall_id,
          .source_key = cardKey(card),
          .name = card.name,
          .image_url = cardImage(card),
          .mana_cost = card.mana_cost,
          .type_line = card.type_line,
          .cmc = card.cmc,
          .tapped = false,
          .counters = 0,
          .flipped = false,
          .is_token = false,
      });
    }
  }
  return instances;
}

// Keep only the first `limit` instances; used to carve the opening hand out of
// the fully-minted card list without re-minting.
std::vector<BoardCard> firstCards(std::vector<BoardCard> all, std::size_t limit) {
  const std::size_t take = std::min(limit, all.size());
  all.resize(take);
  return all;
}

} // namespace

// mintHandCards is the capped variant (the opening hand); mintBoardCards
// returns every copy. Both derive ids from the same running counter, so ids
// stay contiguous and seat-unique no matter how the list is sliced afterwards.

std::vector<BoardCard> mintHandCards(const std::vector<Card> &cards, PlayerSeat prefix,
                                     std::size_t limit) {
  return firstCards(expandInstances(cards, prefix), limit);
}

std::vector<BoardCard> mintBoardCards(const std::vector<Card> &cards, PlayerSeat prefix) {
  return expandInstances(cards, prefix);
}

// Zone routing is substring-based on the lowercase type line: land wins over
// creature (e.g. "Land Creature — Insect"), creature wins over artifact (an
// artifact creature is a creature), then artifacts get their own pile, and
// everything else (instants/sorceries/enchantments) falls into the shared
// spell slot.
PlayerZone classifyZone(std::string_view type_line) {
  std::string lower(type_line);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower.find("land") != std::string::npos) {
    return PlayerZone::Lands;
  }
  if (lower.find("creature") != std::string::npos) {
    return PlayerZone::Creatures;
  }
  if (lower.find("artifact") != std::string::npos) {
    return PlayerZone::Artifacts;
  }
  return PlayerZone::InstantsSorceries;
}

// Zone piles preserve the deck order of the input instances.
ZoneCards distributeToZones(const std::vector<BoardCard> &instances) {
  ZoneCards zones = emptyZones();
  for (const BoardCard &instance : instances) {
    zones.at(static_cast<std::size_t>(classifyZone(instance.type_line))).push_back(instance);
  }
  return zones;
}

// The deck is minted once, then split: the first kHandSize copies become the
// opening hand and everything after is routed onto the zone grid. The slice
// boundaries are what make ids host-1..host-7 the hand and host-9+ the zones.
SeatBoard seatBoardFromDeck(const std::vector<Card> &deck, PlayerSeat prefix) {
  std::vector<BoardCard> all = mintBoardCards(deck, prefix);
  std::vector<BoardCard> hand = firstCards(all, kHandSize);
  all.erase(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(hand.size()));
  return {std::move(hand), distributeToZones(all)};
}

// Single detach loop across hand + all five zones. The card is located by id
// and copied out; every other card is re-inserted in its original order, so
// the two seat halves always see the same deletion.
std::optional<CardDetachment> detachCard(const SeatBoard &seat_board, std::string_view card_id) {
  std::optional<BoardCard> found;
  SeatBoard result;
  for (const BoardCard &card : seat_board.hand) {
    if (card.id == card_id) {
      found = card;
    } else {
      result.hand.push_back(card);
    }
  }
  for (const PlayerZone zone : kPlayerZones) {
    const std::size_t zi = static_cast<std::size_t>(zone);
    for (const BoardCard &card : seat_board.zones.at(zi)) {
      if (card.id == card_id) {
        found = card;
      } else {
        result.zones.at(zi).push_back(card);
      }
    }
  }
  if (!found) {
    return std::nullopt;
  }
  return CardDetachment{*found, std::move(result)};
}

// A zone move is detach-then-append: the detached card (same id, so still the
// same instance) is pushed onto the target pile's tail, giving the fixed
// "append order" semantics.
SeatBoard moveCardToZone(const SeatBoard &seat_board, std::string_view card_id, PlayerZone zone) {
  const std::optional<CardDetachment> detached = detachCard(seat_board, card_id);
  if (!detached) {
    return seat_board;
  }
  SeatBoard result = detached->seat_board;
  result.zones.at(static_cast<std::size_t>(zone)).push_back(detached->card);
  return result;
}

// The stack is not a zone of the seat: a card leaves the seat entirely (the
// shared Stack owns it afterwards), so this is just a detach.
std::optional<CardDetachment> moveCardToStack(const SeatBoard &seat_board,
                                              std::string_view card_id) {
  return detachCard(seat_board, card_id);
}

// Same detach-then-reinsert shape as detachCard, but the matched instance is
// replaced by `update` instead of removed. `update` receives a copy and returns
// a new copy, keeping the value-semantic contract. When nothing matched, the
// input is returned unchanged so callers can short-circuit by equality.
SeatBoard updateCardInSeat(const SeatBoard &seat_board, std::string_view card_id,
                           const std::function<BoardCard(const BoardCard &)> &update) {
  bool changed = false;
  SeatBoard result;
  for (const BoardCard &card : seat_board.hand) {
    if (card.id == card_id) {
      changed = true;
      result.hand.push_back(update(card));
    } else {
      result.hand.push_back(card);
    }
  }
  for (const PlayerZone zone : kPlayerZones) {
    const std::size_t zi = static_cast<std::size_t>(zone);
    for (const BoardCard &card : seat_board.zones.at(zi)) {
      if (card.id == card_id) {
        changed = true;
        result.zones.at(zi).push_back(update(card));
      } else {
        result.zones.at(zi).push_back(card);
      }
    }
  }
  if (!changed) {
    return seat_board;
  }
  return result;
}

// The three context-menu toggles are just updateCardInSeat with a one-field
// mutation; keep them as thin lambdas so the toggling semantics live in one
// place (updateCardInSeat), not duplicated per action.

SeatBoard tapCard(const SeatBoard &seat_board, std::string_view card_id) {
  return updateCardInSeat(seat_board, card_id, [](const BoardCard &card) {
    BoardCard updated = card;
    updated.tapped = !card.tapped;
    return updated;
  });
}

SeatBoard addCounter(const SeatBoard &seat_board, std::string_view card_id) {
  return updateCardInSeat(seat_board, card_id, [](const BoardCard &card) {
    BoardCard updated = card;
    updated.counters = card.counters + 1;
    return updated;
  });
}

SeatBoard flipCard(const SeatBoard &seat_board, std::string_view card_id) {
  return updateCardInSeat(seat_board, card_id, [](const BoardCard &card) {
    BoardCard updated = card;
    updated.flipped = !card.flipped;
    return updated;
  });
}

// Token ids are derived from a seat-local count (hand + all zones), never a
// shared counter, so both seats can mint tokens concurrently without colliding.
// The token carries no card data: empty image_url, empty source_key, and a
// synthetic "Token Creature" type line.
SeatBoard createToken(PlayerSeat seat, const SeatBoard &seat_board, PlayerZone zone,
                      std::string_view name) {
  std::size_t token_count = 0;
  for (const BoardCard &card : seat_board.hand) {
    if (card.is_token) {
      ++token_count;
    }
  }
  for (const PlayerZone z : kPlayerZones) {
    for (const BoardCard &card : seat_board.zones.at(static_cast<std::size_t>(z))) {
      if (card.is_token) {
        ++token_count;
      }
    }
  }
  BoardCard token;
  token.id = playerSeatToString(seat) + "-token-" + std::to_string(token_count + 1);
  token.name = std::string(name) + " Token";
  token.type_line = "Token Creature";
  token.cmc = 0.0f;
  token.is_token = true;
  SeatBoard result = seat_board;
  result.zones.at(static_cast<std::size_t>(zone)).push_back(std::move(token));
  return result;
}

// -- Layout / formatting helpers (M1.4) --------------------------------------
// These produce the exact template strings consumed by the renderer; keep the
// whitespace/quote shape identical to the webapp's CSS so the grid math ports
// 1:1. zoneLabel is the only one that is display-facing (capitalized).

std::string zoneLabel(PlayerZone zone) {
  static constexpr std::array<const char *, kPlayerZoneCount> kLabels{
      "Lands", "Creatures", "Instants / Sorceries", "Graveyard", "Exile", "Artifacts"};
  return kLabels.at(static_cast<std::size_t>(zone));
}

// Fixed 3x3 CSS grid: creatures above lands (bottom row), instants/sorceries
// above that, graveyard/exile on the right and artifacts beside the lands.
// Cell names match the zone wire ids.
std::string zoneGridTemplateAreas() {
  return "\"creatures creatures graveyard\" "
         "\"instants_sorceries instants_sorceries exile\" "
         "\"lands lands artifacts\"";
}

// One grid column per card, never zero columns (empty hand still needs a valid
// 1fr track so the layout doesn't collapse).
std::string handColumnTemplate(std::size_t card_count) {
  return "repeat(" + std::to_string(std::max<std::size_t>(1, card_count)) + ", 1fr)";
}

// Each later stack card shifts by kStackOffsetX/Y pixels so the pile grows in
// play order — the only sanctioned overlap on the table.
std::string stackCardOffset(std::size_t index) {
  const std::size_t x = index * kStackOffsetX;
  const std::size_t y = index * kStackOffsetY;
  return "translate(" + std::to_string(x) + "px, " + std::to_string(y) + "px)";
}

// Parses a leading integer (so "30abc" -> 30, like the webapp's parseInt),
// clamps to [0, 9999], and falls back to kStartingLife when no digits parse.
// Uses std::from_chars (locale-independent, no exceptions, no C-string API).
int parseLife(std::string_view raw) {
  if (raw.empty()) {
    return kStartingLife;
  }
  int value = 0;
  const std::from_chars_result result = std::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (result.ec == std::errc::invalid_argument) {
    return kStartingLife;
  }
  if (result.ec == std::errc::result_out_of_range) {
    return 9999;
  }
  return std::clamp(value, 0, 9999);
}

// Projects only the fields the hand-reveal consent flow needs; the source
// BoardCard list is never mutated.
std::vector<RevealCard> toRevealCards(const std::vector<BoardCard> &cards) {
  std::vector<RevealCard> revealed;
  revealed.reserve(cards.size());
  for (const BoardCard &card : cards) {
    revealed.push_back(RevealCard{card.id, card.name, card.image_url});
  }
  return revealed;
}

// Drop-list ids let the renderer address any pile: `hand-<seat>` for hands,
// `<zone>-<seat>` for zones, and the bare `stack` id for the shared Stack.
// zoneFromDropListId is the inverse parse and must only ever match `<zone>-`.

std::string handDropListId(PlayerSeat seat) { return "hand-" + playerSeatToString(seat); }

std::string zoneDropListId(PlayerSeat seat, PlayerZone zone) {
  return playerZoneToString(zone) + "-" + playerSeatToString(seat);
}

std::vector<std::string> allDropListIds() {
  std::vector<std::string> ids;
  ids.reserve((2 * (1 + kPlayerZoneCount)) + 1);
  for (const PlayerSeat seat : {PlayerSeat::Host, PlayerSeat::Guest}) {
    ids.push_back(handDropListId(seat));
    for (const PlayerZone zone : kPlayerZones) {
      ids.push_back(zoneDropListId(seat, zone));
    }
  }
  ids.push_back(std::string(kStackDropListId));
  return ids;
}

// Matches the `<zone>-` prefix only: "hand-host", "stack" and garbage all
// return nullopt. prefix is a view over static storage (kZoneNames), never a
// temporary, so the returned view can't dangle.
std::optional<PlayerZone> zoneFromDropListId(std::string_view id) {
  for (const PlayerZone zone : kPlayerZones) {
    const std::string_view prefix = kZoneNames.at(static_cast<std::size_t>(zone));
    if (id.size() >= prefix.size() + 1 && id.substr(0, prefix.size()) == prefix &&
        id.at(prefix.size()) == '-') {
      return zone;
    }
  }
  return std::nullopt;
}

} // namespace mtgcpp::core
