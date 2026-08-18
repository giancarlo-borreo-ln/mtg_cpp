// M1.2 seat/zone/minting tests, mirroring the webapp's core/board.spec.ts.

#include "core/board.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mtgcpp::core {
namespace {

Card card(std::string name, int quantity = 1) {
  Card c;
  c.name = std::move(name);
  c.set_code = "sta";
  c.set_name = "Test Set";
  c.collector_number = "1";
  c.quantity = quantity;
  c.type_line = "Instant";
  c.mana_cost = "{R}";
  c.cmc = 1.0f;
  c.image_uris["png"] = "https://img/" + c.name + ".png";
  return c;
}

std::vector<std::string> namesOf(const std::vector<BoardCard> &cards) {
  std::vector<std::string> names;
  names.reserve(cards.size());
  for (const BoardCard &c : cards) {
    names.push_back(c.name);
  }
  return names;
}

std::vector<std::string> idsOf(const std::vector<BoardCard> &cards) {
  std::vector<std::string> ids;
  ids.reserve(cards.size());
  for (const BoardCard &c : cards) {
    ids.push_back(c.id);
  }
  return ids;
}

const std::vector<BoardCard> &zonePile(const ZoneCards &zones, PlayerZone zone) {
  return zones.at(static_cast<std::size_t>(zone));
}

TEST(OppositeSeat, ReturnsGuestForHost) {
  EXPECT_EQ(oppositeSeat(PlayerSeat::Host), PlayerSeat::Guest);
}

TEST(OppositeSeat, ReturnsHostForGuest) {
  EXPECT_EQ(oppositeSeat(PlayerSeat::Guest), PlayerSeat::Host);
}

TEST(MintHandCards, ExpandsQuantitiesIntoIndividualInstances) {
  const std::vector<Card> deck = {card("Bolt", 2), card("Forest", 3)};

  const std::vector<BoardCard> instances = mintHandCards(deck, PlayerSeat::Host);

  EXPECT_EQ(instances.size(), 5u);
  EXPECT_EQ(namesOf(instances),
            (std::vector<std::string>{"Bolt", "Bolt", "Forest", "Forest", "Forest"}));
}

TEST(MintHandCards, AssignsSeatPrefixedUniqueInstanceIds) {
  const std::vector<Card> deck = {card("Bolt", 2)};

  const std::vector<BoardCard> instances = mintHandCards(deck, PlayerSeat::Guest);

  EXPECT_EQ(idsOf(instances), (std::vector<std::string>{"guest-1", "guest-2"}));
}

TEST(MintHandCards, CopiesImageAndMetadataOntoEachInstance) {
  const std::vector<BoardCard> instances = mintHandCards({card("Bolt")}, PlayerSeat::Host);

  const BoardCard expected{
      .id = "host-1",
      .source_key = "sta:1",
      .name = "Bolt",
      .image_url = "https://img/Bolt.png",
      .mana_cost = "{R}",
      .type_line = "Instant",
      .cmc = 1.0f,
      .tapped = false,
      .counters = 0,
      .flipped = false,
      .is_token = false,
  };
  EXPECT_EQ(instances.front(), expected);
}

TEST(MintHandCards, CapsTheHandAtHandSizeCopies) {
  const std::vector<Card> deck = {card("Bolt", static_cast<int>(kHandSize) + 3)};

  EXPECT_EQ(mintHandCards(deck, PlayerSeat::Host).size(), kHandSize);
}

TEST(MintHandCards, ProducesDistinctInstancesAcrossSeats) {
  const std::vector<Card> deck = {card("Bolt", 2)};

  const auto host = mintHandCards(deck, PlayerSeat::Host);
  const auto guest = mintHandCards(deck, PlayerSeat::Guest);

  EXPECT_NE(idsOf(host), idsOf(guest));
}

TEST(MintHandCards, EmptyDeckYieldsNoInstances) {
  EXPECT_TRUE(mintHandCards({}, PlayerSeat::Host).empty());
  EXPECT_TRUE(mintBoardCards({}, PlayerSeat::Host).empty());
}

TEST(ClassifyZone, RoutesLandsToTheLandsZone) {
  EXPECT_EQ(classifyZone("Basic Land — Forest"), PlayerZone::Lands);
  EXPECT_EQ(classifyZone("Land Creature — Insect"), PlayerZone::Lands);
}

TEST(ClassifyZone, RoutesCreaturesToTheCreaturesZone) {
  EXPECT_EQ(classifyZone("Creature — Goblin"), PlayerZone::Creatures);
  EXPECT_EQ(classifyZone("Artifact Creature — Golem"), PlayerZone::Creatures);
}

TEST(ClassifyZone, RoutesInstantsAndSorceriesToTheSharedSpellSpot) {
  EXPECT_EQ(classifyZone("Instant"), PlayerZone::InstantsSorceries);
  EXPECT_EQ(classifyZone("Sorcery"), PlayerZone::InstantsSorceries);
}

TEST(ClassifyZone, RoutesAnyOtherTypeToTheSharedSpellSpot) {
  EXPECT_EQ(classifyZone("Enchantment — Aura"), PlayerZone::InstantsSorceries);
  EXPECT_EQ(classifyZone(""), PlayerZone::InstantsSorceries);
}

TEST(DistributeToZones, GroupsInstancesByZonePreservingOrderWithinEachZone) {
  Card bolt1 = card("Bolt");
  Card bear = card("Grizzly Bears");
  bear.type_line = "Creature — Bear";
  Card forest = card("Forest");
  forest.type_line = "Basic Land — Forest";
  Card bolt2 = card("Bolt");
  const std::vector<Card> deck = {bolt1, bear, forest, bolt2};

  const std::vector<BoardCard> instances = mintBoardCards(deck, PlayerSeat::Host);
  const ZoneCards zones = distributeToZones(instances);

  EXPECT_EQ(namesOf(zonePile(zones, PlayerZone::Lands)), (std::vector<std::string>{"Forest"}));
  EXPECT_EQ(namesOf(zonePile(zones, PlayerZone::Creatures)),
            (std::vector<std::string>{"Grizzly Bears"}));
  EXPECT_EQ(namesOf(zonePile(zones, PlayerZone::InstantsSorceries)),
            (std::vector<std::string>{"Bolt", "Bolt"}));
  EXPECT_TRUE(zonePile(zones, PlayerZone::Graveyard).empty());
  EXPECT_TRUE(zonePile(zones, PlayerZone::Exile).empty());
}

TEST(SeatBoardFromDeck, TakesTheFirstHandSizeCopiesAsTheOpeningHand) {
  Card bolt = card("Bolt", 8);
  Card bears = card("Bears", 6);
  bears.type_line = "Creature — Bear";
  Card forest = card("Forest", 8);
  forest.type_line = "Basic Land — Forest";
  const std::vector<Card> deck = {bolt, bears, forest};

  const SeatBoard board = seatBoardFromDeck(deck, PlayerSeat::Host);

  EXPECT_EQ(board.hand.size(), kHandSize);
  EXPECT_EQ(idsOf(board.hand), (std::vector<std::string>{"host-1", "host-2", "host-3", "host-4",
                                                         "host-5", "host-6", "host-7"}));
}

TEST(SeatBoardFromDeck, DistributesTheRemainingCopiesOntoTheZones) {
  Card bolt = card("Bolt", 8);
  Card bears = card("Bears", 6);
  bears.type_line = "Creature — Bear";
  Card forest = card("Forest", 8);
  forest.type_line = "Basic Land — Forest";
  const std::vector<Card> deck = {bolt, bears, forest};

  const SeatBoard board = seatBoardFromDeck(deck, PlayerSeat::Host);

  EXPECT_EQ(zonePile(board.zones, PlayerZone::Lands).size(), 8u);
  EXPECT_EQ(zonePile(board.zones, PlayerZone::Creatures).size(), 6u);
  EXPECT_EQ(zonePile(board.zones, PlayerZone::InstantsSorceries).size(), 1u);
  EXPECT_TRUE(zonePile(board.zones, PlayerZone::Graveyard).empty());
  EXPECT_TRUE(zonePile(board.zones, PlayerZone::Exile).empty());
}

TEST(SeatBoardFromDeck, MintsDistinctInstancesPerSeat) {
  Card bolt = card("Bolt", 8);
  Card bears = card("Bears", 6);
  bears.type_line = "Creature — Bear";
  Card forest = card("Forest", 8);
  forest.type_line = "Basic Land — Forest";
  const std::vector<Card> deck = {bolt, bears, forest};

  const SeatBoard host = seatBoardFromDeck(deck, PlayerSeat::Host);
  const SeatBoard guest = seatBoardFromDeck(deck, PlayerSeat::Guest);

  EXPECT_NE(idsOf(host.hand), idsOf(guest.hand));
  EXPECT_EQ(zonePile(host.zones, PlayerZone::Creatures).front().id, "host-9");
  EXPECT_EQ(zonePile(guest.zones, PlayerZone::Creatures).front().id, "guest-9");
}

TEST(SeatBoardFromDeck, EmptyDeckYieldsAnEmptyBoard) {
  const SeatBoard board = seatBoardFromDeck({}, PlayerSeat::Host);

  EXPECT_TRUE(board.hand.empty());
  for (const std::vector<BoardCard> &zone : board.zones) {
    EXPECT_TRUE(zone.empty());
  }
}

TEST(EmptySeatBoard, ReturnsAnEmptyHandAndEmptyZones) {
  const SeatBoard board = emptySeatBoard();

  EXPECT_TRUE(board.hand.empty());
  EXPECT_EQ(board.zones, emptyZones());
}

TEST(BoardConstants, HandSizeAndStartingLife) {
  EXPECT_EQ(kHandSize, 7u);
  EXPECT_EQ(kStartingLife, 20);
}

TEST(PlayerZone, ListsAllFiveZonesInRenderOrder) {
  EXPECT_EQ(kPlayerZones.size(), 5u);
  EXPECT_EQ(kPlayerZoneCount, kPlayerZones.size());
}

TEST(PlayerSeat, RoundTripsThroughStrings) {
  EXPECT_EQ(playerSeatFromString(playerSeatToString(PlayerSeat::Host)), PlayerSeat::Host);
  EXPECT_EQ(playerSeatFromString(playerSeatToString(PlayerSeat::Guest)), PlayerSeat::Guest);
  EXPECT_EQ(playerSeatFromString("bogus"), std::nullopt);
}

TEST(PlayerZone, RoundTripsThroughStrings) {
  for (const PlayerZone zone : kPlayerZones) {
    EXPECT_EQ(playerZoneFromString(playerZoneToString(zone)), zone);
  }
  EXPECT_EQ(playerZoneFromString("bogus"), std::nullopt);
}

struct CardMovesFixture {
  BoardCard bolt;
  BoardCard bear;
  BoardCard forest;
  SeatBoard board;
};

CardMovesFixture cardMovesFixture(PlayerSeat seat) {
  Card bolt = card("Bolt");
  bolt.type_line = "Instant";
  Card bear = card("Bears");
  bear.type_line = "Creature — Bear";
  Card forest = card("Forest");
  forest.type_line = "Basic Land — Forest";
  const std::vector<Card> deck = {bolt, bear, forest};

  const std::vector<BoardCard> instances = mintBoardCards(deck, seat);
  SeatBoard board = emptySeatBoard();
  board.hand = {instances.at(0), instances.at(1)};
  board.zones.at(static_cast<std::size_t>(PlayerZone::Lands)) = {instances.at(2)};
  return {instances.at(0), instances.at(1), instances.at(2), std::move(board)};
}

TEST(DetachCard, RemovesAHandCardAndLeavesZonesIntact) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const std::optional<CardDetachment> result = detachCard(fix.board, fix.bolt.id);
  if (result.has_value()) {
    const CardDetachment &detachment = result.value();
    EXPECT_EQ(detachment.card, fix.bolt);
    EXPECT_EQ(detachment.seat_board.hand, (std::vector<BoardCard>{fix.bear}));
    EXPECT_EQ(zonePile(detachment.seat_board.zones, PlayerZone::Lands),
              (std::vector<BoardCard>{fix.forest}));
    EXPECT_TRUE(zonePile(detachment.seat_board.zones, PlayerZone::Creatures).empty());
  } else {
    FAIL() << "expected a card detachment for " << fix.bolt.id;
  }
}

