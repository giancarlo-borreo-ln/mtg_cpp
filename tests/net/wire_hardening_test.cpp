// M10.2 wire-hardening tests on real sockets: malformed inbound payloads are
// ignored (never crash), an oversize frame drops the connection, and a host
// shutdown fans out to the guest as `connection_lost`. Runs under ASan/UBSan.

#include "net/client.h"
#include "net/envelope.h"
#include "net/server.h"
#include "net/transport.h"
#include "state/session.h"

#include <gtest/gtest.h>

#include <asio.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mtgcpp::net {
namespace {

using namespace std::chrono_literals;

// Pump the server + drain a host client into `hostFrames` + drain the guest
// session, so the three sides progress together.
struct Harness {
  AsioTransport transport{0};
  Server server{transport};
  Client hostClient;
  Client guestClient;
  state::Session session{guestClient};
  std::vector<WsEnvelope> hostFrames;

  Harness() { server.start(); }

  ~Harness() { server.stop(); }
  Harness(const Harness &) = delete;
  Harness &operator=(const Harness &) = delete;
  Harness(Harness &&) = delete;
  Harness &operator=(Harness &&) = delete;

  std::string port() const { return std::to_string(transport.localPort()); }

  bool connectBoth() {
    return hostClient.connect("127.0.0.1", port()) && guestClient.connect("127.0.0.1", port());
  }

  void pump() {
    server.runOnce();
    for (;;) {
      const std::optional<std::string> raw = hostClient.receive(0ms);
      if (!raw.has_value()) {
        break;
      }
      if (const std::optional<WsEnvelope> envelope = parseEnvelope(raw.value());
          envelope.has_value()) {
        hostFrames.push_back(envelope.value());
      }
    }
    session.drain();
  }
};

WsEnvelope makeEnvelope(std::string_view event, std::string_view from, nlohmann::json payload) {
  return WsEnvelope{std::string(event), std::string(Server::kDefaultRoom), std::string(from),
                    std::move(payload)};
}

// Wait until `predicate` holds, pumping the harness each iteration.
template <typename Predicate>
void waitUntil(Harness &harness, Predicate predicate, std::string_view what) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    harness.pump();
    if (predicate()) {
      return;
    }
    std::this_thread::sleep_for(1ms);
  }
  FAIL() << "timed out waiting for " << what;
}

