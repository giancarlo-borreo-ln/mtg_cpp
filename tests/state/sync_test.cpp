// M7.2 sync payload tests, mirroring the webapp's board-sync.spec.ts.

#include "state/sync.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
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

using mtgcpp::core::PlayerSeat;
using mtgcpp::core::PlayerZone;

TEST(ToBoardUpdatePayload, SerializesMoveCardToZone) {
  const std::optional<nlohmann::json> payload =
      toBoardUpdatePayload(moveCardToZone(PlayerSeat::Host, "host-3", PlayerZone::Creatures));

  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(expectValue(payload), (nlohmann::json{{"action", "moveCardToZone"},
                                                  {"seat", "host"},
                                                  {"cardId", "host-3"},
                                                  {"zone", "creatures"}}));
}

TEST(ToBoardUpdatePayload, SerializesMoveCardToStack) {
  const std::optional<nlohmann::json> payload = toBoardUpdatePayload(moveCardToStack("host-9"));

  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(expectValue(payload),
            (nlohmann::json{{"action", "moveCardToStack"}, {"cardId", "host-9"}}));
}

TEST(ToBoardUpdatePayload, SerializesTapCard) {
  const std::optional<nlohmann::json> payload =
      toBoardUpdatePayload(tapCard(PlayerSeat::Guest, "guest-2"));

  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(expectValue(payload),
            (nlohmann::json{{"action", "tapCard"}, {"seat", "guest"}, {"cardId", "guest-2"}}));
}

TEST(ToBoardUpdatePayload, SerializesAddCounter) {
  const std::optional<nlohmann::json> payload =
      toBoardUpdatePayload(addCounter(PlayerSeat::Host, "host-1"));

  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(expectValue(payload),
            (nlohmann::json{{"action", "addCounter"}, {"seat", "host"}, {"cardId", "host-1"}}));
}

TEST(ToBoardUpdatePayload, SerializesFlipCard) {
  const std::optional<nlohmann::json> payload =
      toBoardUpdatePayload(flipCard(PlayerSeat::Guest, "guest-4"));

  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(expectValue(payload),
            (nlohmann::json{{"action", "flipCard"}, {"seat", "guest"}, {"cardId", "guest-4"}}));
}

TEST(ToBoardUpdatePayload, SerializesCreateToken) {
  const std::optional<nlohmann::json> payload =
      toBoardUpdatePayload(createToken(PlayerSeat::Host, PlayerZone::Creatures, "Bears"));

  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(
      expectValue(payload),
      (nlohmann::json{
          {"action", "createToken"}, {"seat", "host"}, {"zone", "creatures"}, {"name", "Bears"}}));
}

TEST(ToBoardUpdatePayload, SerializesSetLife) {
  const std::optional<nlohmann::json> payload =
      toBoardUpdatePayload(setLife(PlayerSeat::Guest, 13));

  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(expectValue(payload),
            (nlohmann::json{{"action", "setLife"}, {"seat", "guest"}, {"life", 13}}));
}

TEST(ToBoardUpdatePayload, ReturnsNulloptForNonSyncableActions) {
  EXPECT_FALSE(toBoardUpdatePayload(setMyDeck(PlayerSeat::Host, {})).has_value());
  EXPECT_FALSE(toBoardUpdatePayload(setOpponentDeck(PlayerSeat::Guest, {})).has_value());
  EXPECT_FALSE(toBoardUpdatePayload(setSeatHand(PlayerSeat::Host, {})).has_value());
  EXPECT_FALSE(
      toBoardUpdatePayload(setSeatZone(PlayerSeat::Host, PlayerZone::Lands, {})).has_value());
  EXPECT_FALSE(toBoardUpdatePayload(pushToStack({})).has_value());
  EXPECT_FALSE(toBoardUpdatePayload(clearStack()).has_value());
  EXPECT_FALSE(toBoardUpdatePayload(clearBoard()).has_value());
}

TEST(BoardUpdateToAction, RebuildsMoveCardToZoneWithTheSyncMarker) {
  const std::optional<BoardAction> action = boardUpdateToAction({{"action", "moveCardToZone"},
                                                                 {"seat", "host"},
                                                                 {"cardId", "host-3"},
                                                                 {"zone", "creatures"}});

  ASSERT_TRUE(action.has_value());
  BoardAction expected = moveCardToZone(PlayerSeat::Host, "host-3", PlayerZone::Creatures);
  expected.sync = true;
  EXPECT_EQ(expectValue(action), expected);
}

TEST(BoardUpdateToAction, RebuildsMoveCardToStackWithTheSyncMarker) {
  const std::optional<BoardAction> action =
      boardUpdateToAction({{"action", "moveCardToStack"}, {"cardId", "host-9"}});

  ASSERT_TRUE(action.has_value());
  BoardAction expected = moveCardToStack("host-9");
  expected.sync = true;
  EXPECT_EQ(expectValue(action), expected);
}

