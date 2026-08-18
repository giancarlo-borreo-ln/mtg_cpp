// M6.3 server core tests against a fake transport: seat assignment, room-full
// rejection, impersonation overwrite, and the join/leave/ready event flow.

#include "net/envelope.h"
#include "net/server.h"
#include "net/transport.h"

#include "fake_transport.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mtgcpp::net {
namespace {

// Build a serialized envelope a client would send over the wire.
std::string envelope(std::string_view event, std::string_view room, std::string_view from,
                     nlohmann::json payload) {
  return serializeEnvelope(
      WsEnvelope{std::string(event), std::string(room), std::string(from), std::move(payload)});
}

// Join one player into the room and drain; returns the connected id.
std::size_t join(Server &server, FakeTransport &transport, std::size_t connectionId) {
  transport.simulateConnection(connectionId);
  server.runOnce();
  return connectionId;
}

TEST(Server, SinglePlayerJoinsAndIsSeatedAsHost) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  join(server, transport, 1);

  const std::vector<WsEnvelope> joined = transport.eventsTo(1, WSEvents::kJoined);
  ASSERT_EQ(joined.size(), 1u);
  EXPECT_EQ(joined.at(0).room, Server::kDefaultRoom);
  EXPECT_EQ(joined.at(0).from, "server");
  EXPECT_EQ(joined.at(0).payload.at("player_id").get<std::string>(), "player_1");
  EXPECT_EQ(joined.at(0).payload.at("role").get<std::string>(), "host");
  const std::vector<std::string> players =
      joined.at(0).payload.at("players").get<std::vector<std::string>>();
  ASSERT_EQ(players.size(), 1u);
  EXPECT_EQ(players.at(0), "player_1");
  EXPECT_EQ(server.hostId().value_or(""), "player_1");
}

TEST(Server, SecondPlayerGetsItsOwnSeatAndTheRoomIsNotified) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  join(server, transport, 1);
  join(server, transport, 2);

  const std::vector<WsEnvelope> hostJoined = transport.eventsTo(1, WSEvents::kJoined);
  const std::vector<WsEnvelope> guestJoined = transport.eventsTo(2, WSEvents::kJoined);
  ASSERT_EQ(hostJoined.size(), 1u);
  ASSERT_EQ(guestJoined.size(), 1u);
  EXPECT_EQ(hostJoined.at(0).payload.at("player_id").get<std::string>(), "player_1");
  EXPECT_EQ(hostJoined.at(0).payload.at("role").get<std::string>(), "host");
  EXPECT_EQ(guestJoined.at(0).payload.at("player_id").get<std::string>(), "player_2");
  EXPECT_EQ(guestJoined.at(0).payload.at("role").get<std::string>(), "guest");
  const std::vector<std::string> guestPlayers =
      guestJoined.at(0).payload.at("players").get<std::vector<std::string>>();
  ASSERT_EQ(guestPlayers.size(), 2u);
  EXPECT_EQ(guestPlayers.at(0), "player_1");
  EXPECT_EQ(guestPlayers.at(1), "player_2");

  // The host is told a player joined (the second player_joined broadcast).
  const std::vector<WsEnvelope> announcements = transport.eventsTo(1, WSEvents::kPlayerJoined);
  bool sawFull = false;
  for (const WsEnvelope &announcement : announcements) {
    const std::vector<std::string> players =
        announcement.payload.at("players").get<std::vector<std::string>>();
    if (players == std::vector<std::string>{"player_1", "player_2"}) {
      sawFull = true;
    }
  }
  EXPECT_TRUE(sawFull);
}

TEST(Server, RoomBecomesReadyWhenTheSecondPlayerJoins) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  join(server, transport, 1);
  EXPECT_FALSE(server.isRoomReady());

  join(server, transport, 2);
  EXPECT_TRUE(server.isRoomReady());

  const std::vector<WsEnvelope> hostReady = transport.eventsTo(1, WSEvents::kReady);
  const std::vector<WsEnvelope> guestReady = transport.eventsTo(2, WSEvents::kReady);
  ASSERT_EQ(hostReady.size(), 1u);
  ASSERT_EQ(guestReady.size(), 1u);
  for (const WsEnvelope &ready : {hostReady.at(0), guestReady.at(0)}) {
    const std::vector<std::string> players =
        ready.payload.at("players").get<std::vector<std::string>>();
    ASSERT_EQ(players.size(), 2u);
    EXPECT_EQ(players.at(0), "player_1");
    EXPECT_EQ(players.at(1), "player_2");
  }
}

