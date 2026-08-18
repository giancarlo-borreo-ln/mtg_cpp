// M7.1 board state reducer tests, mirroring the webapp's board.reducer.spec.ts.

#include "state/board_state.h"

#include <gtest/gtest.h>

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

using mtgcpp::core::BoardCard;
using mtgcpp::core::Deck;
using mtgcpp::core::kStartingLife;
using mtgcpp::core::mintBoardCards;
using mtgcpp::core::PlayerSeat;
using mtgcpp::core::PlayerZone;

// Deck fixture mirroring the spec's: 8 Bolt (Instant), 8 Bears (Creature),
// 8 Forest (Land).
std::vector<mtgcpp::core::Card> deckCards() {
  auto bolt = mtgcpp::core::Card{};
  bolt.name = "Bolt";
  bolt.set_code = "sta";
  bolt.set_name = "Test Set";
  bolt.collector_number = "1";
  bolt.quantity = 8;
  bolt.mana_cost = "{R}";
  bolt.cmc = 1.0f;
  bolt.type_line = "Instant";
  bolt.image_uris.emplace("png", "https://img/bolt.png");

  auto bears = mtgcpp::core::Card{};
  bears.name = "Bears";
  bears.set_code = "sta";
  bears.set_name = "Test Set";
  bears.collector_number = "2";
  bears.quantity = 8;
  bears.mana_cost = "{1}{G}";
  bears.cmc = 2.0f;
  bears.type_line = "Creature — Bear";
  bears.image_uris.emplace("png", "https://img/bears.png");

  auto forest = mtgcpp::core::Card{};
  forest.name = "Forest";
  forest.set_code = "sta";
  forest.set_name = "Test Set";
  forest.collector_number = "3";
  forest.quantity = 8;
  forest.type_line = "Basic Land — Forest";
  forest.image_uris.emplace("png", "https://img/forest.png");

  return {std::move(bolt), std::move(bears), std::move(forest)};
}

Deck makeDeck() {
  Deck deck;
  deck.id = "d1";
  deck.name = "Deck";
  deck.format = "Other";
  deck.total_cards = 24;
  deck.unique_cards = 3;
  deck.created_at = "2026-01-01T00:00:00Z";
  deck.updated_at = "2026-01-01T00:00:00Z";
  deck.cards = deckCards();
  return deck;
}

std::vector<BoardCard> cardsFor(PlayerSeat seat) { return mintBoardCards(deckCards(), seat); }

// A board with a hand of two cards and one card in the lands zone.
BoardState seeded(PlayerSeat seat) {
  const std::vector<BoardCard> cards = cardsFor(seat);
  BoardState state = initialBoardState();
  state = applyAction(state, setSeatHand(seat, {cards.at(0), cards.at(1)}));
  state = applyAction(state, setSeatZone(seat, PlayerZone::Lands, {cards.at(2)}));
  return state;
}

TEST(BoardState, InitialStateHasEmptySeatsAndStartingLife) {
  const BoardState state = initialBoardState();

  EXPECT_TRUE(state.seats.at(seatIndex(PlayerSeat::Host)).hand.empty());
  EXPECT_TRUE(state.seats.at(seatIndex(PlayerSeat::Guest)).hand.empty());
  EXPECT_EQ(
      state.seats.at(seatIndex(PlayerSeat::Host)).zones.at(zoneIndex(PlayerZone::Lands)).size(),
      0u);
  EXPECT_TRUE(state.stack.empty());
  EXPECT_EQ(state.life.at(seatIndex(PlayerSeat::Host)), kStartingLife);
  EXPECT_EQ(state.life.at(seatIndex(PlayerSeat::Guest)), kStartingLife);
  EXPECT_FALSE(state.my_deck.has_value());
  EXPECT_FALSE(state.their_deck.has_value());
}