TEST(DetachCard, RemovesAZoneCardAndLeavesTheHandIntact) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const std::optional<CardDetachment> result = detachCard(fix.board, fix.forest.id);
  if (result.has_value()) {
    const CardDetachment &detachment = result.value();
    EXPECT_EQ(detachment.card, fix.forest);
    EXPECT_EQ(detachment.seat_board.hand, (std::vector<BoardCard>{fix.bolt, fix.bear}));
    EXPECT_TRUE(zonePile(detachment.seat_board.zones, PlayerZone::Lands).empty());
  } else {
    FAIL() << "expected a card detachment for " << fix.forest.id;
  }
}

TEST(DetachCard, ReturnsNulloptWhenTheIdIsNotOnTheSeat) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);
  const std::vector<BoardCard> other = mintBoardCards({card("Ghost")}, PlayerSeat::Guest);

  EXPECT_FALSE(detachCard(fix.board, other.at(0).id).has_value());
}

TEST(MoveCardToZone, MovesAHandCardIntoTheTargetZonePreservingTheInstance) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const SeatBoard moved = moveCardToZone(fix.board, fix.bolt.id, PlayerZone::Creatures);

  EXPECT_EQ(moved.hand, (std::vector<BoardCard>{fix.bear}));
  EXPECT_EQ(zonePile(moved.zones, PlayerZone::Creatures), (std::vector<BoardCard>{fix.bolt}));
  EXPECT_EQ(zonePile(moved.zones, PlayerZone::Lands), (std::vector<BoardCard>{fix.forest}));
}

