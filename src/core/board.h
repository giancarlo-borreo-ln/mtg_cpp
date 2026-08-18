// Core battlefield seat/zone model, ported from the webapp's core/board.ts.
//
// Everything here is pure and value-semantic: functions take a SeatBoard by
// const-ref and return a new one, never mutating the input. Instance identity
// is carried by BoardCard::id (never by pointer/position), and a card exists in
// exactly one pile at a time — moves are detach-then-append. This invariant is
// what lets the two synced boards stay drift-free (see state/ in Sprint 5).
//
// Milestones: M1.2 seats/zones/minting, M1.3 card moves, M1.4 layout helpers.
#pragma once

#include "core/card.h"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mtgcpp::core {

// A seat at the table; doubles as the player's role within a room.
enum class PlayerSeat { Host, Guest };

std::string playerSeatToString(PlayerSeat seat);
std::optional<PlayerSeat> playerSeatFromString(std::string_view value);

// The rigid battle zones available on each player's board half.
enum class PlayerZone { Lands, Creatures, InstantsSorceries, Graveyard, Exile };

std::string playerZoneToString(PlayerZone zone);
std::optional<PlayerZone> playerZoneFromString(std::string_view value);

// All zones in rendering order (grid placement is fixed via the layout math).
inline constexpr std::array kPlayerZones{PlayerZone::Lands, PlayerZone::Creatures,
                                         PlayerZone::InstantsSorceries, PlayerZone::Graveyard,
                                         PlayerZone::Exile};

inline constexpr std::size_t kPlayerZoneCount = kPlayerZones.size();

// Standard opening hand size.
inline constexpr std::size_t kHandSize = 7;

// Life total both players start with.
inline constexpr int kStartingLife = 20;

// Per-card horizontal offset on the stack (shows play order via overlap).
inline constexpr std::size_t kStackOffsetX = 14;

// Per-card vertical offset on the stack.
inline constexpr std::size_t kStackOffsetY = 10;

// Stable id of the shared Stack drop list.
inline constexpr std::string_view kStackDropListId{"stack"};

// A single card instance on the battlefield (one per physical copy).
struct BoardCard {
  std::string id;
  std::string source_key;
  std::string name;
  std::string image_url;
  std::string mana_cost;
  std::string type_line;
  std::optional<float> cmc;
  bool tapped = false;
  int counters = 0;
  bool flipped = false;
  bool is_token = false;

  bool operator==(const BoardCard &) const = default;
};

// The ordered card piles for a board half's five rigid zones.
using ZoneCards = std::array<std::vector<BoardCard>, kPlayerZoneCount>;

// One player's battlefield: opening hand plus the rigid zone grid.
struct SeatBoard {
  std::vector<BoardCard> hand;
  ZoneCards zones;

  bool operator==(const SeatBoard &) const = default;
};

// Empty zones for a fresh board (graveyard/exile always start empty).
ZoneCards emptyZones();

// A fresh board half: empty hand and empty zones.
SeatBoard emptySeatBoard();

// The seat across the table from a given seat.
PlayerSeat oppositeSeat(PlayerSeat seat);

// Mint BoardCard instances from a deck's cards: quantities are expanded into
// individual copies, each with a seat-prefixed unique id. Returns at most
// `limit` cards in deck order.
std::vector<BoardCard> mintHandCards(const std::vector<Card> &cards, PlayerSeat prefix,
                                     std::size_t limit = kHandSize);

// Mint every copy in the deck as a BoardCard instance (no cap).
std::vector<BoardCard> mintBoardCards(const std::vector<Card> &cards, PlayerSeat prefix);

// Route a card's type line to its rigid board zone.
PlayerZone classifyZone(std::string_view type_line);

// Group card instances into their zones, preserving deck order per zone.
ZoneCards distributeToZones(const std::vector<BoardCard> &instances);

// Build a seat's battlefield from a deck: the first kHandSize copies become
// the opening hand, the remaining copies are distributed onto the zones.
SeatBoard seatBoardFromDeck(const std::vector<Card> &deck, PlayerSeat prefix);

