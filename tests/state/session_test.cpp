// M7.3 + M7.4 session tests: deck announcement/learning (incl. re-join), the
// headless simulated match over loopback (byte-identical boards, echoed
// mirrors never re-sent), hand-reveal consent, and leave/reset.

#include "state/session.h"

#include "net/client.h"
#include "net/envelope.h"
#include "net/server.h"
#include "net/transport.h"
#include "state/board_state.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mtgcpp::state {
namespace {

// Assert `opt` holds and return a const reference to its value, or fail the
// test. (clang-tidy does not model gtest ASSERT as an optional guard.)
template <typename T> const T &expectValue(const std::optional<T> &opt) {
  if (opt.has_value()) {
    return opt.value();
  }
  ADD_FAILURE() << "expected an optional value";
  static const T empty{};
  return empty;
}

using mtgcpp::core::Deck;
using mtgcpp::core::PlayerSeat;
using mtgcpp::core::PlayerZone;

using namespace std::chrono_literals;

// A peer at the table: its own client + session.
struct Peer {
  mtgcpp::net::Client client;
  Session session;

  Peer() : session(client) {}
};

// Pump the relay and every peer once.
void pump(mtgcpp::net::Server &server, const std::vector<Peer *> &peers) {
  server.runOnce();
  for (Peer *peer : peers) {
    peer->session.drain();
  }
}

// Pump until `predicate` holds (or the deadline passes).
template <typename Predicate>
bool waitUntil(Predicate predicate, mtgcpp::net::Server &server, const std::vector<Peer *> &peers,
               std::chrono::milliseconds timeout = 5s) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    pump(server, peers);
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  pump(server, peers);
  return predicate();
}

mtgcpp::core::Card card(std::string name, std::string type_line, int quantity) {
  auto result = mtgcpp::core::Card{};
  result.name = std::move(name);
  result.set_code = "sta";
  result.set_name = "Test Set";
  result.collector_number = "1";
  result.quantity = quantity;
  result.type_line = std::move(type_line);
  return result;
}

// Two distinct decks so each seat mints a different board; both peers build
// BOTH seats from the same two decks, which is what makes the boards identical.
Deck deckA() {
  Deck deck;
  deck.id = "dA";
  deck.name = "Host Deck";
  deck.format = "Standard";
  deck.total_cards = 8;
  deck.unique_cards = 2;
  deck.cards = {card("Bolt", "Instant", 4), card("Forest", "Basic Land — Forest", 4)};
  return deck;
}

Deck deckB() {
  Deck deck;
  deck.id = "dB";
  deck.name = "Guest Deck";
  deck.format = "Standard";
  deck.total_cards = 8;
  deck.unique_cards = 2;
  deck.cards = {card("Blaze", "Instant", 4), card("Island", "Basic Land — Island", 4)};
  return deck;
}

const SeatBoard &seatOf(const Session &session, PlayerSeat seat) {
  return session.board().seats.at(seatIndex(seat));
}

// The battlefield portion of two states is what must be byte-identical across
// peers; `my_deck`/`their_deck` are per-side mirror images by construction.
bool sameBattlefield(const BoardState &a, const BoardState &b) {
  return a.seats == b.seats && a.stack == b.stack && a.life == b.life;
}

// Regression (found by the Sprint 12 integration harness): a freshly-created
// Session used to carry an INDETERMINATE `life` array (BoardState is an
// aggregate with no default initializer for it), so a session that had not yet
// applied a SetLife read garbage life totals. The session must start from
// initialBoardState().
TEST(Session, BoardStartsWithFullLifeTotals) {
  mtgcpp::net::Client client; // never connected: no networking needed
  Session session(client);
  EXPECT_EQ(session.board().life,
            (std::array<int, 2>{mtgcpp::core::kStartingLife, mtgcpp::core::kStartingLife}));
  // A no-op clear also resets to full totals.
  session.leave();
  EXPECT_EQ(session.board().life,
            (std::array<int, 2>{mtgcpp::core::kStartingLife, mtgcpp::core::kStartingLife}));
}