TEST(MoveCardToZone, MovesACardBetweenZonesRemovingItFromItsOldZone) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);
  const SeatBoard with_bear = moveCardToZone(fix.board, fix.bear.id, PlayerZone::Creatures);

  const SeatBoard moved = moveCardToZone(with_bear, fix.bear.id, PlayerZone::Lands);

  EXPECT_EQ(moved.hand, (std::vector<BoardCard>{fix.bolt}));
  EXPECT_TRUE(zonePile(moved.zones, PlayerZone::Creatures).empty());
  EXPECT_EQ(zonePile(moved.zones, PlayerZone::Lands),
            (std::vector<BoardCard>{fix.forest, fix.bear}));
}

TEST(MoveCardToZone, AppendsTheCardAtTheEndOfTheTargetZone) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);
  const SeatBoard with_bolt = moveCardToZone(fix.board, fix.bolt.id, PlayerZone::Lands);

  const SeatBoard moved = moveCardToZone(with_bolt, fix.bear.id, PlayerZone::Lands);

  EXPECT_EQ(namesOf(zonePile(moved.zones, PlayerZone::Lands)),
            (std::vector<std::string>{"Forest", "Bolt", "Bears"}));
}

TEST(MoveCardToZone, NeverDuplicatesTheInstanceAcrossHandAndZones) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);
  const SeatBoard moved = moveCardToZone(fix.board, fix.bolt.id, PlayerZone::Creatures);

  std::size_t bolt_count = 0;
  for (const BoardCard &c : moved.hand) {
    if (c.id == fix.bolt.id) {
      ++bolt_count;
    }
  }
  for (const std::vector<BoardCard> &zone : moved.zones) {
    for (const BoardCard &c : zone) {
      if (c.id == fix.bolt.id) {
        ++bolt_count;
      }
    }
  }
  EXPECT_EQ(bolt_count, 1u);
}

