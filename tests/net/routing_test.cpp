// M6.4 routing tests against a fake transport: every event is routed to the
// right private target, board updates / deck picks are relayed verbatim, the
// hand-reveal flow is transformed server-side, and teardown on leave works.

#include "net/envelope.h"
#include "net/server.h"
#include "net/transport.h"

#include "fake_transport.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace mtgcpp::net {
namespace {

std::string envelope(std::string_view event, std::string_view room, std::string_view from,
                     nlohmann::json payload) {
  return serializeEnvelope(
      WsEnvelope{std::string(event), std::string(room), std::string(from), std::move(payload)});
}

// An empty-payload envelope (nlohmann's `{}` is JSON null, not an object).
std::string emptyPayloadEnvelope(std::string_view event, std::string_view room,
                                 std::string_view from) {
  return envelope(event, room, from, nlohmann::json::object());
}

// A two-player room, returning the two connection ids.
struct Room {
  std::size_t first;
  std::size_t second;
};

Room twoPlayerRoom(Server &server, FakeTransport &transport) {
  transport.simulateConnection(1);
  server.runOnce();
  transport.simulateConnection(2);
  server.runOnce();
  return Room{1, 2};
}

TEST(Routing, HandRevealRequestRoutesToTheOpponentOnly) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  twoPlayerRoom(server, transport);

  transport.simulateFrame(
      1, emptyPayloadEnvelope(WSEvents::kRequestHandReveal, Server::kDefaultRoom, "player_1"));
  server.runOnce();

  const std::vector<WsEnvelope> prompts = transport.eventsTo(2, WSEvents::kHandRevealRequest);
  ASSERT_EQ(prompts.size(), 1u);
  EXPECT_EQ(prompts.at(0).from, "player_1");
  EXPECT_EQ(prompts.at(0).payload.at("from").get<std::string>(), "player_1");

  // The requester never echoes the request back to itself.
  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kHandRevealRequest).empty());
  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kRequestHandReveal).empty());
}

TEST(Routing, HandRevealAcceptRoutesTheCardsToTheRequesterOnly) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  twoPlayerRoom(server, transport);

  const nlohmann::json cards = {
      {{"id", "c1"}, {"name", "Lightning Bolt"}, {"image_url", "https://img/bolt"}},
      {{"id", "c2"}, {"name", "Forest"}, {"image_url", "https://img/forest"}}};
  transport.simulateFrame(2, envelope(WSEvents::kHandRevealAccept, Server::kDefaultRoom, "player_2",
                                      {{"cards", cards}}));
  server.runOnce();

  const std::vector<WsEnvelope> results = transport.eventsTo(1, WSEvents::kHandRevealResult);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results.at(0).from, "player_2");
  EXPECT_EQ(results.at(0).payload, (nlohmann::json{{"accepted", true}, {"cards", cards}}));

  EXPECT_TRUE(transport.eventsTo(2, WSEvents::kHandRevealResult).empty());
  EXPECT_TRUE(transport.eventsTo(2, WSEvents::kHandRevealAccept).empty());
}

TEST(Routing, HandRevealDenyRoutesAnAcceptedFalseResult) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  twoPlayerRoom(server, transport);

  transport.simulateFrame(
      2, emptyPayloadEnvelope(WSEvents::kHandRevealDeny, Server::kDefaultRoom, "player_2"));
  server.runOnce();

  const std::vector<WsEnvelope> results = transport.eventsTo(1, WSEvents::kHandRevealResult);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results.at(0).payload, (nlohmann::json{{"accepted", false}}));
  EXPECT_FALSE(results.at(0).payload.contains("cards"));
}

TEST(Routing, HandRevealWithoutAnOpponentReturnsNoTarget) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  transport.simulateConnection(1);
  server.runOnce();

  transport.simulateFrame(
      1, emptyPayloadEnvelope(WSEvents::kRequestHandReveal, Server::kDefaultRoom, "player_1"));
  server.runOnce();

  const std::vector<WsEnvelope> errors = transport.eventsTo(1, WSEvents::kError);
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors.at(0).payload.at("code").get<std::string>(), WSErrorCodes::kNoTarget);
  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kHandRevealRequest).empty());
}

TEST(Routing, BoardUpdateRelaysToTheOpponentOnly) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  twoPlayerRoom(server, transport);

  const nlohmann::json payload = {{"action", "tapCard"}, {"seat", "host"}, {"cardId", "host-3"}};
  transport.simulateFrame(
      1, envelope(WSEvents::kBoardUpdate, Server::kDefaultRoom, "player_1", payload));
  server.runOnce();

  const std::vector<WsEnvelope> updates = transport.eventsTo(2, WSEvents::kBoardUpdate);
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates.at(0).room, Server::kDefaultRoom);
  EXPECT_EQ(updates.at(0).from, "player_1");
  EXPECT_EQ(updates.at(0).payload, payload);
  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kBoardUpdate).empty());
}

