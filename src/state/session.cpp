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
using mtgcpp::net::readPayloadArray;
using mtgcpp::net::readPayloadBool;
using mtgcpp::net::readPayloadObject;
using mtgcpp::net::readPayloadString;
using mtgcpp::net::readPayloadStringArray;
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
    if (!entry.is_object()) {
      continue; // malformed entry: skip, never crash
    }
    RevealCard card;
    card.id = readPayloadString(entry, "id").value_or("");
    card.name = readPayloadString(entry, "name").value_or("");
    card.image_url = readPayloadString(entry, "image_url").value_or("");
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
    // The three identity fields are required: a malformed join is ignored in
    // full rather than applied with defaults (never trust the wire).
    const std::optional<std::string> id = readPayloadString(envelope.payload, "player_id");
    const std::optional<std::string> roleText = readPayloadString(envelope.payload, "role");
    const std::optional<std::vector<std::string>> players =
        readPayloadStringArray(envelope.payload, "players");
    if (!id.has_value() || !roleText.has_value() || !players.has_value()) {
      return;
    }
    player_id_ = id.value();
    role_ = playerSeatFromString(roleText.value());
    players_ = players.value();
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
    if (const std::optional<std::vector<std::string>> players =
            readPayloadStringArray(envelope.payload, "players");
        players.has_value()) {
      players_ = players.value();
    }
    announceDeckIfNeeded();
    return;
  }
  if (envelope.event == WSEvents::kReady) {
    if (const std::optional<std::vector<std::string>> players =
            readPayloadStringArray(envelope.payload, "players");
        players.has_value()) {
      players_ = players.value();
      status_ = RoomStatus::Ready;
    }
    announceDeckIfNeeded();
    return;
  }
  if (envelope.event == WSEvents::kPlayerLeft) {
    if (const std::optional<std::vector<std::string>> players =
            readPayloadStringArray(envelope.payload, "players");
        players.has_value()) {
      players_ = players.value();
      status_ = RoomStatus::Waiting;
    }
    return;
  }
  if (envelope.event == WSEvents::kDeckSelected) {
    // Untrusted deck payload: every field is validated before use, and a
    // malformed one is ignored (never applied, never crashes).
    const bool alreadyKnown = their_deck_.has_value();
    const std::optional<std::string> seatText = readPayloadString(envelope.payload, "seat");
    const std::optional<nlohmann::json> deckJson = readPayloadObject(envelope.payload, "deck");
    if (seatText.has_value() && deckJson.has_value() && deckJson->is_object() &&
        (!deckJson->contains("cards") || deckJson->at("cards").is_array())) {
      const std::optional<PlayerSeat> seat = playerSeatFromString(seatText.value());
      if (seat.has_value()) {
        if (alreadyKnown) {
          // Redundant re-announce (a join/ready echo of a deck we already
          // learned): re-minting the opponent's seat would wipe their played
          // battlefield (un-tap a tapped card), so the match can drift. Decks
          // are immutable for the match — ignore it.
          return;
        }
        their_deck_ = mtgcpp::core::deckFromJson(deckJson.value());
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
    reveal_.pending_request_from = readPayloadString(envelope.payload, "from");
    return;
  }
  if (envelope.event == WSEvents::kHandRevealResult) {
    reveal_.reveal_accepted = readPayloadBool(envelope.payload, "accepted");
    reveal_.revealed_hand = revealCardsFromJson(
        readPayloadArray(envelope.payload, "cards").value_or(nlohmann::json::array()));
    return;
  }
  if (envelope.event == WSEvents::kError) {
    error_ = readPayloadString(envelope.payload, "message").value_or("Server error");
    return;
  }
  if (envelope.event == WSEvents::kConnectionLost) {
    // The host/relay is gone: the room is dead. Drop back to Idle and surface
    // the reason so the lobby/table can show it (host-shutdown fan-out).
    player_id_.reset();
    role_.reset();
    players_.clear();
    their_deck_.reset();
    reveal_ = RevealState{};
    status_ = RoomStatus::Idle;
    error_ = "Host disconnected";
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
