// M6.5 loopback integration: the real Asio transport + two real TCP clients
// play the full protocol on 127.0.0.1 — join -> ready -> hand reveal accept and
// deny -> verbatim relay -> leave — plus room-full and no-target paths.

#include "net/client.h"
#include "net/envelope.h"
#include "net/server.h"
#include "net/transport.h"

#include <gtest/gtest.h>

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

// A connected client plus every frame it has received so far.
struct PumpedClient {
  Client client;
  std::vector<WsEnvelope> frames;
};

WsEnvelope makeEnvelope(std::string_view event, std::string_view from, nlohmann::json payload) {
  return WsEnvelope{std::string(event), std::string(Server::kDefaultRoom), std::string(from),
                    std::move(payload)};
}

// Drain the server and every client's pending inbound frames into `frames`.
void pump(Server &server, const std::vector<PumpedClient *> &clients) {
  server.runOnce();
  for (PumpedClient *pumped : clients) {
    for (;;) {
      const std::optional<std::string> raw = pumped->client.receive(0ms);
      if (!raw.has_value()) {
        break;
      }
      const std::optional<WsEnvelope> envelope = parseEnvelope(raw.value());
      if (envelope.has_value()) {
        pumped->frames.push_back(envelope.value());
      }
    }
  }
}

// Pump until `predicate` holds (or the deadline passes).
template <typename Predicate>
bool waitUntil(Predicate predicate, Server &server, const std::vector<PumpedClient *> &clients,
               std::chrono::milliseconds timeout = 5s) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    pump(server, clients);
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  pump(server, clients);
  return predicate();
}

// The frames of one event a client has received, in order.
std::vector<WsEnvelope> events(const PumpedClient &pumped, std::string_view event) {
  std::vector<WsEnvelope> result;
  for (const WsEnvelope &frame : pumped.frames) {
    if (frame.event == event) {
      result.push_back(frame);
    }
  }
  return result;
}