// A card instance detached from a seat, alongside the seat board it was removed
// from. Instances are preserved by value — identity is carried by `id`.
struct CardDetachment {
  BoardCard card;
  SeatBoard seat_board;

  bool operator==(const CardDetachment &) const = default;
};

// Remove the instance with `card_id` from a seat's hand AND every zone. Returns
// the removed instance plus the resulting seat board, or nullopt when the id is
// not on that seat.
std::optional<CardDetachment> detachCard(const SeatBoard &seat_board, std::string_view card_id);

// Move the instance with `card_id` into a target zone, appending it after the
// zone's current cards. Returns the original seat board unchanged when the id
// is not present (so callers can short-circuit on equality).
SeatBoard moveCardToZone(const SeatBoard &seat_board, std::string_view card_id, PlayerZone zone);

// Detach the instance with `card_id` from a seat so it can join the shared
// Stack. Returns the card plus the cleaned seat board, or nullopt when the id
// is not on that seat.
std::optional<CardDetachment> moveCardToStack(const SeatBoard &seat_board,
                                              std::string_view card_id);

// Replace the instance with `card_id` (in the hand or any zone) with a new
// instance produced by `update`. Returns the original seat board unchanged when
// the id is not present, so callers can short-circuit on equality.
SeatBoard updateCardInSeat(const SeatBoard &seat_board, std::string_view card_id,
                           const std::function<BoardCard(const BoardCard &)> &update);

// Toggle the tapped state of a card instance.
SeatBoard tapCard(const SeatBoard &seat_board, std::string_view card_id);

// Add one +1/+1 counter to a card instance.
SeatBoard addCounter(const SeatBoard &seat_board, std::string_view card_id);

// Flip a card instance to its other side.
SeatBoard flipCard(const SeatBoard &seat_board, std::string_view card_id);

// Mint a generic token named `${name} Token` and append it to `zone`. The id is
// seat-prefixed and derived from the number of existing tokens on the seat, so
// every token gets a unique instance id without a shared counter.
SeatBoard createToken(PlayerSeat seat, const SeatBoard &seat_board, PlayerZone zone,
                      std::string_view name);

// Human-readable label for a zone.
std::string zoneLabel(PlayerZone zone);

// Fixed CSS grid-template-areas for a board half. Lands always form the bottom
// row; creatures sit in the middle; instants/sorceries share the spot above;
// graveyard and exile are the right-hand cells.
std::string zoneGridTemplateAreas();

// CSS grid-template-columns value for a hand so every card sits side-by-side in
// an explicit grid cell (no overlap, no animation).
std::string handColumnTemplate(std::size_t card_count);

// CSS transform for the stack card at `index` (0 = played first). Each later
// card shifts by a fixed small offset so the pile visually grows in play order
// — this is the ONLY permitted overlap on the table.
std::string stackCardOffset(std::size_t index);

// Parse a life-total input: clamp to a sane non-negative integer, falling back
// to the starting life when the input isn't a number.
int parseLife(std::string_view raw);

// Project board cards onto the RevealCard shape used by the consent flow.
std::vector<RevealCard> toRevealCards(const std::vector<BoardCard> &cards);

// Drop-list id for a seat's hand.
std::string handDropListId(PlayerSeat seat);

// Drop-list id for one zone of a seat (e.g. `creatures-host`).
std::string zoneDropListId(PlayerSeat seat, PlayerZone zone);

// Every drop-list id on the table: both seats' hands + zones plus the shared
// Stack. All lists connect to this full set so any dragged card can be dropped
// into any zone or the Stack (opponent lists stay disabled/read-only).
std::vector<std::string> allDropListIds();

// Parse the zone out of a drop-list id like `creatures-host` (nullopt for the
// hand/stack).
std::optional<PlayerZone> zoneFromDropListId(std::string_view id);

} // namespace mtgcpp::core
