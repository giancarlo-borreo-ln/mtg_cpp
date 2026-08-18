// Session implementation (M7.3 + M7.4).
#include "state/session.h"

#include "core/card_serialization.h"
#include "net/server.h"
#include "state/sync.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mtgcpp::state {

using mtgcpp::core::playerSeatFromString;
using mtgcpp::core::playerSeatToString;
using mtgcpp::net::WsEnvelope;

namespace {

using namespace std::chrono_literals;

// Reveal cards <-> JSON (array of {id, name, image_url}).
nlohmann::json revealCardsToJson(const std::vector<RevealCard> &cards) {
  nlohmann::json list = nlohmann::json::array();
  for (const RevealCard &card : cards) {
    list.push_back({{"id", card.id}, {"name", card.name}, {"image_url", card.image_url}});
  }
  return list;
}

std::vector<RevealCard> revealCardsFromJson(const nlohmann::json &value) {
  std::vector<RevealCard> cards;
  if (!value.is_array()) {
    return cards;
  }
  cards.reserve(value.size());
  for (const nlohmann::json &entry : value) {
    RevealCard card;
    card.id = entry.value("id", "");
    card.name = entry.value("name", "");
    card.image_url = entry.value("image_url", "");
    cards.push_back(std::move(card));
  }
  return cards;
}

} // namespace

std::size_t Session::drain() {
  std::size_t processed = 0;
  for (;;) {
    const std::optional<std::string> raw = client_.receive(0ms);
    if (!raw.has_value()) {
      break;
    }
    const std::optional<WsEnvelope> envelope = mtgcpp::net::parseEnvelope(raw.value());
    if (envelope.has_value()) {
      handleEnvelope(envelope.value());
      ++processed;
    }
  }
  return processed;
}

void Session::chooseDeck(const Deck &deck) {
  my_deck_ = deck;
  if (role_.has_value()) {
    board_ = applyAction(board_, setMyDeck(role_.value(), deck));
  }
  announceDeckIfNeeded();
}

void Session::applyLocalAction(const BoardAction &action) {
  board_ = applyAction(board_, action);
  if (action.sync) {
    return; // replayed from the wire: applied locally, never re-sent
  }
  const std::optional<nlohmann::json> payload = toBoardUpdatePayload(action);
  if (payload.has_value() && connected()) {
    sendEnvelope(mtgcpp::net::WSEvents::kBoardUpdate, payload.value());
  }
}

void Session::requestHandReveal() {
  reveal_.revealed_hand.clear();
  reveal_.reveal_accepted.reset();
  sendEnvelope(mtgcpp::net::WSEvents::kRequestHandReveal, nlohmann::json::object());
}

void Session::acceptHandReveal(const std::vector<RevealCard> &cards) {
  reveal_.pending_request_from.reset();
  sendEnvelope(mtgcpp::net::WSEvents::kHandRevealAccept, {{"cards", revealCardsToJson(cards)}});
}

void Session::denyHandReveal() {
  reveal_.pending_request_from.reset();
  sendEnvelope(mtgcpp::net::WSEvents::kHandRevealDeny, nlohmann::json::object());
}

void Session::dismissRevealPrompt() { reveal_.pending_request_from.reset(); }

void Session::clearRevealed() {
  reveal_.revealed_hand.clear();
  reveal_.reveal_accepted.reset();
}

void Session::leave() {
  client_.disconnect();
  status_ = RoomStatus::Idle;
  player_id_.reset();
  role_.reset();
  players_.clear();
  my_deck_.reset();
  their_deck_.reset();
  reveal_ = RevealState{};
  error_.reset();
}

void Session::handleEnvelope(const WsEnvelope &envelope) {
  using namespace mtgcpp::net;
  if (envelope.event == WSEvents::kJoined) {
    player_id_ = envelope.payload.value("player_id", "");
    role_ = playerSeatFromString(envelope.payload.value("role", ""));
    players_ = envelope.payload.value("players", std::vector<std::string>{});
    status_ = players_.size() >= 2 ? RoomStatus::Ready : RoomStatus::Waiting;
    // A deck chosen before the join landed (role unknown) is minted now that
    // the seat is known; announcing after a join also covers late choices.
    if (role_.has_value() && my_deck_.has_value()) {
      board_ = applyAction(board_, setMyDeck(role_.value(), my_deck_.value()));
    }
    announceDeckIfNeeded();
    return;
  }
  if (envelope.event == WSEvents::kPlayerJoined) {
    players_ = envelope.payload.value("players", std::vector<std::string>{});
    announceDeckIfNeeded();
    return;
  }
  if (envelope.event == WSEvents::kReady) {
    players_ = envelope.payload.value("players", std::vector<std::string>{});
    status_ = RoomStatus::Ready;
    announceDeckIfNeeded();
    return;
  }
  if (envelope.event == WSEvents::kPlayerLeft) {
    players_ = envelope.payload.value("players", std::vector<std::string>{});
    status_ = RoomStatus::Waiting;
    return;
  }
  if (envelope.event == WSEvents::kDeckSelected) {
    if (envelope.payload.contains("seat") && envelope.payload.contains("deck")) {
      const std::optional<PlayerSeat> seat =
          playerSeatFromString(envelope.payload.at("seat").get<std::string>());
      if (seat.has_value()) {
        their_deck_ = mtgcpp::core::deckFromJson(envelope.payload.at("deck"));
        board_ = applyAction(board_, setOpponentDeck(seat.value(), their_deck_.value()));
        announceDeckIfNeeded();
      }
    }
    return;
  }
  if (envelope.event == WSEvents::kBoardUpdate) {
    const std::optional<BoardAction> action = boardUpdateToAction(envelope.payload);
    if (action.has_value()) {
      board_ = applyAction(board_, action.value());
      ++board_updates_applied_;
    }
    return;
  }
  if (envelope.event == WSEvents::kHandRevealRequest) {
    reveal_.pending_request_from = envelope.payload.value("from", "");
    return;
  }
  if (envelope.event == WSEvents::kHandRevealResult) {
    reveal_.reveal_accepted = envelope.payload.value("accepted", false);
    reveal_.revealed_hand =
        revealCardsFromJson(envelope.payload.value("cards", nlohmann::json::array()));
    return;
  }
  if (envelope.event == WSEvents::kError) {
    error_ = envelope.payload.value("message", "Server error");
    return;
  }
}

void Session::sendEnvelope(std::string_view event, nlohmann::json payload) {
  if (!player_id_.has_value() || !connected()) {
    return;
  }
  client_.sendEnvelope(WsEnvelope{std::string(event),
                                  std::string(mtgcpp::net::Server::kDefaultRoom),
                                  player_id_.value(), std::move(payload)});
}

void Session::announceDeckIfNeeded() {
  if (!my_deck_.has_value() || !role_.has_value() || !player_id_.has_value() || !connected()) {
    return;
  }
  sendEnvelope(mtgcpp::net::WSEvents::kDeckSelected,
               {{"seat", playerSeatToString(role_.value())},
                {"deck", mtgcpp::core::deckToJson(my_deck_.value())}});
}

} // namespace mtgcpp::state