TEST(Session, DecksAreAnnouncedAndLearnedOnBothSidesAfterRejoin) {
  mtgcpp::net::AsioTransport transport(0);
  mtgcpp::net::Server server(transport);
  server.start();
  const std::string port = std::to_string(transport.localPort());

  Peer host;
  Peer guest;
  const std::vector<Peer *> peers = {&host, &guest};

  // The host joins and picks its deck BEFORE the guest connects. The announce
  // is dropped (no opponent yet) but the deck is remembered.
  ASSERT_TRUE(host.client.connect("127.0.0.1", port));
  ASSERT_TRUE(waitUntil([&] { return host.session.role().has_value(); }, server, peers));
  EXPECT_EQ(expectValue(host.session.role()), PlayerSeat::Host);
  host.session.chooseDeck(deckA());
  EXPECT_FALSE(host.session.canStartTable());

  // The guest joins late: the host re-announces its deck (on player_joined and
  // ready), so the guest still learns it.
  ASSERT_TRUE(guest.client.connect("127.0.0.1", port));
  ASSERT_TRUE(
      waitUntil([&] { return guest.session.canStartTable() == false && guest.session.isReady(); },
                server, peers));
  ASSERT_TRUE(waitUntil(
      [&] {
        return guest.session.board().their_deck.has_value() &&
               guest.session.board().their_deck.value() == deckA();
      },
      server, peers));
  EXPECT_TRUE(guest.session.board().their_deck.has_value());
  EXPECT_EQ(expectValue(guest.session.board().their_deck), deckA());

  // The guest picks its deck; the host learns it too.
  guest.session.chooseDeck(deckB());
  ASSERT_TRUE(waitUntil(
      [&] {
        return host.session.board().their_deck.has_value() &&
               host.session.board().their_deck.value() == deckB();
      },
      server, peers));

  ASSERT_TRUE(waitUntil([&] { return host.session.canStartTable(); }, server, peers));
  ASSERT_TRUE(waitUntil([&] { return guest.session.canStartTable(); }, server, peers));

  // Both peers built BOTH seats from the same two decks: the battlefield is
  // identical, and each announced deck was learned losslessly by the peer
  // (my_deck on one side == their_deck on the other — a mirror image).
  EXPECT_TRUE(sameBattlefield(host.session.board(), guest.session.board()));
  EXPECT_EQ(host.session.board().my_deck, guest.session.board().their_deck);
  EXPECT_EQ(guest.session.board().my_deck, host.session.board().their_deck);
  EXPECT_EQ(host.session.playerId().value_or(""), "player_1");
  EXPECT_EQ(guest.session.playerId().value_or(""), "player_2");
  EXPECT_EQ(expectValue(guest.session.role()), PlayerSeat::Guest);

  host.session.leave();
  guest.session.leave();
  server.stop();
}

