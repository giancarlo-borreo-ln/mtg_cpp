// M1.1 card-model tests, mirroring the webapp's core/models.spec.ts.

#include "core/card.h"

#include <gtest/gtest.h>

#include <map>
#include <optional>

namespace mtgcpp::core {
namespace {

Card bolt() {
  Card c;
  c.name = "Lightning Bolt";
  c.set_code = "sta";
  c.set_name = "Strixhaven Mystical Archive";
  c.collector_number = "49";
  c.quantity = 1;
  c.type_line = "Instant";
  c.image_uris["png"] = "https://img/bolt.png";
  return c;
}

Card doubleFaced() {
  Card c;
  c.name = "Akoum Warrior // Akoum Teeth";
  c.set_code = "znr";
  c.set_name = "Zendikar Rising";
  c.collector_number = "134";
  c.quantity = 1;
  c.type_line = "Creature — Minotaur";

  ScryfallFace front;
  front.name = "Akoum Warrior";
  front.image_uris["png"] = "https://img/front.png";

  ScryfallFace back;
  back.name = "Akoum Teeth";
  back.image_uris["png"] = "https://img/back.png";

  c.card_faces = {front, back};
  return c;
}

TEST(CardKey, CombinesSetAndCollectorNumber) { EXPECT_EQ(cardKey(bolt()), "sta:49"); }

TEST(CardKey, FallsBackToNameWhenPrintingMissing) {
  Card c = bolt();
  c.set_code.clear();
  c.collector_number.clear();
  EXPECT_EQ(cardKey(c), "Lightning Bolt");
}

TEST(CardKey, FallsBackToNameWhenOnlyCollectorNumberMissing) {
  Card c = bolt();
  c.collector_number.clear();
  EXPECT_EQ(cardKey(c), "Lightning Bolt");
}

TEST(CardImage, PrefersPng) { EXPECT_EQ(cardImage(bolt()), "https://img/bolt.png"); }

TEST(CardImage, UsesFrontFaceForDoubleFacedCards) {
  EXPECT_EQ(cardImage(doubleFaced()), "https://img/front.png");
}

TEST(CardImage, FallsBackToNormalSize) {
  Card c = bolt();
  c.image_uris = {{"normal", "https://img/n.png"}};
  EXPECT_EQ(cardImage(c), "https://img/n.png");
}

TEST(CardImage, PrefersPngOverNormal) {
  Card c = bolt();
  c.image_uris["normal"] = "https://img/n.png";
  EXPECT_EQ(cardImage(c), "https://img/bolt.png");
}

TEST(CardImage, ReturnsEmptyWhenNoImage) {
  Card c = bolt();
  c.image_uris.clear();
  EXPECT_TRUE(cardImage(c).empty());
}

TEST(PlayerName, ReturnsKnownProfileNames) {
  EXPECT_EQ(playerName("carlo"), "Carlo");
  EXPECT_EQ(playerName("stefano"), "Stefano");
  EXPECT_EQ(playerName("giancarlo"), "Giancarlo");
  EXPECT_EQ(playerName("nicola"), "Nicola");
}

TEST(PlayerName, ReturnsIdForUnknownProfile) { EXPECT_EQ(playerName("guest"), "guest"); }

TEST(PlayerProfiles, ListsAllFourProfiles) {
  EXPECT_EQ(playerProfiles().size(), 4u);
  EXPECT_EQ(playerProfiles().front().id, "carlo");
}

TEST(ArenaSection, RoundTripsThroughStrings) {
  const std::array<ArenaSection, 3> sections = {ArenaSection::Mainboard, ArenaSection::Sideboard,
                                                ArenaSection::Commander};
  for (const ArenaSection section : sections) {
    EXPECT_EQ(arenaSectionFromString(arenaSectionToString(section)), section);
  }
}

TEST(ArenaSection, FromStringRejectsUnknownValues) {
  EXPECT_EQ(arenaSectionFromString("oops"), std::nullopt);
  EXPECT_EQ(arenaSectionFromString(""), std::nullopt);
}

TEST(Card, DefaultsAreSane) {
  const Card c;
  EXPECT_EQ(c.quantity, 1);
  EXPECT_EQ(c.section, ArenaSection::Mainboard);
  EXPECT_FALSE(c.cmc.has_value());
  EXPECT_FALSE(c.hasPrinting());
  EXPECT_TRUE(c.colors.empty());
  EXPECT_TRUE(c.image_uris.empty());
}

TEST(RevealCard, IsTriviallyConstructibleAndComparable) {
  const RevealCard a{"host-1", "Forest", "https://img/forest.png"};
  const RevealCard &b = a;
  EXPECT_EQ(a, b);
}

} // namespace
} // namespace mtgcpp::core
