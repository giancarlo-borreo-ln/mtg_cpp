// M8.1 lobby screen tests: pure view-state + hit-test logic (mirroring the
// webapp's lobby.component.spec.ts), plus a two-peer headless flow that drives
// two lobby screens over the loopback relay to the ready-to-start state.
//
// Clicks are synthesized as sf::Events and routed through the screen (the same
// pattern as the Home / Deck Editor screen tests), so nothing needs a display.

#include "ui/screens/lobby_screen.h"

#include "net/client.h"
#include "net/server.h"
#include "net/transport.h"
#include "state/board_state.h"
#include "state/session.h"

#include <gtest/gtest.h>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mtgcpp::core {
namespace {

using namespace std::chrono_literals;

// The content band the App hands the Lobby (chrome margins from app.cpp).
sf::FloatRect contentRect() { return {40.f, 196.f, 880.f, 300.f}; }

// A synthetic left-button press/release pair at (x, y).
sf::Event mousePress(float x, float y) {
  sf::Event event{};
  event.type = sf::Event::MouseButtonPressed;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

sf::Event mouseRelease(float x, float y) {
  sf::Event event{};
  event.type = sf::Event::MouseButtonReleased;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

// A single printable character, as SFML delivers it for a key press.
sf::Event textChar(char c) {
  sf::Event event{};
  event.type = sf::Event::TextEntered;
  event.text.unicode = static_cast<std::uint32_t>(static_cast<unsigned char>(c));
  return event;
}

sf::Vector2f center(const sf::FloatRect &rect) {
  return {rect.left + (rect.width / 2.f), rect.top + (rect.height / 2.f)};
}

// Complete a click on the point, then drain the resulting action.
LobbyAction click(LobbyScreen &lobby, sf::Vector2f point) {
  lobby.routeEvent(mousePress(point.x, point.y));
  lobby.routeEvent(mouseRelease(point.x, point.y));
  return lobby.pollAction();
}

// Focus the join field and type `text` into it.
void typeInto(LobbyScreen &lobby, const sf::FloatRect &field, std::string_view text) {
  lobby.routeEvent(mousePress(center(field).x, center(field).y));
  lobby.routeEvent(mouseRelease(center(field).x, center(field).y));
  for (const char c : text) {
    lobby.routeEvent(textChar(c));
  }
}

// A connected room state the screen can be pushed into for the pure tests.
void setConnectedRoom(LobbyScreen &lobby) {
  lobby.setConnected(true);
  lobby.setRole(PlayerSeat::Host);
  lobby.setPlayerId("player_1");
  lobby.setPlayers({"player_1"});
  lobby.setStatus(LobbyStatus::Waiting);
  lobby.setShareAddress("192.168.1.10:7500");
}

DeckSummary summaryOf(const std::string &id, const std::string &name,
                      int total = kDeckMinimumSize) {
  DeckSummary summary;
  summary.id = id;
  summary.name = name;
  summary.format = "Standard";
  summary.total_cards = total;
  summary.unique_cards = 2;
  return summary;
}

// ---------------------------------------------------------------------------
// Pure view-state / hit-test logic (mirrors lobby.component.spec.ts)
// ---------------------------------------------------------------------------

TEST(LobbyScreen, StartsInTheConnectView) {
  LobbyScreen lobby;
  EXPECT_FALSE(lobby.connected());
  EXPECT_EQ(lobby.status(), LobbyStatus::Idle);
  EXPECT_FALSE(lobby.canStartTable());
}

TEST(LobbyScreen, CreateRoomButtonReportsCreateRoom) {
  LobbyScreen lobby;
  lobby.relayout(contentRect(), 1.f);

  EXPECT_EQ(click(lobby, center(lobby.createButton().bounds())), LobbyAction::CreateRoom);
}

TEST(LobbyScreen, JoinButtonReportsJoinRoomWithTheTypedAddress) {
  LobbyScreen lobby;
  lobby.relayout(contentRect(), 1.f);
  typeInto(lobby, lobby.joinInput().bounds(), "192.168.1.5:7500");
  EXPECT_EQ(lobby.joinAddress(), "192.168.1.5:7500");

  EXPECT_EQ(click(lobby, center(lobby.joinButton().bounds())), LobbyAction::JoinRoom);
}

TEST(LobbyScreen, JoinIsIgnoredForAnEmptyOrBlankAddress) {
  LobbyScreen lobby;
  lobby.relayout(contentRect(), 1.f);

  EXPECT_EQ(click(lobby, center(lobby.joinButton().bounds())), LobbyAction::None);

  typeInto(lobby, lobby.joinInput().bounds(), "   ");
  EXPECT_EQ(click(lobby, center(lobby.joinButton().bounds())), LobbyAction::None);
}

TEST(LobbyScreen, ConnectingDisablesTheConnectButtons) {
  LobbyScreen lobby;
  lobby.setConnecting(true);

  EXPECT_FALSE(lobby.createButton().isEnabled());
  EXPECT_FALSE(lobby.joinButton().isEnabled());
  EXPECT_EQ(lobby.createButton().label(), "Creating...");
}

TEST(LobbyScreen, ChooseDeckReportsTheClickedDeckIndex) {
  LobbyScreen lobby;
  setConnectedRoom(lobby);
  lobby.setDecks({summaryOf("dA", "Host Deck"), summaryOf("dB", "Guest Deck")});
  lobby.relayout(contentRect(), 1.f);
  ASSERT_EQ(lobby.deckButtons().size(), 2u);

  EXPECT_EQ(click(lobby, center(lobby.deckButtons().at(1).bounds())), LobbyAction::ChooseDeck);
  EXPECT_EQ(lobby.deckIndex(), std::optional<std::size_t>(1));
}

TEST(LobbyScreen, DecksUnderTheMinimumSizeCannotBeChosen) {
  LobbyScreen lobby;
  setConnectedRoom(lobby);
  // dA is legal (60 cards), dB is not (8 cards).
  lobby.setDecks({summaryOf("dA", "Legal Deck"), summaryOf("dB", "Tiny Deck", 8)});
  lobby.relayout(contentRect(), 1.f);
  ASSERT_EQ(lobby.deckButtons().size(), 2u);

  EXPECT_TRUE(lobby.deckPlayable(0));
  EXPECT_FALSE(lobby.deckPlayable(1));
  EXPECT_TRUE(lobby.deckButtons().at(0).isEnabled());
  EXPECT_FALSE(lobby.deckButtons().at(1).isEnabled()); // disabled rows ignore clicks

  EXPECT_EQ(click(lobby, center(lobby.deckButtons().at(1).bounds())), LobbyAction::None);
  EXPECT_FALSE(lobby.deckIndex().has_value());

  // The legal deck still reports a ChooseDeck.
  EXPECT_EQ(click(lobby, center(lobby.deckButtons().at(0).bounds())), LobbyAction::ChooseDeck);
}

TEST(LobbyScreen, SandboxButtonReportsSandbox) {
  LobbyScreen lobby;
  lobby.relayout(contentRect(), 1.f);

  EXPECT_EQ(click(lobby, center(lobby.sandboxButton().bounds())), LobbyAction::Sandbox);
}

TEST(LobbyScreen, LeaveButtonReportsLeaveRoom) {
  LobbyScreen lobby;
  setConnectedRoom(lobby);
  lobby.setMyDeckName("Host Deck");
  lobby.relayout(contentRect(), 1.f);

  EXPECT_EQ(click(lobby, center(lobby.leaveButton().bounds())), LobbyAction::LeaveRoom);
}

TEST(LobbyScreen, CanStartTableRequiresReadyAndBothDecks) {
  LobbyScreen lobby;
  setConnectedRoom(lobby);
  EXPECT_FALSE(lobby.canStartTable());

  lobby.setStatus(LobbyStatus::Ready);
  lobby.setMyDeckName("Host Deck");
  EXPECT_FALSE(lobby.canStartTable());

  lobby.setTheirDeckName("Guest Deck");
  EXPECT_TRUE(lobby.canStartTable());
}

TEST(LobbyScreen, ResetReturnsToTheConnectView) {
  LobbyScreen lobby;
  setConnectedRoom(lobby);
  lobby.setStatus(LobbyStatus::Ready);
  lobby.setMyDeckName("Host Deck");
  lobby.setTheirDeckName("Guest Deck");

  lobby.reset();

  EXPECT_FALSE(lobby.connected());
  EXPECT_EQ(lobby.status(), LobbyStatus::Idle);
  EXPECT_FALSE(lobby.canStartTable());
  EXPECT_FALSE(lobby.role().has_value());
  EXPECT_TRUE(lobby.decks().empty());
}

// ---------------------------------------------------------------------------
// Two-peer headless flow: lobby screens + real sessions over the loopback relay
// ---------------------------------------------------------------------------

TEST(LobbyScreen, TwoPeersDriveTheLobbyToTheReadyToStartState) {
  mtgcpp::net::AsioTransport transport(0);
  mtgcpp::net::Server server(transport);
  server.start();
  const std::string port = std::to_string(transport.localPort());

  // Host side.
  mtgcpp::net::Client hostClient;
  mtgcpp::state::Session hostSession(hostClient);
  LobbyScreen hostLobby;
  hostLobby.setDecks({summaryOf("dA", "Host Deck")});
  hostLobby.setShareAddress("127.0.0.1:" + port);

  // Guest side.
  mtgcpp::net::Client guestClient;
  mtgcpp::state::Session guestSession(guestClient);
  LobbyScreen guestLobby;
  guestLobby.setDecks({summaryOf("dB", "Guest Deck")});

  // Both connect to the relay (the host joins its own embedded relay).
  ASSERT_TRUE(hostClient.connect("127.0.0.1", port));
  ASSERT_TRUE(guestClient.connect("127.0.0.1", port));

  // Mirror the App's pumpLobby: pump the relay + sessions and push session
  // state into the screens.
  const auto sync = [](LobbyScreen &lobby, const mtgcpp::state::Session &session) {
    lobby.setConnected(session.connected());
    lobby.setRole(session.role());
    lobby.setPlayerId(session.playerId());
    lobby.setPlayers(session.players());
    lobby.setStatus(session.status() == mtgcpp::state::Session::RoomStatus::Ready
                        ? LobbyStatus::Ready
                        : LobbyStatus::Waiting);
    if (session.board().my_deck.has_value()) {
      lobby.setMyDeckName(session.board().my_deck.value().name);
    } else {
      lobby.setMyDeckName(std::nullopt);
    }
    if (session.board().their_deck.has_value()) {
      lobby.setTheirDeckName(session.board().their_deck.value().name);
    } else {
      lobby.setTheirDeckName(std::nullopt);
    }
    lobby.setError(session.lastError());
  };

  const auto pump = [&]() {
    server.runOnce();
    hostSession.drain();
    guestSession.drain();
    sync(hostLobby, hostSession);
    sync(guestLobby, guestSession);
    std::this_thread::sleep_for(1ms);
  };

  const auto waitUntil = [&](auto predicate) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
      pump();
      if (predicate()) {
        return true;
      }
    }
    pump();
    return predicate();
  };

  // Both peers join and know their seats.
  ASSERT_TRUE(
      waitUntil([&] { return hostSession.role().has_value() && guestSession.role().has_value(); }));
  EXPECT_EQ(hostLobby.role(), std::optional<PlayerSeat>(PlayerSeat::Host));
  EXPECT_EQ(guestLobby.role(), std::optional<PlayerSeat>(PlayerSeat::Guest));

  // The host picks its deck, then the guest; both are announced to the peer.
  mtgcpp::core::Deck deckA;
  deckA.id = "dA";
  deckA.name = "Host Deck";
  mtgcpp::core::Card bolt;
  bolt.name = "Bolt";
  bolt.set_code = "sta";
  bolt.collector_number = "1";
  bolt.quantity = 4;
  bolt.type_line = "Instant";
  deckA.cards = {bolt};
  deckA.total_cards = 4;
  deckA.unique_cards = 1;
  hostSession.chooseDeck(deckA);

  mtgcpp::core::Deck deckB;
  deckB.id = "dB";
  deckB.name = "Guest Deck";
  mtgcpp::core::Card blaze = bolt;
  blaze.name = "Blaze";
  deckB.cards = {blaze};
  deckB.total_cards = 4;
  deckB.unique_cards = 1;
  guestSession.chooseDeck(deckB);

  // The lobby reflects both decks on both sides and reaches the ready state.
  ASSERT_TRUE(waitUntil([&] { return hostLobby.canStartTable() && guestLobby.canStartTable(); }));
  EXPECT_EQ(hostLobby.status(), LobbyStatus::Ready);
  EXPECT_FALSE(hostLobby.decks().empty()); // the vault is still listed
  EXPECT_EQ(hostLobby.myDeckName(), std::optional<std::string>("Host Deck"));
  EXPECT_EQ(hostLobby.theirDeckName(), std::optional<std::string>("Guest Deck"));
  EXPECT_EQ(guestLobby.myDeckName(), std::optional<std::string>("Guest Deck"));
  EXPECT_EQ(guestLobby.theirDeckName(), std::optional<std::string>("Host Deck"));

  hostSession.leave();
  guestSession.leave();
  server.stop();
}

} // namespace
} // namespace mtgcpp::core