TEST(BoardState, PushToStackPreservesPlayOrder) {
  const std::vector<BoardCard> cards = cardsFor(PlayerSeat::Host);
  BoardState state = initialBoardState();
  state = applyAction(state, pushToStack(cards.at(0)));
  state = applyAction(state, pushToStack(cards.at(1)));

  ASSERT_EQ(state.stack.size(), 2u);
  EXPECT_EQ(state.stack.at(0).id, cards.at(0).id);
  EXPECT_EQ(state.stack.at(1).id, cards.at(1).id);
}

TEST(BoardState, ClearStackEmptiesTheStack) {
  BoardState state =
      applyAction(initialBoardState(), pushToStack(cardsFor(PlayerSeat::Host).at(0)));
  state = applyAction(state, clearStack());
  EXPECT_TRUE(state.stack.empty());
}

TEST(BoardState, SetLifeTouchesOnlyOneSeat) {
  const BoardState state = applyAction(initialBoardState(), setLife(PlayerSeat::Host, 30));

  EXPECT_EQ(state.life.at(seatIndex(PlayerSeat::Host)), 30);
  EXPECT_EQ(state.life.at(seatIndex(PlayerSeat::Guest)), kStartingLife);
}

TEST(BoardState, SetSeatHandOnlyTouchesThatSeat) {
  const std::vector<BoardCard> cards = cardsFor(PlayerSeat::Host);
  const BoardState state = applyAction(initialBoardState(), setSeatHand(PlayerSeat::Host, cards));

  EXPECT_EQ(state.seats.at(seatIndex(PlayerSeat::Host)).hand, cards);
  EXPECT_TRUE(state.seats.at(seatIndex(PlayerSeat::Guest)).hand.empty());
}

TEST(BoardState, SetSeatZoneLeavesHandAndOtherZonesIntact) {
  const std::vector<BoardCard> creatures = cardsFor(PlayerSeat::Guest);
  std::vector<BoardCard> two;
  two.push_back(creatures.at(0));
  two.push_back(creatures.at(1));

  const BoardState state =
      applyAction(initialBoardState(), setSeatZone(PlayerSeat::Guest, PlayerZone::Creatures, two));

  const SeatBoard &guest = state.seats.at(seatIndex(PlayerSeat::Guest));
  EXPECT_EQ(guest.zones.at(zoneIndex(PlayerZone::Creatures)), two);
  EXPECT_TRUE(guest.zones.at(zoneIndex(PlayerZone::Lands)).empty());
  EXPECT_TRUE(guest.hand.empty());
  EXPECT_EQ(state.seats.at(seatIndex(PlayerSeat::Host)),
            initialBoardState().seats.at(seatIndex(PlayerSeat::Host)));
}

TEST(BoardState, SetSeatZoneKeepsOtherZonesOfTheSameSeatIntact) {
  const std::vector<BoardCard> all = cardsFor(PlayerSeat::Host);
  std::vector<BoardCard> creatures;
  creatures.push_back(all.at(0));
  creatures.push_back(all.at(1));
  std::vector<BoardCard> lands;
  lands.push_back(all.at(2));

  BoardState state = applyAction(initialBoardState(),
                                 setSeatZone(PlayerSeat::Host, PlayerZone::Creatures, creatures));
  state = applyAction(state, setSeatZone(PlayerSeat::Host, PlayerZone::Lands, lands));

  EXPECT_EQ(state.seats.at(seatIndex(PlayerSeat::Host)).zones.at(zoneIndex(PlayerZone::Creatures)),
            creatures);
  EXPECT_EQ(state.seats.at(seatIndex(PlayerSeat::Host)).zones.at(zoneIndex(PlayerZone::Lands)),
            lands);
}