TEST(Session, HandRevealConsentRoutesAcceptAndDeny) {
  mtgcpp::net::AsioTransport transport(0);
  mtgcpp::net::Server server(transport);
  server.start();
  const std::string port = std::to_string(transport.localPort());

  Peer host;
  Peer guest;
  const std::vector<Peer *> peers = {&host, &guest};
  ASSERT_TRUE(host.client.connect("127.0.0.1", port));
  ASSERT_TRUE(guest.client.connect("127.0.0.1", port));
  ASSERT_TRUE(
      waitUntil([&] { return host.session.isReady() && guest.session.isReady(); }, server, peers));
  host.session.chooseDeck(deckA());
  guest.session.chooseDeck(deckB());
  ASSERT_TRUE(
      waitUntil([&] { return host.session.canStartTable() && guest.session.canStartTable(); },
                server, peers));

  // Host requests a reveal -> the guest is prompted with the requester.
  host.session.requestHandReveal();
  ASSERT_TRUE(waitUntil([&] { return guest.session.reveal().pending_request_from.has_value(); },
                        server, peers));
  EXPECT_EQ(expectValue(guest.session.reveal().pending_request_from), "player_1");

  // Guest accepts, revealing its hand cards.
  const std::vector<mtgcpp::core::RevealCard> revealed =
      mtgcpp::core::toRevealCards(seatOf(guest.session, PlayerSeat::Guest).hand);
  guest.session.acceptHandReveal(revealed);
  ASSERT_TRUE(waitUntil(
      [&] {
        return host.session.reveal().reveal_accepted.has_value() &&
               host.session.reveal().reveal_accepted.value();
      },
      server, peers));
  EXPECT_EQ(host.session.reveal().revealed_hand, revealed);
  EXPECT_FALSE(guest.session.reveal().pending_request_from.has_value());

  // A second request, denied this time.
  host.session.requestHandReveal();
  ASSERT_TRUE(waitUntil([&] { return guest.session.reveal().pending_request_from.has_value(); },
                        server, peers));
  guest.session.denyHandReveal();
  ASSERT_TRUE(waitUntil(
      [&] {
        return host.session.reveal().reveal_accepted.has_value() &&
               !host.session.reveal().reveal_accepted.value();
      },
      server, peers));
  EXPECT_TRUE(host.session.reveal().revealed_hand.empty());

  host.session.leave();
  guest.session.leave();
  server.stop();
}

TEST(Session, SoloHandRevealReportsAnError) {
  mtgcpp::net::AsioTransport transport(0);
  mtgcpp::net::Server server(transport);
  server.start();

  Peer host;
  const std::vector<Peer *> peers = {&host};
  ASSERT_TRUE(host.client.connect("127.0.0.1", std::to_string(transport.localPort())));
  ASSERT_TRUE(waitUntil([&] { return host.session.role().has_value(); }, server, peers));

  host.session.requestHandReveal();
  ASSERT_TRUE(waitUntil([&] { return host.session.lastError().has_value(); }, server, peers));
  EXPECT_NE(expectValue(host.session.lastError()).find("No opponent"), std::string::npos);

  host.session.leave();
  server.stop();
}

TEST(Session, HeadlessSimulatedMatchKeepsBoardsIdenticalAndNeverEchoes) {
  mtgcpp::net::AsioTransport transport(0);
  mtgcpp::net::Server server(transport);
  server.start();
  const std::string port = std::to_string(transport.localPort());

  Peer host;
  Peer guest;
  const std::vector<Peer *> peers = {&host, &guest};
  ASSERT_TRUE(host.client.connect("127.0.0.1", port));
  ASSERT_TRUE(guest.client.connect("127.0.0.1", port));
  ASSERT_TRUE(
      waitUntil([&] { return host.session.isReady() && guest.session.isReady(); }, server, peers));
  host.session.chooseDeck(deckA());
  guest.session.chooseDeck(deckB());
  ASSERT_TRUE(
      waitUntil([&] { return host.session.canStartTable() && guest.session.canStartTable(); },
                server, peers));
  EXPECT_TRUE(sameBattlefield(host.session.board(), guest.session.board()));

  // Host drives a sequence of local battlefield changes.
  const SeatBoard &hostBoard = seatOf(host.session, PlayerSeat::Host);
  const std::string h1 = hostBoard.hand.at(0).id;
  const std::string h2 = hostBoard.hand.at(1).id;
  const std::string h3 = hostBoard.hand.at(2).id;
  const std::string h4 = hostBoard.hand.at(3).id;
  const std::string h5 = hostBoard.hand.at(4).id;

  host.session.applyLocalAction(moveCardToZone(PlayerSeat::Host, h1, PlayerZone::Creatures));
  host.session.applyLocalAction(tapCard(PlayerSeat::Host, h2));
  host.session.applyLocalAction(addCounter(PlayerSeat::Host, h3));
  host.session.applyLocalAction(flipCard(PlayerSeat::Host, h4));
  host.session.applyLocalAction(createToken(PlayerSeat::Host, PlayerZone::Creatures, "Goblin"));
  host.session.applyLocalAction(setLife(PlayerSeat::Host, 17));
  host.session.applyLocalAction(moveCardToStack(h5));

  // Guest drives a couple of its own local changes.
  const std::string g1 = seatOf(guest.session, PlayerSeat::Guest).hand.at(0).id;
  guest.session.applyLocalAction(tapCard(PlayerSeat::Guest, g1));
  guest.session.applyLocalAction(setLife(PlayerSeat::Guest, 19));

  // Both boards converge on the same battlefield value.
  ASSERT_TRUE(waitUntil(
      [&] { return sameBattlefield(host.session.board(), guest.session.board()); }, server, peers));

  const BoardState &hostBoard_ = host.session.board();
  EXPECT_EQ(hostBoard_.life.at(seatIndex(PlayerSeat::Host)), 17);
  EXPECT_EQ(hostBoard_.life.at(seatIndex(PlayerSeat::Guest)), 19);
  EXPECT_EQ(hostBoard_.stack.size(), 1u);
  EXPECT_EQ(hostBoard_.stack.at(0).id, h5);

  // Echo suppression: a mirrored board_update is applied locally but never
  // re-sent. Each peer therefore applies exactly as many inbound updates as
  // the opponent's LOCAL actions — a re-sent mirror would arrive back here and
  // inflate the count.
  EXPECT_EQ(host.session.boardUpdatesApplied(), 2u);  // the guest's two actions
  EXPECT_EQ(guest.session.boardUpdatesApplied(), 7u); // the host's seven actions

  host.session.leave();
  guest.session.leave();
  server.stop();
}

