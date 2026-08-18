// Room session state machine, ported from the webapp's state/lobby.effects.ts +
// lobby.reducer.ts + sync.effects.ts (M7.3 + M7.4).
//
// A `Session` is one peer at the table. It owns the local `BoardState`, talks
// to the relay over a `Client`, and implements the session flow: connect/join
// (role + player id arrive with the server's `joined` event), choose a deck
// (mint the local seat and announce it via `deck_selected` — re-announced on
// `player_joined`/`ready` so a late-joining opponent still learns it), learn
// the opponent's deck, hand-reveal consent, and the ready→table transition
// once both decks are known.
//
// Echo suppression: local board actions are applied locally and sent as
// `board_update`; actions replayed from the wire carry the `sync` marker and
// are applied directly, never re-sent — so the two boards can never drift and
// nothing loops.
#pragma once

#include "core/board.h"
#include "core/card.h"
#include "net/client.h"
#include "net/envelope.h"
#include "state/board_state.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mtgcpp::state {

using mtgcpp::core::Deck;
using mtgcpp::core::PlayerSeat;
using mtgcpp::core::RevealCard;

class Session {
public:
  enum class RoomStatus { Idle, Waiting, Ready };

  // Hand-reveal consent flow state.
  struct RevealState {
    std::optional<std::string> pending_request_from;
    std::vector<RevealCard> revealed_hand;
    std::optional<bool> reveal_accepted;

    bool operator==(const RevealState &) const = default;
  };

  explicit Session(mtgcpp::net::Client &client) : client_(client), board_(initialBoardState()) {}

  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  // Drain every pending inbound frame and apply it to the state machine.
  // Returns the number of frames processed. Call from the main loop / the
  // test driver, alongside `Server::runOnce()`.
  std::size_t drain();

  // --- local player actions -------------------------------------------------

  // Choose the deck to play: mints the local seat and announces the deck to
  // the opponent (re-announced when a player joins / the room becomes ready).
  void chooseDeck(const Deck &deck);

  // A local battlefield action: applied locally, and sent to the opponent as a
  // `board_update` when it is one of the 7 syncable mutations.
  void applyLocalAction(const BoardAction &action);

  // Hand-reveal consent.
  void requestHandReveal();
  void acceptHandReveal(const std::vector<RevealCard> &cards);
  void denyHandReveal();
  void dismissRevealPrompt();
  // Close the revealed-hand panel (the webapp's clearRevealedHand).
  void clearRevealed();

  // Leave the room: disconnect the client and reset all session state.
  void leave();

  // --- read-only introspection ----------------------------------------------

  const BoardState &board() const { return board_; }
  const RevealState &reveal() const { return reveal_; }

  bool connected() const { return client_.connected(); }
  bool isReady() const { return status_ == RoomStatus::Ready; }
  // The table can start once the room is ready and both decks are known.
  bool canStartTable() const {
    return isReady() && my_deck_.has_value() && their_deck_.has_value();
  }

  std::optional<std::string> playerId() const { return player_id_; }
  std::optional<PlayerSeat> role() const { return role_; }
  RoomStatus status() const { return status_; }
  const std::vector<std::string> &players() const { return players_; }
  std::optional<std::string> lastError() const { return error_; }

  // Number of inbound `board_update` payloads applied to the board. Because
  // sync-marked mirrors are never re-sent, this stays equal to the number of
  // the opponent's local actions — an echo would show up here as a duplicate.
  std::size_t boardUpdatesApplied() const { return board_updates_applied_; }

private:
  void handleEnvelope(const mtgcpp::net::WsEnvelope &envelope);
  void sendEnvelope(std::string_view event, nlohmann::json payload);
  void announceDeckIfNeeded();

  mtgcpp::net::Client &client_;
  BoardState board_;
  RoomStatus status_ = RoomStatus::Idle;
  std::optional<std::string> player_id_;
  std::optional<PlayerSeat> role_;
  std::vector<std::string> players_;
  std::optional<Deck> my_deck_;
  std::optional<Deck> their_deck_;
  RevealState reveal_;
  std::optional<std::string> error_;
  std::size_t board_updates_applied_ = 0;
};

} // namespace mtgcpp::state
