// M9.3 CardView tests: pure card-state logic (back/token/badge/tap) and
// geometry. Rendering needs a window, so it is exercised in the running app.

#include "ui/widgets/card_view.h"

#include <gtest/gtest.h>

#include <SFML/Graphics.hpp>

namespace mtgcpp::core {
namespace {

BoardCard card(bool flipped, bool token, int counters, bool tapped) {
  BoardCard c;
  c.id = "host-1";
  c.name = "Grizzly Bears";
  c.mana_cost = "{1}{G}";
  c.flipped = flipped;
  c.is_token = token;
  c.counters = counters;
  c.tapped = tapped;
  return c;
}

TEST(CardView, BoundsMatchPositionAndSize) {
  CardView view;
  view.setPosition({10.f, 20.f});
  view.setSize({50.f, 70.f});
  EXPECT_FLOAT_EQ(view.bounds().left, 10.f);
  EXPECT_FLOAT_EQ(view.bounds().top, 20.f);
  EXPECT_FLOAT_EQ(view.bounds().width, 50.f);
  EXPECT_FLOAT_EQ(view.bounds().height, 70.f);
  EXPECT_TRUE(view.contains({10.f, 20.f}));
  EXPECT_FALSE(view.contains({9.f, 20.f}));
  EXPECT_FALSE(view.contains({60.f, 90.f})); // bottom-right edge is exclusive
}

TEST(CardView, FaceDownWhenFlipped) {
  CardView view;
  view.setCard(card(/*flipped=*/true, /*token=*/false, 0, false));
  EXPECT_TRUE(view.showsBack());
  EXPECT_FALSE(view.showsToken());
}

TEST(CardView, TokenAlwaysShowsTheTokenFace) {
  CardView view;
  view.setCard(card(/*flipped=*/false, /*token=*/true, 0, false));
  EXPECT_TRUE(view.showsToken());
}

TEST(CardView, CounterBadgeOnlyWhenCountersExist) {
  CardView with;
  with.setCard(card(false, false, 2, false));
  EXPECT_TRUE(with.showsBadge());
  EXPECT_EQ(with.badgeText(), "+2/+2");

  CardView none;
  none.setCard(card(false, false, 0, false));
  EXPECT_FALSE(none.showsBadge());
}

TEST(CardView, TapRotationIsInstant) {
  CardView tapped;
  tapped.setCard(card(false, false, 0, true));
  EXPECT_FLOAT_EQ(tapped.tapAngleDegrees(), 90.f);

  CardView upright;
  upright.setCard(card(false, false, 0, false));
  EXPECT_FLOAT_EQ(upright.tapAngleDegrees(), 0.f);
}

TEST(CardView, ExposesTheNameAndCost) {
  CardView view;
  view.setCard(card(false, false, 0, false));
  EXPECT_EQ(view.nameText(), "Grizzly Bears");
  EXPECT_EQ(view.costText(), "{1}{G}");
}

} // namespace
} // namespace mtgcpp::core