TEST(Session, ChoosingADeckBeforeJoiningStillMintsTheSeat) {
  // Regression: a deck chosen before the `joined` event (role unknown) must
  // still mint the local seat once the join lands and still be announced.
  mtgcpp::net::AsioTransport transport(0);
  mtgcpp::net::Server server(transport);
  server.start();

  Peer host;
  const std::vector<Peer *> peers = {&host};
  ASSERT_TRUE(host.client.connect("127.0.0.1", std::to_string(transport.localPort())));

  // Choose immediately; the join has not necessarily been processed yet.
  host.session.chooseDeck(deckA());
  ASSERT_TRUE(waitUntil([&] { return host.session.role().has_value(); }, server, peers));

  EXPECT_EQ(expectValue(host.session.role()), PlayerSeat::Host);
  EXPECT_EQ(seatOf(host.session, PlayerSeat::Host).hand.size(), 7u);
  ASSERT_TRUE(host.session.board().my_deck.has_value());
  EXPECT_EQ(expectValue(host.session.board().my_deck), deckA());

  host.session.leave();
  server.stop();
}

TEST(Session, LeaveNotifiesThePeerAndResetsTheSession) {
  mtgcpp::net::AsioTransport transport(0);
  mtgcpp::net::Server server(transport);
  server.start();
  const std::string port = std::to_string(transport.localPort());

  Peer host;
  Peer guest;
  const std::vector<Peer *> peers = {&host, &guest};
  ASSERT_TRUE(host.client.connect("127.0.0.1", port));
  ASSERT_TRUE(guest.client.connect("127.0.0.1", port));
  ASSERT_TRUE(
      waitUntil([&] { return host.session.isReady() && guest.session.isReady(); }, server, peers));

  host.session.leave();

  // The guest is told the host left and the room drops back to waiting.
  ASSERT_TRUE(waitUntil([&] { return guest.session.status() == Session::RoomStatus::Waiting; },
                        server, peers));
  EXPECT_EQ(guest.session.players().size(), 1u);
  EXPECT_EQ(guest.session.players().at(0), "player_2");
  EXPECT_FALSE(guest.session.isReady());

  // The host's own session is fully reset.
  EXPECT_FALSE(host.session.connected());
  EXPECT_FALSE(host.session.role().has_value());
  EXPECT_FALSE(host.session.board().my_deck.has_value());

  guest.session.leave();
  server.stop();
}

} // namespace
} // namespace mtgcpp::state