TEST(BoardUpdateToAction, RebuildsTapAddCounterAndFlipWithTheSyncMarker) {
  const std::optional<BoardAction> tapped =
      boardUpdateToAction({{"action", "tapCard"}, {"seat", "guest"}, {"cardId", "guest-2"}});
  const std::optional<BoardAction> countered =
      boardUpdateToAction({{"action", "addCounter"}, {"seat", "host"}, {"cardId", "host-1"}});
  const std::optional<BoardAction> flipped =
      boardUpdateToAction({{"action", "flipCard"}, {"seat", "guest"}, {"cardId", "guest-4"}});

  ASSERT_TRUE(tapped.has_value());
  ASSERT_TRUE(countered.has_value());
  ASSERT_TRUE(flipped.has_value());
  EXPECT_EQ(expectValue(tapped).kind, BoardAction::Kind::TapCard);
  EXPECT_TRUE(expectValue(tapped).sync);
  EXPECT_EQ(expectValue(countered).kind, BoardAction::Kind::AddCounter);
  EXPECT_TRUE(expectValue(countered).sync);
  EXPECT_EQ(expectValue(flipped).kind, BoardAction::Kind::FlipCard);
  EXPECT_TRUE(expectValue(flipped).sync);
}

TEST(BoardUpdateToAction, RebuildsCreateTokenWithTheSyncMarker) {
  const std::optional<BoardAction> action = boardUpdateToAction(
      {{"action", "createToken"}, {"seat", "host"}, {"zone", "creatures"}, {"name", "Bears"}});

  ASSERT_TRUE(action.has_value());
  BoardAction expected = createToken(PlayerSeat::Host, PlayerZone::Creatures, "Bears");
  expected.sync = true;
  EXPECT_EQ(expectValue(action), expected);
}

TEST(BoardUpdateToAction, RebuildsSetLifeWithTheSyncMarker) {
  const std::optional<BoardAction> action =
      boardUpdateToAction({{"action", "setLife"}, {"seat", "guest"}, {"life", 13}});

  ASSERT_TRUE(action.has_value());
  BoardAction expected = setLife(PlayerSeat::Guest, 13);
  expected.sync = true;
  EXPECT_EQ(expectValue(action), expected);
}

TEST(BoardUpdateToAction, RoundTripsEverySyncableActionPayload) {
  const std::vector<BoardAction> actions = {
      moveCardToZone(PlayerSeat::Host, "host-3", PlayerZone::Creatures),
      moveCardToStack("host-9"),
      tapCard(PlayerSeat::Guest, "guest-2"),
      addCounter(PlayerSeat::Host, "host-1"),
      flipCard(PlayerSeat::Guest, "guest-4"),
      createToken(PlayerSeat::Host, PlayerZone::Lands, "Forest"),
      setLife(PlayerSeat::Host, 19),
  };

  for (const BoardAction &action : actions) {
    const std::optional<nlohmann::json> payload = toBoardUpdatePayload(action);
    ASSERT_TRUE(payload.has_value());
    const std::optional<BoardAction> rebuilt = boardUpdateToAction(expectValue(payload));
    ASSERT_TRUE(rebuilt.has_value());
    BoardAction expected = action;
    expected.sync = true;
    EXPECT_EQ(expectValue(rebuilt), expected);
  }
}

TEST(BoardUpdateToAction, ReturnsNulloptForAnUnknownActionName) {
  EXPECT_FALSE(boardUpdateToAction({{"action", "nonsense"}}).has_value());
  EXPECT_FALSE(boardUpdateToAction(nlohmann::json::object()).has_value());
}

TEST(BoardUpdateToAction, RejectsPayloadsWithAnInvalidSeat) {
  EXPECT_FALSE(boardUpdateToAction({{"action", "tapCard"}, {"seat", "spectator"}, {"cardId", "c1"}})
                   .has_value());
  EXPECT_FALSE(boardUpdateToAction({{"action", "tapCard"}, {"cardId", "c1"}}).has_value());
}

TEST(BoardUpdateToAction, RejectsPayloadsWithAMissingCardId) {
  EXPECT_FALSE(boardUpdateToAction({{"action", "tapCard"}, {"seat", "host"}}).has_value());
  EXPECT_FALSE(
      boardUpdateToAction({{"action", "tapCard"}, {"seat", "host"}, {"cardId", ""}}).has_value());
}

TEST(BoardUpdateToAction, RejectsPayloadsWithAnInvalidZone) {
  EXPECT_FALSE(
      boardUpdateToAction(
          {{"action", "moveCardToZone"}, {"seat", "host"}, {"cardId", "c1"}, {"zone", "nowhere"}})
          .has_value());
}

TEST(BoardUpdateToAction, RejectsPayloadsWithANonNumericLife) {
  EXPECT_FALSE(
      boardUpdateToAction({{"action", "setLife"}, {"seat", "host"}, {"life", "20"}}).has_value());
}

TEST(BoardUpdateToAction, RejectsCreateTokenWithoutAName) {
  EXPECT_FALSE(
      boardUpdateToAction({{"action", "createToken"}, {"seat", "host"}, {"zone", "creatures"}})
          .has_value());
}

} // namespace
} // namespace mtgcpp::state