TEST(BoardState, SetMyDeckMintsTheSeatFromTheDeck) {
  const Deck deck = makeDeck();
  const BoardState state = applyAction(initialBoardState(), setMyDeck(PlayerSeat::Host, deck));

  ASSERT_TRUE(state.my_deck.has_value());
  EXPECT_EQ(expectValue(state.my_deck), deck);
  EXPECT_FALSE(state.their_deck.has_value());

  const SeatBoard &host = state.seats.at(seatIndex(PlayerSeat::Host));
  EXPECT_EQ(host.hand.size(), 7u);
  EXPECT_EQ(host.zones.at(zoneIndex(PlayerZone::Creatures)).size(), 8u);
  EXPECT_EQ(host.zones.at(zoneIndex(PlayerZone::Lands)).size(), 8u);
  EXPECT_EQ(host.zones.at(zoneIndex(PlayerZone::InstantsSorceries)).size(), 1u);
  EXPECT_TRUE(host.zones.at(zoneIndex(PlayerZone::Graveyard)).empty());
  EXPECT_TRUE(host.zones.at(zoneIndex(PlayerZone::Exile)).empty());
  EXPECT_EQ(state.seats.at(seatIndex(PlayerSeat::Guest)),
            initialBoardState().seats.at(seatIndex(PlayerSeat::Guest)));
}

TEST(BoardState, SetOpponentDeckMintsTheGuestSeat) {
  const Deck deck = makeDeck();
  const BoardState state =
      applyAction(initialBoardState(), setOpponentDeck(PlayerSeat::Guest, deck));

  ASSERT_TRUE(state.their_deck.has_value());
  EXPECT_EQ(expectValue(state.their_deck), deck);
  EXPECT_FALSE(state.my_deck.has_value());

  const SeatBoard &guest = state.seats.at(seatIndex(PlayerSeat::Guest));
  EXPECT_EQ(guest.hand.size(), 7u);
  EXPECT_EQ(guest.zones.at(zoneIndex(PlayerZone::Creatures)).size(), 8u);
  EXPECT_EQ(guest.zones.at(zoneIndex(PlayerZone::Lands)).size(), 8u);
  EXPECT_EQ(state.seats.at(seatIndex(PlayerSeat::Host)),
            initialBoardState().seats.at(seatIndex(PlayerSeat::Host)));
}

TEST(BoardState, ClearBoardResetsToTheInitialState) {
  BoardState state =
      applyAction(initialBoardState(), pushToStack(cardsFor(PlayerSeat::Host).at(0)));
  state = applyAction(state, setLife(PlayerSeat::Guest, 1));
  state = applyAction(state, setMyDeck(PlayerSeat::Host, makeDeck()));

  const BoardState cleared = applyAction(state, clearBoard());

  EXPECT_EQ(cleared, initialBoardState());
}

TEST(BoardState, MoveCardToZoneMovesAHandCardIntoTheZone) {
  const BoardState state = seeded(PlayerSeat::Host);
  const std::string cardId = state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).id;

  const BoardState moved =
      applyAction(state, moveCardToZone(PlayerSeat::Host, cardId, PlayerZone::Creatures));

  const SeatBoard &host = moved.seats.at(seatIndex(PlayerSeat::Host));
  ASSERT_EQ(host.hand.size(), 1u);
  EXPECT_EQ(host.hand.at(0).id, state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(1).id);
  ASSERT_EQ(host.zones.at(zoneIndex(PlayerZone::Creatures)).size(), 1u);
  EXPECT_EQ(host.zones.at(zoneIndex(PlayerZone::Creatures)).at(0).id, cardId);
  EXPECT_EQ(host.zones.at(zoneIndex(PlayerZone::Lands)).size(), 1u);
}