TEST(WireHardening, MalformedInboundPayloadsAreIgnoredNeverCrash) {
  Harness harness;
  ASSERT_TRUE(harness.connectBoth());

  waitUntil(harness, [&] { return harness.session.playerId().has_value(); }, "guest joined");
  EXPECT_EQ(harness.session.playerId().value_or(""), "player_2");
  EXPECT_TRUE(harness.session.role().has_value());

  // --- malformed `deck_selected` payloads: each must be rejected, never
  // applied and never crashing (in-order relay means the valid deck below is
  // only applied after every bad one was processed and refused).
  const std::vector<nlohmann::json> badDecks = {
      {{"seat", "host"}, {"deck", 5}},                        // deck not an object
      {{"seat", "host"}, {"deck", nlohmann::json::array()}},  // deck not an object
      {{"seat", "host"}, {"deck", {{"cards", 7}}}},           // cards not an array
      {{"seat", 123}, {"deck", {{"name", "Evil"}}}},          // seat not a string
      {{"seat", "not_a_seat"}, {"deck", {{"name", "Evil"}}}}, // unknown seat
  };
  for (const nlohmann::json &payload : badDecks) {
    ASSERT_TRUE(harness.hostClient.sendEnvelope(
        makeEnvelope(WSEvents::kDeckSelected, "player_1", payload)));
  }
  const nlohmann::json goodDeck = {{"name", "Friendly"}, {"cards", nlohmann::json::array()}};
  ASSERT_TRUE(harness.hostClient.sendEnvelope(
      makeEnvelope(WSEvents::kDeckSelected, "player_1", {{"seat", "host"}, {"deck", goodDeck}})));

  waitUntil(
      harness,
      [&] {
        return harness.session.board().their_deck.has_value() &&
               harness.session.board().their_deck.value().name == "Friendly";
      },
      "valid deck applied after the malformed ones");

  // --- malformed `board_update` + `hand_reveal_result` payloads are refused.
  // Delivery is in order (same socket, single-threaded relay), so once the
  // valid `tapCard` below is applied, every malformed frame that preceded it
  // was already processed and rejected.
  const std::size_t appliedBefore = harness.session.boardUpdatesApplied();
  ASSERT_TRUE(harness.hostClient.sendEnvelope(
      makeEnvelope(WSEvents::kBoardUpdate, "player_1", {{"action", "notARealAction"}})));
  ASSERT_TRUE(harness.hostClient.sendEnvelope(
      makeEnvelope(WSEvents::kHandRevealResult, "player_1", {{"accepted", "yes"}, {"cards", 5}})));
  ASSERT_TRUE(harness.hostClient.sendEnvelope(
      makeEnvelope(WSEvents::kBoardUpdate, "player_1",
                   {{"action", "tapCard"}, {"seat", "host"}, {"cardId", "host-9"}})));
  waitUntil(
      harness, [&] { return harness.session.boardUpdatesApplied() == appliedBefore + 1; },
      "valid board update applied exactly once after the malformed ones");

  // The malformed reveal result left the reveal state untouched.
  EXPECT_FALSE(harness.session.reveal().reveal_accepted.has_value());
  EXPECT_TRUE(harness.session.reveal().revealed_hand.empty());
  // The guest is still alive and connected (not dropped by the garbage).
  EXPECT_TRUE(harness.guestClient.connected());

  harness.hostClient.disconnect();
  harness.guestClient.disconnect();
  harness.server.stop();
}

