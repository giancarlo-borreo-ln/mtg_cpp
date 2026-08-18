// Whole-table battlefield state + pure reducer, ported from the webapp's
// state/board.reducer.ts + board.actions.ts (M7.1).
//
// `BoardState` is a plain value type: both seats' boards, the shared Stack,
// life totals and the two decks. Every mutation is a pure function that takes
// the state by const-ref and returns a NEW state, never mutating the input —
// and actions that cannot change anything (unknown card id, id on the other
// seat, ...) return the input unchanged (a no-op short-circuit, mirroring the
// webapp's `===` checks). This is what keeps two synced boards drift-free.
//
// `BoardAction` is a discriminated struct mirroring board.actions.ts; the
// `sync` flag marks an action replayed from the wire — it is applied locally
// but never re-sent (echo suppression, Sprint 7).
#pragma once

#include "core/board.h"
#include "core/card.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mtgcpp::state {

using mtgcpp::core::BoardCard;
using mtgcpp::core::Deck;
using mtgcpp::core::PlayerSeat;
using mtgcpp::core::PlayerZone;
using mtgcpp::core::SeatBoard;

// Array index for a PlayerSeat (Host=0, Guest=1). Bounds-checked `.at` on the
// result; never use a raw `[]` with a non-constant seat.
inline std::size_t seatIndex(PlayerSeat seat) { return static_cast<std::size_t>(seat); }

// Array index for a PlayerZone (matches kPlayerZones order).
inline std::size_t zoneIndex(PlayerZone zone) { return static_cast<std::size_t>(zone); }

// The whole battlefield. `my_deck` is the local player's chosen deck (re-sent
// to late-joining opponents); `their_deck` is the opponent's (learned from
// their `deck_selected`).
struct BoardState {
  std::array<SeatBoard, 2> seats;
  std::vector<BoardCard> stack;
  // Default member initializers keep a default-constructed BoardState safe
  // (life is otherwise indeterminate — the Session starts from one before any
  // action is applied).
  std::array<int, 2> life{mtgcpp::core::kStartingLife, mtgcpp::core::kStartingLife};
  std::optional<Deck> my_deck = std::nullopt;
  std::optional<Deck> their_deck = std::nullopt;

  bool operator==(const BoardState &) const = default;
};

// The fresh battlefield: empty seats, empty stack, starting life, no decks.
BoardState initialBoardState();

// A single battlefield mutation. `cards`/`card`/`deck` hold the payload of the
// actions that carry them; all other members are ignored for other kinds.
struct BoardAction {
  enum class Kind {
    SetMyDeck,
    SetOpponentDeck,
    SetSeatHand,
    SetSeatZone,
    PushToStack,
    ClearStack,
    MoveCardToZone,
    MoveCardToStack,
    TapCard,
    AddCounter,
    FlipCard,
    CreateToken,
    SetLife,
    ClearBoard,
  };

  Kind kind = Kind::ClearBoard;
  PlayerSeat seat = PlayerSeat::Host;
  PlayerZone zone = PlayerZone::Creatures;
  std::string card_id;
  std::string name;
  std::vector<BoardCard> cards;
  BoardCard card;
  Deck deck;
  int life = mtgcpp::core::kStartingLife;
  bool sync = false; // true when replayed from the wire: applied, never re-sent

  bool operator==(const BoardAction &) const = default;
};

// Action builders mirroring the board.actions.ts creators.
BoardAction setMyDeck(PlayerSeat seat, const Deck &deck);
BoardAction setOpponentDeck(PlayerSeat seat, const Deck &deck);
BoardAction setSeatHand(PlayerSeat seat, const std::vector<BoardCard> &cards);
BoardAction setSeatZone(PlayerSeat seat, PlayerZone zone, const std::vector<BoardCard> &cards);
BoardAction pushToStack(BoardCard card);
BoardAction clearStack();
BoardAction moveCardToZone(PlayerSeat seat, std::string card_id, PlayerZone zone);
BoardAction moveCardToStack(std::string card_id);
BoardAction tapCard(PlayerSeat seat, std::string card_id);
BoardAction addCounter(PlayerSeat seat, std::string card_id);
BoardAction flipCard(PlayerSeat seat, std::string card_id);
BoardAction createToken(PlayerSeat seat, PlayerZone zone, std::string name);
BoardAction setLife(PlayerSeat seat, int life);
BoardAction clearBoard();

// The pure reducer. Applies `action` and returns the resulting state; actions
// that cannot change anything return `state` unchanged.
BoardState applyAction(const BoardState &state, const BoardAction &action);

} // namespace mtgcpp::state
