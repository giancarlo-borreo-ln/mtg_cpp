// The Lobby screen (M8.1): connect to a game, then pick a deck and wait.
//
// Mirrors the webapp's lobby.component.html, with room codes replaced by the
// connection identity `IP:PORT` (locked decision). There are two views:
//
//   * Connect panel (not connected): a "Create Room" button (starts the
//     embedded relay and joins it locally — the host then shares its `IP:PORT`)
//     and a "Join" row: an `IP:PORT` field plus a Join button.
//   * Room panel (connected): the shared address (host), your seat + the
//     connected players, the deck picker (a vault-like list), the picked-deck
//     status (yours + the opponent's), the wait/ready hints, and Leave.
//
// The screen owns its widgets but ZERO I/O: it reports `LobbyAction`s that the
// App translates into relay/session/router work, exactly like Home and the Deck
// Editor. Everything except draw() is pure and headless-testable.
#pragma once

#include "core/board.h"
#include "core/card.h"
#include "ui/cursor.h"
#include "ui/widgets/button.h"
#include "ui/widgets/text_input.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mtgcpp::core {

// Room status as the lobby renders it (decoupled from state::Session).
enum class LobbyStatus { Idle, Waiting, Ready };

// What the user just did on the Lobby. The App reacts by starting/stopping the
// relay + session and switching screens.
enum class LobbyAction {
  None,
  CreateRoom, // start the embedded relay and join it locally as host
  JoinRoom,   // join the address in joinAddress() as guest
  ChooseDeck, // deckIndex() holds the deck to bring to the table
  Sandbox,    // start a local-only, empty table (debugging, no network)
  LeaveRoom,
};

class LobbyScreen {
public:
  LobbyScreen();

  // --- View state (pushed by the App from the Session) ----------------------
  void setConnected(bool connected);
  bool connected() const { return connected_; }
  void setConnecting(bool connecting);
  bool connecting() const { return connecting_; }

  void setRole(std::optional<PlayerSeat> role);
  const std::optional<PlayerSeat> &role() const { return role_; }
  void setPlayerId(std::optional<std::string> playerId);
  void setPlayers(std::vector<std::string> players);
  void setStatus(LobbyStatus status);
  LobbyStatus status() const { return status_; }
  void setError(std::optional<std::string> error);
  void setMyDeckName(std::optional<std::string> name);
  const std::optional<std::string> &myDeckName() const { return myDeckName_; }
  void setTheirDeckName(std::optional<std::string> name);
  const std::optional<std::string> &theirDeckName() const { return theirDeckName_; }

  // The `IP:PORT` the host shares for peers to join (empty until the host
  // started its relay).
  void setShareAddress(std::string address);
  const std::string &shareAddress() const { return shareAddress_; }

  // Deck vault data for the deck picker (shown until a deck is chosen).
  void setDecks(std::vector<DeckSummary> decks);
  const std::vector<DeckSummary> &decks() const { return decks_; }

  // Clear all session-derived view state back to the connect panel (called by
  // the App on LeaveRoom). The join field's text is kept.
  void reset();

  // The `IP:PORT` the user typed in the join field (the App reads it on
  // JoinRoom).
  const std::string &joinAddress() const { return joinInput_.text(); }

  // The index of the deck queued by a ChooseDeck action (stale by the next
  // setDecks / setMyDeckName).
  std::optional<std::size_t> deckIndex() const { return deckIndex_; }

  // True when the deck at `index` is legal to bring to the table (>= the 60-card
  // minimum). Invalid decks are disabled in the picker; this lets the App run
  // the same check when a ChooseDeck action arrives.
  bool deckPlayable(std::size_t index) const {
    return index < decks_.size() && decks_.at(index).total_cards >= kDeckMinimumSize;
  }

  // True once the room is ready AND both decks are known: the table can start.
  // The App watches this and switches to the table on the rising edge.
  bool canStartTable() const;

  // --- Layout ---------------------------------------------------------------
  void relayout(const sf::FloatRect &content, float scale);

  // --- Interaction ----------------------------------------------------------
  bool routeEvent(const sf::Event &event);
  LobbyAction pollAction();

  void draw(sf::RenderTarget &target, const sf::Font &font, const sf::Font &boldFont) const;

  // The pointer over `point`: Text over the join-address field, Hand over the
  // buttons / deck picker rows.
  CursorKind cursorAt(sf::Vector2f point) const;

  // Exposed for tests: the connect-panel widgets.
  const Button &createButton() const { return createButton_; }
  const Button &joinButton() const { return joinButton_; }
  const TextInput &joinInput() const { return joinInput_; }
  const Button &sandboxButton() const { return sandboxButton_; }
  const Button &leaveButton() const { return leaveButton_; }
  // The deck-picker buttons (only meaningful while the picker is shown).
  const std::vector<Button> &deckButtons() const { return deckButtons_; }

private:
  void relayoutConnect(const sf::FloatRect &content, float scale);
  void relayoutRoom(const sf::FloatRect &content, float scale);
  bool deckPickerVisible() const { return connected_ && !myDeckName_.has_value(); }

  bool connected_ = false;
  bool connecting_ = false;
  std::optional<PlayerSeat> role_;
  std::optional<std::string> playerId_;
  std::vector<std::string> players_;
  LobbyStatus status_ = LobbyStatus::Idle;
  std::optional<std::string> error_;
  std::optional<std::string> myDeckName_;
  std::optional<std::string> theirDeckName_;
  std::string shareAddress_;
  std::vector<DeckSummary> decks_;
  std::optional<std::size_t> deckIndex_;

  float scale_ = 1.f;
  sf::FloatRect content_{0.f, 0.f, 0.f, 0.f};

  // Connect panel.
  Button createButton_;
  TextInput joinInput_;
  Button joinButton_;
  Button sandboxButton_;
  sf::FloatRect connectPanel_{0.f, 0.f, 0.f, 0.f};

  // Room panel.
  Button leaveButton_;
  std::vector<Button> deckButtons_;
  sf::FloatRect infoTop_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect deckPanel_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect statusPanel_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect hintRect_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect leaveRect_{0.f, 0.f, 0.f, 0.f};
};

} // namespace mtgcpp::core