TEST(MoveCardToZone, ReturnsTheOriginalSeatBoardWhenTheIdIsNotFound) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);
  const std::vector<BoardCard> other = mintBoardCards({card("Ghost")}, PlayerSeat::Guest);

  EXPECT_EQ(moveCardToZone(fix.board, other.at(0).id, PlayerZone::Creatures), fix.board);
}

TEST(MoveCardToStack, DetachesAHandCardAndReturnsItForTheStack) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const std::optional<CardDetachment> result = moveCardToStack(fix.board, fix.bolt.id);
  if (result.has_value()) {
    const CardDetachment &detachment = result.value();
    EXPECT_EQ(detachment.card, fix.bolt);
    EXPECT_EQ(detachment.seat_board.hand, (std::vector<BoardCard>{fix.bear}));
  } else {
    FAIL() << "expected a card detachment for " << fix.bolt.id;
  }
}

TEST(MoveCardToStack, DetachesAZoneCardAndReturnsItForTheStack) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const std::optional<CardDetachment> result = moveCardToStack(fix.board, fix.forest.id);
  if (result.has_value()) {
    const CardDetachment &detachment = result.value();
    EXPECT_EQ(detachment.card, fix.forest);
    EXPECT_TRUE(zonePile(detachment.seat_board.zones, PlayerZone::Lands).empty());
  } else {
    FAIL() << "expected a card detachment for " << fix.forest.id;
  }
}