TEST(BoardState, MoveCardToZoneMovesBetweenZonesAndRemovesFromTheOld) {
  const BoardState state = seeded(PlayerSeat::Host);
  const BoardState toCreatures =
      applyAction(state, moveCardToZone(PlayerSeat::Host,
                                        state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).id,
                                        PlayerZone::Creatures));
  const std::string creatureId = toCreatures.seats.at(seatIndex(PlayerSeat::Host))
                                     .zones.at(zoneIndex(PlayerZone::Creatures))
                                     .at(0)
                                     .id;

  const BoardState moved =
      applyAction(toCreatures, moveCardToZone(PlayerSeat::Host, creatureId, PlayerZone::Lands));

  const SeatBoard &host = moved.seats.at(seatIndex(PlayerSeat::Host));
  EXPECT_TRUE(host.zones.at(zoneIndex(PlayerZone::Creatures)).empty());
  EXPECT_EQ(host.zones.at(zoneIndex(PlayerZone::Lands)).size(), 2u);
}

TEST(BoardState, MoveCardToZoneTouchesOnlyTheGivenSeat) {
  const BoardState state = seeded(PlayerSeat::Host);
  const std::string cardId = state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).id;

  const BoardState moved =
      applyAction(state, moveCardToZone(PlayerSeat::Host, cardId, PlayerZone::Creatures));

  EXPECT_EQ(moved.seats.at(seatIndex(PlayerSeat::Guest)),
            state.seats.at(seatIndex(PlayerSeat::Guest)));
}

TEST(BoardState, MoveCardToZoneWithAnUnknownIdIsANoOp) {
  const BoardState state = seeded(PlayerSeat::Host);
  const BoardState moved =
      applyAction(state, moveCardToZone(PlayerSeat::Host, "ghost", PlayerZone::Creatures));

  EXPECT_EQ(moved, state);
}

TEST(BoardState, MoveCardToZoneScopesToTheGivenSeat) {
  const BoardState state = seeded(PlayerSeat::Host);
  const std::string hostCard = state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).id;

  const BoardState moved =
      applyAction(state, moveCardToZone(PlayerSeat::Guest, hostCard, PlayerZone::Creatures));

  EXPECT_EQ(moved, state); // the host's card is not on the guest seat
}

TEST(BoardState, MoveCardToStackRemovesTheCardAndPushesItOntoTheStack) {
  const BoardState state = seeded(PlayerSeat::Host);
  const std::string cardId = state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).id;

  const BoardState moved = applyAction(state, moveCardToStack(cardId));

  const SeatBoard &host = moved.seats.at(seatIndex(PlayerSeat::Host));
  ASSERT_EQ(host.hand.size(), 1u);
  EXPECT_EQ(host.hand.at(0).id, state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(1).id);
  ASSERT_EQ(moved.stack.size(), 1u);
  EXPECT_EQ(moved.stack.at(0).id, cardId);
}

TEST(BoardState, MoveCardToStackFindsTheCardOnEitherSeat) {
  const BoardState state = seeded(PlayerSeat::Guest);
  const std::string cardId =
      state.seats.at(seatIndex(PlayerSeat::Guest)).zones.at(zoneIndex(PlayerZone::Lands)).at(0).id;

  const BoardState moved = applyAction(state, moveCardToStack(cardId));

  EXPECT_TRUE(
      moved.seats.at(seatIndex(PlayerSeat::Guest)).zones.at(zoneIndex(PlayerZone::Lands)).empty());
  ASSERT_EQ(moved.stack.size(), 1u);
  EXPECT_EQ(moved.stack.at(0).id, cardId);
}

TEST(BoardState, MoveCardToStackAppendsInPlayOrder) {
  const BoardState state = seeded(PlayerSeat::Host);
  const std::string first = state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).id;
  const std::string second = state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(1).id;

  const BoardState one = applyAction(state, moveCardToStack(first));
  const BoardState moved = applyAction(one, moveCardToStack(second));

  ASSERT_EQ(moved.stack.size(), 2u);
  EXPECT_EQ(moved.stack.at(0).id, first);
  EXPECT_EQ(moved.stack.at(1).id, second);
}

TEST(BoardState, MoveCardToStackWithAnUnknownIdIsANoOp) {
  const BoardState state = seeded(PlayerSeat::Host);
  const BoardState moved = applyAction(state, moveCardToStack("ghost"));

  EXPECT_EQ(moved, state);
}

