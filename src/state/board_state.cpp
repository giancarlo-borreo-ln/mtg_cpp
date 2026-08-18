// BoardState reducer implementation (M7.1).
#include "state/board_state.h"

#include <utility>

namespace mtgcpp::state {

namespace {

// Bounds-checked seat board accessor.
SeatBoard &seatBoard(BoardState &state, PlayerSeat seat) { return state.seats.at(seatIndex(seat)); }

const SeatBoard &seatBoard(const BoardState &state, PlayerSeat seat) {
  return state.seats.at(seatIndex(seat));
}

} // namespace

BoardState initialBoardState() {
  return BoardState{
      .seats = {mtgcpp::core::emptySeatBoard(), mtgcpp::core::emptySeatBoard()},
      .stack = {},
      .life = {mtgcpp::core::kStartingLife, mtgcpp::core::kStartingLife},
      .my_deck = std::nullopt,
      .their_deck = std::nullopt,
  };
}

BoardAction setMyDeck(PlayerSeat seat, const Deck &deck) {
  BoardAction action;
  action.kind = BoardAction::Kind::SetMyDeck;
  action.seat = seat;
  action.deck = deck;
  return action;
}

BoardAction setOpponentDeck(PlayerSeat seat, const Deck &deck) {
  BoardAction action;
  action.kind = BoardAction::Kind::SetOpponentDeck;
  action.seat = seat;
  action.deck = deck;
  return action;
}

BoardAction setSeatHand(PlayerSeat seat, const std::vector<BoardCard> &cards) {
  BoardAction action;
  action.kind = BoardAction::Kind::SetSeatHand;
  action.seat = seat;
  action.cards = cards;
  return action;
}

BoardAction setSeatZone(PlayerSeat seat, PlayerZone zone, const std::vector<BoardCard> &cards) {
  BoardAction action;
  action.kind = BoardAction::Kind::SetSeatZone;
  action.seat = seat;
  action.zone = zone;
  action.cards = cards;
  return action;
}

BoardAction pushToStack(BoardCard card) {
  BoardAction action;
  action.kind = BoardAction::Kind::PushToStack;
  action.card = std::move(card);
  return action;
}

BoardAction clearStack() {
  BoardAction action;
  action.kind = BoardAction::Kind::ClearStack;
  return action;
}

BoardAction moveCardToZone(PlayerSeat seat, std::string card_id, PlayerZone zone) {
  BoardAction action;
  action.kind = BoardAction::Kind::MoveCardToZone;
  action.seat = seat;
  action.card_id = std::move(card_id);
  action.zone = zone;
  return action;
}

BoardAction moveCardToStack(std::string card_id) {
  BoardAction action;
  action.kind = BoardAction::Kind::MoveCardToStack;
  action.card_id = std::move(card_id);
  return action;
}

BoardAction tapCard(PlayerSeat seat, std::string card_id) {
  BoardAction action;
  action.kind = BoardAction::Kind::TapCard;
  action.seat = seat;
  action.card_id = std::move(card_id);
  return action;
}

BoardAction addCounter(PlayerSeat seat, std::string card_id) {
  BoardAction action;
  action.kind = BoardAction::Kind::AddCounter;
  action.seat = seat;
  action.card_id = std::move(card_id);
  return action;
}

BoardAction flipCard(PlayerSeat seat, std::string card_id) {
  BoardAction action;
  action.kind = BoardAction::Kind::FlipCard;
  action.seat = seat;
  action.card_id = std::move(card_id);
  return action;
}

BoardAction createToken(PlayerSeat seat, PlayerZone zone, std::string name) {
  BoardAction action;
  action.kind = BoardAction::Kind::CreateToken;
  action.seat = seat;
  action.zone = zone;
  action.name = std::move(name);
  return action;
}

BoardAction setLife(PlayerSeat seat, int life) {
  BoardAction action;
  action.kind = BoardAction::Kind::SetLife;
  action.seat = seat;
  action.life = life;
  return action;
}

BoardAction clearBoard() {
  BoardAction action;
  action.kind = BoardAction::Kind::ClearBoard;
  return action;
}

BoardState applyAction(const BoardState &state, const BoardAction &action) {
  switch (action.kind) {
  case BoardAction::Kind::SetMyDeck: {
    BoardState next = state;
    next.my_deck = action.deck;
    seatBoard(next, action.seat) = mtgcpp::core::seatBoardFromDeck(action.deck.cards, action.seat);
    return next;
  }
  case BoardAction::Kind::SetOpponentDeck: {
    BoardState next = state;
    next.their_deck = action.deck;
    seatBoard(next, action.seat) = mtgcpp::core::seatBoardFromDeck(action.deck.cards, action.seat);
    return next;
  }
  case BoardAction::Kind::SetSeatHand: {
    BoardState next = state;
    seatBoard(next, action.seat).hand = action.cards;
    return next;
  }
  case BoardAction::Kind::SetSeatZone: {
    BoardState next = state;
    seatBoard(next, action.seat).zones.at(zoneIndex(action.zone)) = action.cards;
    return next;
  }
  case BoardAction::Kind::PushToStack: {
    BoardState next = state;
    next.stack.push_back(action.card);
    return next;
  }
  case BoardAction::Kind::ClearStack: {
    BoardState next = state;
    next.stack.clear();
    return next;
  }
  case BoardAction::Kind::MoveCardToZone: {
    const SeatBoard &current = seatBoard(state, action.seat);
    const SeatBoard moved = mtgcpp::core::moveCardToZone(current, action.card_id, action.zone);
    if (moved == current) {
      return state; // no-op: the card is not on this seat
    }
    BoardState next = state;
    seatBoard(next, action.seat) = moved;
    return next;
  }
  case BoardAction::Kind::MoveCardToStack: {
    // The card may be on either seat; the webapp scans both before giving up.
    BoardState next = state;
    std::optional<BoardCard> pushed;
    for (const PlayerSeat seat : {PlayerSeat::Host, PlayerSeat::Guest}) {
      const std::optional<mtgcpp::core::CardDetachment> detached =
          mtgcpp::core::moveCardToStack(seatBoard(next, seat), action.card_id);
      if (detached.has_value()) {
        seatBoard(next, seat) = detached.value().seat_board;
        pushed = detached.value().card;
        break;
      }
    }
    if (!pushed.has_value()) {
      return state; // no-op: the card is on neither seat
    }
    next.stack.push_back(pushed.value());
    return next;
  }
  case BoardAction::Kind::TapCard: {
    const SeatBoard &current = seatBoard(state, action.seat);
    const SeatBoard updated = mtgcpp::core::tapCard(current, action.card_id);
    if (updated == current) {
      return state; // no-op
    }
    BoardState next = state;
    seatBoard(next, action.seat) = updated;
    return next;
  }
  case BoardAction::Kind::AddCounter: {
    const SeatBoard &current = seatBoard(state, action.seat);
    const SeatBoard updated = mtgcpp::core::addCounter(current, action.card_id);
    if (updated == current) {
      return state; // no-op
    }
    BoardState next = state;
    seatBoard(next, action.seat) = updated;
    return next;
  }
  case BoardAction::Kind::FlipCard: {
    const SeatBoard &current = seatBoard(state, action.seat);
    const SeatBoard updated = mtgcpp::core::flipCard(current, action.card_id);
    if (updated == current) {
      return state; // no-op
    }
    BoardState next = state;
    seatBoard(next, action.seat) = updated;
    return next;
  }
  case BoardAction::Kind::CreateToken: {
    // Token creation always mints a new instance (unique id), so there is no
    // no-op path.
    BoardState next = state;
    seatBoard(next, action.seat) = mtgcpp::core::createToken(
        action.seat, seatBoard(next, action.seat), action.zone, action.name);
    return next;
  }
  case BoardAction::Kind::SetLife: {
    BoardState next = state;
    next.life.at(seatIndex(action.seat)) = action.life;
    return next;
  }
  case BoardAction::Kind::ClearBoard:
    return initialBoardState();
  }
  return state; // unreachable: Kind is exhaustive
}

} // namespace mtgcpp::state