TEST(MoveCardToStack, ReturnsNulloptWhenTheIdIsNotOnTheSeat) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  EXPECT_FALSE(moveCardToStack(fix.board, "nope").has_value());
}

TEST(UpdateCardInSeat, ReplacesAHandCardKeepingItsId) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const SeatBoard updated = updateCardInSeat(fix.board, fix.bolt.id, [](const BoardCard &c) {
    BoardCard result = c;
    result.name = "Shiny Bolt";
    return result;
  });

  EXPECT_EQ(updated.hand.size(), 2u);
  EXPECT_NE(updated.hand.at(0), fix.bolt);
  EXPECT_EQ(updated.hand.at(0).id, fix.bolt.id);
  EXPECT_EQ(updated.hand.at(0).name, "Shiny Bolt");
  EXPECT_EQ(updated.hand.at(1), fix.bear);
}

TEST(UpdateCardInSeat, ReplacesAZoneCardInPlace) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const SeatBoard updated = updateCardInSeat(fix.board, fix.forest.id, [](const BoardCard &c) {
    BoardCard result = c;
    result.counters = 2;
    return result;
  });

  EXPECT_EQ(zonePile(updated.zones, PlayerZone::Lands).size(), 1u);
  EXPECT_EQ(zonePile(updated.zones, PlayerZone::Lands).at(0).counters, 2);
  EXPECT_NE(zonePile(updated.zones, PlayerZone::Lands).at(0), fix.forest);
  EXPECT_EQ(zonePile(updated.zones, PlayerZone::Lands).at(0).id, fix.forest.id);
}

TEST(UpdateCardInSeat, ReturnsTheOriginalSeatBoardWhenTheIdIsUnknown) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const SeatBoard noop = updateCardInSeat(fix.board, "ghost", [](const BoardCard &c) { return c; });

  EXPECT_EQ(noop, fix.board);
}

TEST(TapCard, TogglesAHandCardBetweenTappedAndUntapped) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const SeatBoard tapped = tapCard(fix.board, fix.bolt.id);
  EXPECT_TRUE(tapped.hand.at(0).tapped);
  EXPECT_EQ(tapped.hand.at(1), fix.bear);

  const SeatBoard untapped = tapCard(tapped, fix.bolt.id);
  EXPECT_FALSE(untapped.hand.at(0).tapped);
}

TEST(TapCard, TogglesAZoneCardTappedState) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const SeatBoard tapped = tapCard(fix.board, fix.forest.id);

  EXPECT_TRUE(zonePile(tapped.zones, PlayerZone::Lands).at(0).tapped);
}

TEST(TapCard, ReturnsTheOriginalSeatBoardForAnUnknownId) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  EXPECT_EQ(tapCard(fix.board, "ghost"), fix.board);
}

TEST(AddCounter, IncrementsTheCounterCountOnTheTargetCard) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const SeatBoard once = addCounter(fix.board, fix.bolt.id);
  EXPECT_EQ(once.hand.at(0).counters, 1);

  const SeatBoard twice = addCounter(once, fix.bolt.id);
  EXPECT_EQ(twice.hand.at(0).counters, 2);
  EXPECT_EQ(twice.hand.at(1), fix.bear);
}

TEST(AddCounter, ReturnsTheOriginalSeatBoardForAnUnknownId) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  EXPECT_EQ(addCounter(fix.board, "ghost"), fix.board);
}

TEST(FlipCard, TogglesTheFlippedStateBackAndForth) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const SeatBoard flipped = flipCard(fix.board, fix.bolt.id);
  EXPECT_TRUE(flipped.hand.at(0).flipped);

  const SeatBoard front = flipCard(flipped, fix.bolt.id);
  EXPECT_FALSE(front.hand.at(0).flipped);
}