TEST(TwoPeerIntegration, PlaysTheFullProtocolOnRealSockets) {
  AsioTransport transport(0);
  Server server(transport);
  server.start();
  const std::string port = std::to_string(transport.localPort());

  PumpedClient host;
  PumpedClient guest;
  const std::vector<PumpedClient *> clients = {&host, &guest};

  ASSERT_TRUE(host.client.connect("127.0.0.1", port));
  ASSERT_TRUE(guest.client.connect("127.0.0.1", port));

  // join: seats are assigned in arrival order, host is the first connection.
  ASSERT_TRUE(waitUntil(
      [&] {
        return !events(host, WSEvents::kJoined).empty() &&
               !events(guest, WSEvents::kJoined).empty();
      },
      server, clients));
  const WsEnvelope hostJoined = events(host, WSEvents::kJoined).at(0);
  const WsEnvelope guestJoined = events(guest, WSEvents::kJoined).at(0);
  EXPECT_EQ(hostJoined.payload.at("player_id").get<std::string>(), "player_1");
  EXPECT_EQ(hostJoined.payload.at("role").get<std::string>(), "host");
  EXPECT_EQ(guestJoined.payload.at("player_id").get<std::string>(), "player_2");
  EXPECT_EQ(guestJoined.payload.at("role").get<std::string>(), "guest");

  // ready: both sides learn the room is full.
  ASSERT_TRUE(waitUntil(
      [&] {
        return !events(host, WSEvents::kReady).empty() && !events(guest, WSEvents::kReady).empty();
      },
      server, clients));
  EXPECT_TRUE(server.isRoomReady());

  // reveal accept: the request becomes a prompt for the opponent only.
  ASSERT_TRUE(host.client.sendEnvelope(
      makeEnvelope(WSEvents::kRequestHandReveal, "player_1", nlohmann::json::object())));
  ASSERT_TRUE(waitUntil([&] { return !events(guest, WSEvents::kHandRevealRequest).empty(); },
                        server, clients));
  const WsEnvelope prompt = events(guest, WSEvents::kHandRevealRequest).at(0);
  EXPECT_EQ(prompt.from, "player_1");
  EXPECT_EQ(prompt.payload.at("from").get<std::string>(), "player_1");

  const nlohmann::json cards = {
      {{"id", "c1"}, {"name", "Lightning Bolt"}, {"image_url", "https://img/bolt"}},
      {{"id", "c2"}, {"name", "Forest"}, {"image_url", "https://img/forest"}}};
  ASSERT_TRUE(guest.client.sendEnvelope(
      makeEnvelope(WSEvents::kHandRevealAccept, "player_2", {{"cards", cards}})));
  ASSERT_TRUE(waitUntil([&] { return !events(host, WSEvents::kHandRevealResult).empty(); }, server,
                        clients));
  const WsEnvelope accepted = events(host, WSEvents::kHandRevealResult).at(0);
  EXPECT_EQ(accepted.from, "player_2");
  EXPECT_EQ(accepted.payload, (nlohmann::json{{"accepted", true}, {"cards", cards}}));

  // reveal deny: a second request answered with a reject.
  ASSERT_TRUE(host.client.sendEnvelope(
      makeEnvelope(WSEvents::kRequestHandReveal, "player_1", nlohmann::json::object())));
  ASSERT_TRUE(waitUntil([&] { return events(guest, WSEvents::kHandRevealRequest).size() >= 2; },
                        server, clients));
  ASSERT_TRUE(guest.client.sendEnvelope(
      makeEnvelope(WSEvents::kHandRevealDeny, "player_2", nlohmann::json::object())));
  ASSERT_TRUE(waitUntil([&] { return events(host, WSEvents::kHandRevealResult).size() >= 2; },
                        server, clients));
  const WsEnvelope denied = events(host, WSEvents::kHandRevealResult).at(1);
  EXPECT_EQ(denied.payload, (nlohmann::json{{"accepted", false}}));

  // relay: board updates go to the opponent only, verbatim.
  const nlohmann::json payload = {{"action", "tapCard"}, {"seat", "host"}, {"cardId", "host-9"}};
  ASSERT_TRUE(host.client.sendEnvelope(makeEnvelope(WSEvents::kBoardUpdate, "player_1", payload)));
  ASSERT_TRUE(
      waitUntil([&] { return !events(guest, WSEvents::kBoardUpdate).empty(); }, server, clients));
  const WsEnvelope update = events(guest, WSEvents::kBoardUpdate).at(0);
  EXPECT_EQ(update.from, "player_1");
  EXPECT_EQ(update.payload, payload);
  EXPECT_TRUE(events(host, WSEvents::kBoardUpdate).empty());

  // relay: deck selections announce to the opponent only.
  const nlohmann::json deck = {{"seat", "guest"}, {"deck", {{"name", "Guest Deck"}}}};
  ASSERT_TRUE(guest.client.sendEnvelope(makeEnvelope(WSEvents::kDeckSelected, "player_2", deck)));
  ASSERT_TRUE(
      waitUntil([&] { return !events(host, WSEvents::kDeckSelected).empty(); }, server, clients));
  const WsEnvelope selection = events(host, WSEvents::kDeckSelected).at(0);
  EXPECT_EQ(selection.from, "player_2");
  EXPECT_EQ(selection.payload, deck);
  EXPECT_TRUE(events(guest, WSEvents::kDeckSelected).empty());

  // broadcast: the sender receives its own echo and the opponent does too.
  ASSERT_TRUE(host.client.sendEnvelope(makeEnvelope("move_card", "player_1", {{"id", "m1"}})));
  ASSERT_TRUE(waitUntil([&] { return !events(host, "move_card").empty(); }, server, clients));
  ASSERT_TRUE(waitUntil([&] { return !events(guest, "move_card").empty(); }, server, clients));
  EXPECT_EQ(events(host, "move_card").at(0).from, "player_1");
  EXPECT_EQ(events(guest, "move_card").at(0).from, "player_1");

  // leave: the guest dropping out notifies the host and drops the ready state.
  guest.client.disconnect();
  ASSERT_TRUE(
      waitUntil([&] { return !events(host, WSEvents::kPlayerLeft).empty(); }, server, clients));
  const WsEnvelope left = events(host, WSEvents::kPlayerLeft).at(0);
  EXPECT_EQ(left.payload.at("player_id").get<std::string>(), "player_2");
  const std::vector<std::string> players =
      left.payload.at("players").get<std::vector<std::string>>();
  ASSERT_EQ(players.size(), 1u);
  EXPECT_EQ(players.at(0), "player_1");
  EXPECT_FALSE(server.isRoomReady());

  host.client.disconnect();
  server.stop();
}

