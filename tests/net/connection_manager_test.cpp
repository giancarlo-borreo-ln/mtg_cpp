// M6.3 connection manager tests, mirroring the webapp's test_ws_manager.py.

#include "net/connection_manager.h"

#include <gtest/gtest.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mtgcpp::net {
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

TEST(ConnectionManager, DefaultMaxPlayersIsTwo) {
  ConnectionManager manager;
  EXPECT_FALSE(manager.isRoomFull("ABC12"));
  ASSERT_TRUE(manager.connect("ABC12", 1).has_value());
  ASSERT_TRUE(manager.connect("ABC12", 2).has_value());
  EXPECT_TRUE(manager.isRoomFull("ABC12"));
}

TEST(ConnectionManager, AssignsSeatsInArrivalOrder) {
  ConnectionManager manager;
  const std::optional<std::string> first = manager.connect("ABC12", 1);
  const std::optional<std::string> second = manager.connect("ABC12", 2);
  EXPECT_EQ(expectValue(first), "player_1");
  EXPECT_EQ(expectValue(second), "player_2");
}

TEST(ConnectionManager, ConnectsAreIsolatedPerRoom) {
  ConnectionManager manager;
  const std::optional<std::string> a = manager.connect("AAA11", 1);
  const std::optional<std::string> b = manager.connect("BBB22", 2);
  const std::optional<std::string> c = manager.connect("AAA11", 3);
  EXPECT_EQ(expectValue(a), "player_1");
  EXPECT_EQ(expectValue(b), "player_1");
  EXPECT_EQ(expectValue(c), "player_2");
}

TEST(ConnectionManager, RoomFullRejectsTheThirdConnection) {
  ConnectionManager manager;
  ASSERT_TRUE(manager.connect("ABC12", 1).has_value());
  ASSERT_TRUE(manager.connect("ABC12", 2).has_value());
  EXPECT_FALSE(manager.connect("ABC12", 3).has_value());
}

TEST(ConnectionManager, HonorsACustomMaxPlayers) {
  ConnectionManager manager(3);
  ASSERT_TRUE(manager.connect("ABC12", 1).has_value());
  ASSERT_TRUE(manager.connect("ABC12", 2).has_value());
  ASSERT_TRUE(manager.connect("ABC12", 3).has_value());
  EXPECT_FALSE(manager.connect("ABC12", 4).has_value());
}

TEST(ConnectionManager, TracksPlayerCountAndRoomFull) {
  ConnectionManager manager;
  EXPECT_EQ(manager.playerCount("ABC12"), 0u);
  EXPECT_FALSE(manager.isRoomFull("ABC12"));

  manager.connect("ABC12", 1);
  EXPECT_EQ(manager.playerCount("ABC12"), 1u);
  EXPECT_FALSE(manager.isRoomFull("ABC12"));

  manager.connect("ABC12", 2);
  EXPECT_EQ(manager.playerCount("ABC12"), 2u);
  EXPECT_TRUE(manager.isRoomFull("ABC12"));
}

TEST(ConnectionManager, DisconnectReturnsTheFreedPlayerId) {
  ConnectionManager manager;
  manager.connect("ABC12", 1);
  manager.connect("ABC12", 2);

  const std::optional<std::string> freed = manager.disconnect("ABC12", 1);

  EXPECT_EQ(expectValue(freed), "player_1");
  const std::vector<std::string> remaining = manager.playerIds("ABC12");
  ASSERT_EQ(remaining.size(), 1u);
  EXPECT_EQ(remaining.at(0), "player_2");
}

TEST(ConnectionManager, DisconnectOfAnUnknownConnectionIsANoOp) {
  ConnectionManager manager;
  manager.connect("ABC12", 1);
  EXPECT_FALSE(manager.disconnect("ABC12", 999).has_value());
  EXPECT_FALSE(manager.disconnect("NOPE1", 1).has_value());
}

TEST(ConnectionManager, DisconnectOfTheLastPlayerRemovesTheRoom) {
  ConnectionManager manager;
  manager.connect("ABC12", 1);
  EXPECT_EQ(manager.roomCodes().size(), 1u);
  manager.disconnect("ABC12", 1);
  EXPECT_EQ(manager.roomCodes().size(), 0u);
}

TEST(ConnectionManager, ReconnectAfterDisconnectFillsTheFirstSeat) {
  ConnectionManager manager;
  manager.connect("ABC12", 1);
  manager.connect("ABC12", 2);
  manager.disconnect("ABC12", 1);

  const std::optional<std::string> replacement = manager.connect("ABC12", 3);

  EXPECT_EQ(expectValue(replacement), "player_1");
}

TEST(ConnectionManager, ListsSocketsAndPlayerIdsInArrivalOrder) {
  ConnectionManager manager;
  manager.connect("ABC12", 1);
  manager.connect("ABC12", 2);

  const std::vector<std::size_t> sockets = manager.sockets("ABC12");
  const std::vector<std::string> ids = manager.playerIds("ABC12");
  ASSERT_EQ(sockets.size(), 2u);
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(sockets.at(0), 1u);
  EXPECT_EQ(sockets.at(1), 2u);
  EXPECT_EQ(ids.at(0), "player_1");
  EXPECT_EQ(ids.at(1), "player_2");
  EXPECT_TRUE(manager.sockets("NOPE1").empty());
  EXPECT_TRUE(manager.playerIds("NOPE1").empty());
}

TEST(ConnectionManager, SocketsForPlayerFiltersBySeat) {
  ConnectionManager manager;
  manager.connect("ABC12", 1);
  manager.connect("ABC12", 2);

  const std::vector<std::size_t> host = manager.socketsForPlayer("ABC12", "player_1");
  const std::vector<std::size_t> guest = manager.socketsForPlayer("ABC12", "player_2");
  ASSERT_EQ(host.size(), 1u);
  ASSERT_EQ(guest.size(), 1u);
  EXPECT_EQ(host.at(0), 1u);
  EXPECT_EQ(guest.at(0), 2u);
  EXPECT_TRUE(manager.socketsForPlayer("ABC12", "player_3").empty());
}

TEST(ConnectionManager, PlayerIdForMapsAConnectionBackToItsSeat) {
  ConnectionManager manager;
  manager.connect("ABC12", 1);
  manager.connect("ABC12", 2);

  const std::optional<std::string> first = manager.playerIdFor("ABC12", 1);
  const std::optional<std::string> second = manager.playerIdFor("ABC12", 2);
  EXPECT_EQ(expectValue(first), "player_1");
  EXPECT_EQ(expectValue(second), "player_2");
  EXPECT_FALSE(manager.playerIdFor("ABC12", 3).has_value());
}

TEST(ConnectionManager, RoomCodesReflectsActiveRooms) {
  ConnectionManager manager;
  manager.connect("AAA11", 1);
  manager.connect("BBB22", 2);
  manager.connect("CCC33", 3);

  const std::vector<std::string> codes = manager.roomCodes();
  const std::set<std::string> codeSet(codes.begin(), codes.end());
  EXPECT_EQ(codeSet, (std::set<std::string>{"AAA11", "BBB22", "CCC33"}));
}

TEST(ConnectionManager, ResetClearsAllRooms) {
  ConnectionManager manager;
  manager.connect("AAA11", 1);
  manager.connect("BBB22", 2);
  manager.reset();
  EXPECT_TRUE(manager.roomCodes().empty());
}

} // namespace
} // namespace mtgcpp::net