TEST(WireHardening, OversizeFrameDropsTheConnection) {
  AsioTransport transport(0);
  Server server(transport);
  server.start();

  // A raw TCP connection (Client refuses to send oversize frames, so bypass it).
  asio::io_context io;
  asio::ip::tcp::socket socket(io);
  asio::error_code ec;
  socket.connect(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), transport.localPort()), ec);
  ASSERT_FALSE(ec);
  // The server seats the connection once the accept lands.
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + 5s;
  while (server.playerCount() == 0u && std::chrono::steady_clock::now() < deadline) {
    server.runOnce();
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_EQ(server.playerCount(), 1u);

  // A 4-byte big-endian length header claiming 0x20000000 (512 MiB) >> 1 MiB.
  std::array<char, 4> header{{static_cast<char>(0x20), 0, 0, 0}};
  asio::write(socket, asio::buffer(header), ec);
  ASSERT_FALSE(ec);

  // The server must drop the connection: drain whatever it already sent (the
  // `joined` frame) and read until the socket closes with EOF/reset.
  std::array<char, 256> buf{};
  asio::error_code readEc;
  for (;;) {
    const std::size_t n = socket.read_some(asio::buffer(buf), readEc);
    if (readEc) {
      break;
    }
    (void)n; // discard the server's queued frames
  }
  EXPECT_TRUE(readEc);

  // The drop surfaces as a disconnect and the seat is freed.
  const std::chrono::steady_clock::time_point dropDeadline = std::chrono::steady_clock::now() + 5s;
  while (server.playerCount() != 0u && std::chrono::steady_clock::now() < dropDeadline) {
    server.runOnce();
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_EQ(server.playerCount(), 0u);
  server.stop();
}

TEST(WireHardening, RedundantDeckAnnounceDoesNotResetTheOpponentsBoard) {
  Harness harness;
  ASSERT_TRUE(harness.connectBoth());
  waitUntil(harness, [&] { return harness.session.playerId().has_value(); }, "guest joined");

  // A deck with two copies, so the guest's mirror of the host seat has host-1
  // and host-2 in hand. First learn mints that seat.
  const nlohmann::json deck = {{"id", "d1"},
                               {"name", "Forests"},
                               {"format", "Other"},
                               {"cards",
                                {{{"id", "c1"},
                                  {"name", "Forest"},
                                  {"set_code", "war"},
                                  {"set_name", "War of the Spark"},
                                  {"collector_number", "263"},
                                  {"quantity", 2},
                                  {"section", "mainboard"},
                                  {"mana_cost", ""},
                                  {"cmc", nullptr},
                                  {"colors", {"G"}},
                                  {"type_line", "Basic Land"},
                                  {"image_uris", nlohmann::json::object()},
                                  {"card_faces", nlohmann::json::array()}}}}};
  const nlohmann::json announce = {{"seat", "host"}, {"deck", deck}};
  ASSERT_TRUE(
      harness.hostClient.sendEnvelope(makeEnvelope(WSEvents::kDeckSelected, "player_1", announce)));
  waitUntil(
      harness,
      [&] {
        return harness.session.board().their_deck.has_value() &&
               !harness.session.board().seats.at(0).hand.empty();
      },
      "guest learns the host deck");

  // A battle action lands on the guest's mirror of the host seat.
  ASSERT_TRUE(harness.hostClient.sendEnvelope(
      makeEnvelope(WSEvents::kBoardUpdate, "player_1",
                   {{"action", "tapCard"}, {"seat", "host"}, {"cardId", "host-1"}})));
  waitUntil(
      harness, [&] { return harness.session.board().seats.at(0).hand.at(0).tapped; },
      "tap mirrors to the guest");

  // A REDUNDANT deck re-announce (a join/ready echo of a deck we already
  // learned) must be ignored: re-minting the host seat would un-tap the card
  // and let the boards drift. In-order delivery means the re-announce below is
  // processed before the second tap, so if it reset the board the first tap
  // would be lost.
  ASSERT_TRUE(
      harness.hostClient.sendEnvelope(makeEnvelope(WSEvents::kDeckSelected, "player_1", announce)));
  ASSERT_TRUE(harness.hostClient.sendEnvelope(
      makeEnvelope(WSEvents::kBoardUpdate, "player_1",
                   {{"action", "tapCard"}, {"seat", "host"}, {"cardId", "host-2"}})));
  waitUntil(
      harness, [&] { return harness.session.board().seats.at(0).hand.at(1).tapped; },
      "second tap applied after the redundant announce");
  EXPECT_TRUE(harness.session.board().seats.at(0).hand.at(0).tapped);

  harness.hostClient.disconnect();
  harness.guestClient.disconnect();
  harness.server.stop();
}

TEST(WireHardening, HostShutdownFansOutConnectionLostToTheGuest) {
  Harness harness;
  ASSERT_TRUE(harness.connectBoth());
  waitUntil(harness, [&] { return harness.session.playerId().has_value(); }, "guest joined");
  EXPECT_EQ(harness.session.status(), state::Session::RoomStatus::Ready);

  // The host's whole app goes away: the relay stops and closes every session.
  harness.server.stop();

  // The guest's socket dies; the client synthesizes `connection_lost` and the
  // session drops back to Idle with a reason, instead of hanging on the room.
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + 5s;
  while (harness.session.status() != state::Session::RoomStatus::Idle &&
         std::chrono::steady_clock::now() < deadline) {
    harness.session.drain();
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_EQ(harness.session.status(), state::Session::RoomStatus::Idle);
  EXPECT_FALSE(harness.session.connected());
  EXPECT_TRUE(harness.session.lastError().has_value());
  EXPECT_EQ(harness.session.lastError().value_or(""), "Host disconnected");
  EXPECT_FALSE(harness.session.role().has_value());

  harness.hostClient.disconnect();
  harness.guestClient.disconnect();
}

} // namespace
} // namespace mtgcpp::net