TEST(TwoPeerIntegration, SoloHandRevealReturnsNoTargetOverRealSockets) {
  AsioTransport transport(0);
  Server server(transport);
  server.start();
  PumpedClient host;
  const std::vector<PumpedClient *> clients = {&host};

  ASSERT_TRUE(host.client.connect("127.0.0.1", std::to_string(transport.localPort())));
  ASSERT_TRUE(waitUntil([&] { return !events(host, WSEvents::kJoined).empty(); }, server, clients));

  ASSERT_TRUE(host.client.sendEnvelope(
      makeEnvelope(WSEvents::kRequestHandReveal, "player_1", nlohmann::json::object())));
  ASSERT_TRUE(waitUntil([&] { return !events(host, WSEvents::kError).empty(); }, server, clients));
  EXPECT_EQ(events(host, WSEvents::kError).at(0).payload.at("code").get<std::string>(),
            WSErrorCodes::kNoTarget);

  host.client.disconnect();
  server.stop();
}

TEST(TwoPeerIntegration, AThirdConnectionIsRejectedWithRoomFull) {
  AsioTransport transport(0);
  Server server(transport);
  server.start();
  const std::string port = std::to_string(transport.localPort());

  PumpedClient first;
  PumpedClient second;
  PumpedClient third;
  const std::vector<PumpedClient *> clients = {&first, &second, &third};

  ASSERT_TRUE(first.client.connect("127.0.0.1", port));
  ASSERT_TRUE(second.client.connect("127.0.0.1", port));
  ASSERT_TRUE(waitUntil(
      [&] {
        return !events(first, WSEvents::kReady).empty() &&
               !events(second, WSEvents::kReady).empty();
      },
      server, clients));

  ASSERT_TRUE(third.client.connect("127.0.0.1", port));
  ASSERT_TRUE(waitUntil(
      [&] { return !events(third, WSEvents::kError).empty() && !third.client.connected(); }, server,
      clients));
  EXPECT_EQ(events(third, WSEvents::kError).at(0).payload.at("code").get<std::string>(),
            WSErrorCodes::kRoomFull);
  EXPECT_TRUE(events(third, WSEvents::kJoined).empty());

  first.client.disconnect();
  second.client.disconnect();
  server.stop();
}

TEST(TwoPeerIntegration, InvalidJsonGetsAnErrorAndTheConnectionSurvives) {
  AsioTransport transport(0);
  Server server(transport);
  server.start();
  PumpedClient host;
  const std::vector<PumpedClient *> clients = {&host};

  ASSERT_TRUE(host.client.connect("127.0.0.1", std::to_string(transport.localPort())));
  ASSERT_TRUE(waitUntil([&] { return !events(host, WSEvents::kJoined).empty(); }, server, clients));

  ASSERT_TRUE(host.client.send("this is not json"));
  ASSERT_TRUE(waitUntil([&] { return !events(host, WSEvents::kError).empty(); }, server, clients));
  EXPECT_EQ(events(host, WSEvents::kError).at(0).payload.at("code").get<std::string>(),
            WSErrorCodes::kInvalidMessage);
  EXPECT_TRUE(host.client.connected());

  // The connection still works afterwards.
  ASSERT_TRUE(host.client.sendEnvelope(makeEnvelope("ping", "player_1", {{"n", 1}})));
  ASSERT_TRUE(waitUntil([&] { return !events(host, "ping").empty(); }, server, clients));
  EXPECT_EQ(events(host, "ping").at(0).from, "player_1");

  host.client.disconnect();
  server.stop();
}

} // namespace
} // namespace mtgcpp::net