TEST(FlipCard, ReturnsTheOriginalSeatBoardForAnUnknownId) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  EXPECT_EQ(flipCard(fix.board, "ghost"), fix.board);
}

TEST(CreateToken, MintsAGenericTokenIntoTheTargetZone) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const SeatBoard with_token =
      createToken(PlayerSeat::Host, fix.board, PlayerZone::Creatures, "Bears");

  EXPECT_EQ(with_token.hand, (std::vector<BoardCard>{fix.bolt, fix.bear}));
  EXPECT_EQ(zonePile(with_token.zones, PlayerZone::Lands), (std::vector<BoardCard>{fix.forest}));
  EXPECT_EQ(zonePile(with_token.zones, PlayerZone::Creatures).size(), 1u);

  const BoardCard token = zonePile(with_token.zones, PlayerZone::Creatures).at(0);
  EXPECT_EQ(token.name, "Bears Token");
  EXPECT_TRUE(token.is_token);
  EXPECT_TRUE(token.image_url.empty());
  EXPECT_EQ(token.type_line, "Token Creature");
  EXPECT_FALSE(token.tapped);
  EXPECT_EQ(token.counters, 0);
  EXPECT_FALSE(token.flipped);
  EXPECT_EQ(token.cmc, 0.0f);
}

TEST(CreateToken, GivesEachTokenAUniqueSeatPrefixedId) {
  const CardMovesFixture fix = cardMovesFixture(PlayerSeat::Host);

  const SeatBoard one = createToken(PlayerSeat::Host, fix.board, PlayerZone::Creatures, "Bears");
  const SeatBoard two = createToken(PlayerSeat::Host, one, PlayerZone::Creatures, "Bears");

  EXPECT_EQ(idsOf(zonePile(two.zones, PlayerZone::Creatures)),
            (std::vector<std::string>{"host-token-1", "host-token-2"}));
}

TEST(CreateToken, DistinguishesTokensAcrossSeats) {
  const SeatBoard host =
      createToken(PlayerSeat::Host, emptySeatBoard(), PlayerZone::Creatures, "Bears");
  const SeatBoard guest =
      createToken(PlayerSeat::Guest, emptySeatBoard(), PlayerZone::Creatures, "Bears");

  EXPECT_EQ(zonePile(host.zones, PlayerZone::Creatures).at(0).id, "host-token-1");
  EXPECT_EQ(zonePile(guest.zones, PlayerZone::Creatures).at(0).id, "guest-token-1");
}

TEST(ZoneLabel, LabelsEveryZone) {
  EXPECT_EQ(zoneLabel(PlayerZone::Lands), "Lands");
  EXPECT_EQ(zoneLabel(PlayerZone::Creatures), "Creatures");
  EXPECT_EQ(zoneLabel(PlayerZone::InstantsSorceries), "Instants / Sorceries");
  EXPECT_EQ(zoneLabel(PlayerZone::Graveyard), "Graveyard");
  EXPECT_EQ(zoneLabel(PlayerZone::Exile), "Exile");
}

TEST(ZoneGridTemplateAreas, ContainsEveryZone) {
  const std::string areas = zoneGridTemplateAreas();
  for (const PlayerZone zone : kPlayerZones) {
    EXPECT_NE(areas.find(playerZoneToString(zone)), std::string::npos);
  }
  EXPECT_NE(areas.find("\"lands lands lands\""), std::string::npos);
}

TEST(ZoneGridTemplateAreas, IsExact) {
  EXPECT_EQ(zoneGridTemplateAreas(),
            "\"creatures creatures graveyard\" \"instants_sorceries instants_sorceries exile\" "
            "\"lands lands lands\"");
}

TEST(HandColumnTemplate, BuildsOneColumnPerCard) {
  EXPECT_EQ(handColumnTemplate(7), "repeat(7, 1fr)");
}

TEST(HandColumnTemplate, NeverReturnsZeroColumnsForAnEmptyHand) {
  EXPECT_EQ(handColumnTemplate(0), "repeat(1, 1fr)");
}