TEST(BoardState, TapCardTogglesAHandCard) {
  const BoardState state = seeded(PlayerSeat::Host);
  const std::string cardId = state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).id;

  const BoardState tapped = applyAction(state, tapCard(PlayerSeat::Host, cardId));

  EXPECT_TRUE(tapped.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).tapped);
  EXPECT_EQ(tapped.seats.at(seatIndex(PlayerSeat::Guest)),
            state.seats.at(seatIndex(PlayerSeat::Guest)));
}

TEST(BoardState, TapCardTogglesAZoneCard) {
  const BoardState state = seeded(PlayerSeat::Host);
  const std::string cardId =
      state.seats.at(seatIndex(PlayerSeat::Host)).zones.at(zoneIndex(PlayerZone::Lands)).at(0).id;

  const BoardState tapped = applyAction(state, tapCard(PlayerSeat::Host, cardId));

  EXPECT_TRUE(tapped.seats.at(seatIndex(PlayerSeat::Host))
                  .zones.at(zoneIndex(PlayerZone::Lands))
                  .at(0)
                  .tapped);
}

TEST(BoardState, AddCounterIncrementsStepByStep) {
  const BoardState state = seeded(PlayerSeat::Host);
  const std::string cardId = state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).id;

  const BoardState one = applyAction(state, addCounter(PlayerSeat::Host, cardId));
  const BoardState two = applyAction(one, addCounter(PlayerSeat::Host, cardId));

  EXPECT_EQ(two.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).counters, 2);
}

TEST(BoardState, FlipCardFlipsToTheBackside) {
  const BoardState state = seeded(PlayerSeat::Host);
  const std::string cardId = state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).id;

  const BoardState flipped = applyAction(state, flipCard(PlayerSeat::Host, cardId));

  EXPECT_TRUE(flipped.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).flipped);
}

TEST(BoardState, TapCounterFlipWithAnUnknownIdAreNoOps) {
  const BoardState state = seeded(PlayerSeat::Host);
  EXPECT_EQ(applyAction(state, tapCard(PlayerSeat::Host, "ghost")), state);
  EXPECT_EQ(applyAction(state, addCounter(PlayerSeat::Host, "ghost")), state);
  EXPECT_EQ(applyAction(state, flipCard(PlayerSeat::Host, "ghost")), state);
}

TEST(BoardState, TapCounterFlipAreScopedToTheGivenSeat) {
  const BoardState state = seeded(PlayerSeat::Host);
  const std::string hostCard = state.seats.at(seatIndex(PlayerSeat::Host)).hand.at(0).id;

  EXPECT_EQ(applyAction(state, flipCard(PlayerSeat::Guest, hostCard)), state);
}

TEST(BoardState, CreateTokenMintsANamedTokenInTheZone) {
  const BoardState state = seeded(PlayerSeat::Host);

  const BoardState created =
      applyAction(state, createToken(PlayerSeat::Host, PlayerZone::Creatures, "Bears"));

  const SeatBoard &host = created.seats.at(seatIndex(PlayerSeat::Host));
  ASSERT_EQ(host.zones.at(zoneIndex(PlayerZone::Creatures)).size(), 1u);
  const BoardCard &token = host.zones.at(zoneIndex(PlayerZone::Creatures)).at(0);
  EXPECT_TRUE(token.is_token);
  EXPECT_EQ(token.name, "Bears Token");
  EXPECT_EQ(token.id, "host-token-1");
  EXPECT_EQ(host.hand, state.seats.at(seatIndex(PlayerSeat::Host)).hand);
  EXPECT_EQ(created.seats.at(seatIndex(PlayerSeat::Guest)),
            state.seats.at(seatIndex(PlayerSeat::Guest)));
}

} // namespace
} // namespace mtgcpp::state