TEST(Routing, BoardUpdatePayloadIsRelayedVerbatim) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  twoPlayerRoom(server, transport);

  const nlohmann::json payload = {{"action", "moveCardToZone"},
                                  {"seat", "guest"},
                                  {"cardId", "guest-5"},
                                  {"zone", "creatures"},
                                  {"meta", {{"nested", {1, 2, 3}}}}};
  transport.simulateFrame(
      1, envelope(WSEvents::kBoardUpdate, Server::kDefaultRoom, "player_1", payload));
  server.runOnce();

  const std::vector<WsEnvelope> updates = transport.eventsTo(2, WSEvents::kBoardUpdate);
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates.at(0).payload, payload);
}

TEST(Routing, BoardUpdateWithNoOpponentIsDroppedSilently) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  transport.simulateConnection(1);
  server.runOnce();

  transport.simulateFrame(1, envelope(WSEvents::kBoardUpdate, Server::kDefaultRoom, "player_1",
                                      {{"action", "tapCard"}}));
  server.runOnce();

  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kBoardUpdate).empty());
  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kError).empty());
  EXPECT_TRUE(transport.closed().empty());
}

TEST(Routing, DeckSelectedRelaysToTheOpponentOnly) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  twoPlayerRoom(server, transport);

  const nlohmann::json payload = {
      {"seat", "host"},
      {"deck", {{"name", "Host Izzet"}, {"cards", {{{"name", "Opt"}, {"quantity", 4}}}}}}};
  transport.simulateFrame(
      1, envelope(WSEvents::kDeckSelected, Server::kDefaultRoom, "player_1", payload));
  server.runOnce();

  const std::vector<WsEnvelope> selections = transport.eventsTo(2, WSEvents::kDeckSelected);
  ASSERT_EQ(selections.size(), 1u);
  EXPECT_EQ(selections.at(0).room, Server::kDefaultRoom);
  EXPECT_EQ(selections.at(0).from, "player_1");
  EXPECT_EQ(selections.at(0).payload, payload);
  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kDeckSelected).empty());
}

TEST(Routing, DeckSelectedWithNoOpponentIsDroppedSilently) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  transport.simulateConnection(1);
  server.runOnce();

  transport.simulateFrame(
      1, envelope(WSEvents::kDeckSelected, Server::kDefaultRoom, "player_1", {{"seat", "host"}}));
  server.runOnce();

  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kDeckSelected).empty());
  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kError).empty());
}

TEST(Routing, TeardownOnLeaveNotifiesTheRemainingPlayer) {
  FakeTransport transport;
  Server server(transport);
  server.start();
  const Room room = twoPlayerRoom(server, transport);

  transport.simulateDisconnect(room.second);
  server.runOnce();

  const std::vector<WsEnvelope> left = transport.eventsTo(1, WSEvents::kPlayerLeft);
  ASSERT_EQ(left.size(), 1u);
  EXPECT_EQ(left.at(0).payload.at("player_id").get<std::string>(), "player_2");
  const std::vector<std::string> players =
      left.at(0).payload.at("players").get<std::vector<std::string>>();
  ASSERT_EQ(players.size(), 1u);
  EXPECT_EQ(players.at(0), "player_1");

  // After the leave the remaining player is solo: private relays are dropped.
  transport.simulateFrame(1, envelope(WSEvents::kBoardUpdate, Server::kDefaultRoom, "player_1",
                                      {{"action", "tapCard"}}));
  server.runOnce();
  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kBoardUpdate).empty());
  EXPECT_TRUE(transport.eventsTo(1, WSEvents::kError).empty());
}

TEST(Routing, PrivateRelaysNeverLeakBetweenRooms) {
  // A second server acts as an isolated "room": nothing leaks across instances.
  FakeTransport firstTransport;
  Server first(firstTransport);
  first.start();
  twoPlayerRoom(first, firstTransport);

  FakeTransport secondTransport;
  Server second(secondTransport);
  second.start();
  twoPlayerRoom(second, secondTransport);

  firstTransport.simulateFrame(1, envelope(WSEvents::kBoardUpdate, Server::kDefaultRoom, "player_1",
                                           {{"action", "tapCard"}}));
  first.runOnce();

  EXPECT_EQ(firstTransport.eventsTo(2, WSEvents::kBoardUpdate).size(), 1u);
  EXPECT_TRUE(secondTransport.eventsTo(2, WSEvents::kBoardUpdate).empty());
  EXPECT_TRUE(secondTransport.eventsTo(1, WSEvents::kBoardUpdate).empty());
}

} // namespace
} // namespace mtgcpp::net