TEST(ToRevealCards, MapsBoardCardsToTheRevealCardShape) {
  const std::vector<BoardCard> cards = mintHandCards({card("Forest")}, PlayerSeat::Host);

  const std::vector<RevealCard> revealed = toRevealCards(cards);

  const std::vector<RevealCard> expected{{"host-1", "Forest", "https://img/Forest.png"}};
  EXPECT_EQ(revealed, expected);
}

TEST(StackCardOffset, PlacesTheFirstCardAtTheOrigin) {
  EXPECT_EQ(stackCardOffset(0), "translate(0px, 0px)");
}

TEST(StackCardOffset, ShiftsEachLaterCardByTheFixedOffset) {
  EXPECT_EQ(stackCardOffset(1), "translate(14px, 10px)");
  EXPECT_EQ(stackCardOffset(2), "translate(28px, 20px)");
}

TEST(StackCardOffset, GrowsStrictlyInPlayOrder) {
  const std::string first = stackCardOffset(0);
  const std::string second = stackCardOffset(1);
  const std::string third = stackCardOffset(2);
  EXPECT_NE(second, first);
  EXPECT_NE(third, second);
}

TEST(ParseLife, ParsesAPlainInteger) {
  EXPECT_EQ(parseLife("30"), 30);
  EXPECT_EQ(parseLife("20"), 20);
}

TEST(ParseLife, ClampsNegativeValuesToZero) { EXPECT_EQ(parseLife("-5"), 0); }

TEST(ParseLife, ClampsAbsurdValuesToASaneCeiling) { EXPECT_EQ(parseLife("100000"), 9999); }

TEST(ParseLife, FallsBackToStartingLifeOnGarbageInput) {
  EXPECT_EQ(parseLife(""), kStartingLife);
  EXPECT_EQ(parseLife("abc"), kStartingLife);
}

TEST(DropListIds, NamesTheHandDropListPerSeat) {
  EXPECT_EQ(handDropListId(PlayerSeat::Host), "hand-host");
  EXPECT_EQ(handDropListId(PlayerSeat::Guest), "hand-guest");
}

TEST(DropListIds, NamesEachZoneDropListPerSeat) {
  EXPECT_EQ(zoneDropListId(PlayerSeat::Host, PlayerZone::Creatures), "creatures-host");
  EXPECT_EQ(zoneDropListId(PlayerSeat::Guest, PlayerZone::Lands), "lands-guest");
}

TEST(DropListIds, CollectsEveryDropListIdExactlyOncePlusTheStack) {
  const std::vector<std::string> ids = allDropListIds();

  EXPECT_EQ(std::count(ids.begin(), ids.end(), std::string(kStackDropListId)), 1);
  EXPECT_EQ(std::count(ids.begin(), ids.end(), "hand-host"), 1);
  EXPECT_EQ(std::count(ids.begin(), ids.end(), "hand-guest"), 1);
  for (const PlayerSeat seat : {PlayerSeat::Host, PlayerSeat::Guest}) {
    for (const PlayerZone zone : kPlayerZones) {
      EXPECT_EQ(std::count(ids.begin(), ids.end(), zoneDropListId(seat, zone)), 1);
    }
  }
  const std::set<std::string> unique(ids.begin(), ids.end());
  EXPECT_EQ(unique.size(), ids.size());
}

TEST(DropListIds, ParsesTheZoneOutOfAZoneDropListId) {
  EXPECT_EQ(zoneFromDropListId("creatures-host"), PlayerZone::Creatures);
  EXPECT_EQ(zoneFromDropListId("lands-guest"), PlayerZone::Lands);
}

TEST(DropListIds, ReturnsNulloptForHandStackAndUnknownIds) {
  EXPECT_EQ(zoneFromDropListId("hand-host"), std::nullopt);
  EXPECT_EQ(zoneFromDropListId(kStackDropListId), std::nullopt);
  EXPECT_EQ(zoneFromDropListId("bogus"), std::nullopt);
}

TEST(StackConstants, OffsetsAndDropListId) {
  EXPECT_EQ(kStackOffsetX, 14u);
  EXPECT_EQ(kStackOffsetY, 10u);
  EXPECT_EQ(kStackDropListId, "stack");
}

} // namespace
} // namespace mtgcpp::core