TEST(Server, RoomFullRejectsTheThirdPlayer) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  join(server, transport, 1);
  join(server, transport, 2);

  transport.simulateConnection(3);
  server.runOnce();

  const std::vector<WsEnvelope> errors = transport.eventsTo(3, WSEvents::kError);
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors.at(0).payload.at("code").get<std::string>(), WSErrorCodes::kRoomFull);
  EXPECT_TRUE(transport.eventsTo(3, WSEvents::kJoined).empty());
  // The rejected connection is closed and never seated.
  EXPECT_EQ(transport.closed().size(), 1u);
  EXPECT_EQ(transport.closed().at(0), 3u);
  EXPECT_EQ(server.playerCount(), 2u);
}

TEST(Server, ServerOverridesTheSpoofedSenderId) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  join(server, transport, 1);
  join(server, transport, 2);

  // player_1 claims to be player_2 — the relay must overwrite `from`.
  transport.simulateFrame(1,
                          envelope("move_card", Server::kDefaultRoom, "player_2", {{"id", "c2"}}));
  server.runOnce();

  const std::vector<WsEnvelope> relayed = transport.eventsTo(2, "move_card");
  ASSERT_EQ(relayed.size(), 1u);
  EXPECT_EQ(relayed.at(0).from, "player_1");
  EXPECT_EQ(relayed.at(0).payload.at("id").get<std::string>(), "c2");
}

TEST(Server, SenderReceivesItsOwnBroadcastEcho) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  join(server, transport, 1);

  transport.simulateFrame(1,
                          envelope("tap_card", Server::kDefaultRoom, "player_1", {{"id", "c1"}}));
  server.runOnce();

  const std::vector<WsEnvelope> echoed = transport.eventsTo(1, "tap_card");
  ASSERT_EQ(echoed.size(), 1u);
  EXPECT_EQ(echoed.at(0).from, "player_1");
  EXPECT_EQ(echoed.at(0).payload.at("id").get<std::string>(), "c1");
}

TEST(Server, InvalidJsonGetsAnErrorAndTheConnectionSurvives) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  join(server, transport, 1);

  transport.simulateFrame(1, "this is not json");
  server.runOnce();
  const std::vector<WsEnvelope> errors = transport.eventsTo(1, WSEvents::kError);
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors.at(0).payload.at("code").get<std::string>(), WSErrorCodes::kInvalidMessage);
  EXPECT_TRUE(transport.closed().empty());
  EXPECT_EQ(server.playerCount(), 1u);

  // A minimal envelope still parses as invalid (missing fields).
  transport.simulateFrame(1, "{}");
  server.runOnce();
  EXPECT_EQ(transport.eventsTo(1, WSEvents::kError).size(), 2u);
}

TEST(Server, RoomMismatchGetsAnError) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  join(server, transport, 1);

  transport.simulateFrame(1, envelope("move_card", "WRONG1", "player_1", {{"id", "c1"}}));
  server.runOnce();

  const std::vector<WsEnvelope> errors = transport.eventsTo(1, WSEvents::kError);
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors.at(0).payload.at("code").get<std::string>(), WSErrorCodes::kRoomMismatch);
}

TEST(Server, DisconnectBroadcastsPlayerLeftAndDropsReady) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  join(server, transport, 1);
  join(server, transport, 2);
  EXPECT_TRUE(server.isRoomReady());

  transport.simulateDisconnect(2);
  server.runOnce();

  EXPECT_FALSE(server.isRoomReady());
  const std::vector<WsEnvelope> left = transport.eventsTo(1, WSEvents::kPlayerLeft);
  ASSERT_EQ(left.size(), 1u);
  EXPECT_EQ(left.at(0).payload.at("player_id").get<std::string>(), "player_2");
  const std::vector<std::string> players =
      left.at(0).payload.at("players").get<std::vector<std::string>>();
  ASSERT_EQ(players.size(), 1u);
  EXPECT_EQ(players.at(0), "player_1");
}

TEST(Server, LastPlayerLeavingResetsTheRoom) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  join(server, transport, 1);

  transport.simulateDisconnect(1);
  server.runOnce();

  EXPECT_EQ(server.playerCount(), 0u);
  EXPECT_FALSE(server.hostId().has_value());
  EXPECT_FALSE(server.isRoomReady());

  // A new connection starts fresh as host again.
  join(server, transport, 5);
  const std::vector<WsEnvelope> joined = transport.eventsTo(5, WSEvents::kJoined);
  ASSERT_EQ(joined.size(), 1u);
  EXPECT_EQ(joined.at(0).payload.at("player_id").get<std::string>(), "player_1");
  EXPECT_EQ(joined.at(0).payload.at("role").get<std::string>(), "host");
}

TEST(Server, StartAndStopAreIdempotent) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  EXPECT_TRUE(transport.started());
  server.start();
  EXPECT_TRUE(transport.started());

  server.stop();
  EXPECT_TRUE(transport.stopped());
  server.stop();
  EXPECT_TRUE(transport.stopped());
}

} // namespace
} // namespace mtgcpp::net
