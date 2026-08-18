// M9.3 ZoneView tests: geometry, label and empty-state logic (headless).

#include "ui/widgets/zone_view.h"

#include <gtest/gtest.h>

#include <SFML/Graphics.hpp>

namespace mtgcpp::core {
namespace {

TEST(ZoneView, BoundsMatchPositionAndSize) {
  ZoneView view;
  view.setPosition({5.f, 6.f});
  view.setSize({120.f, 40.f});
  EXPECT_FLOAT_EQ(view.bounds().left, 5.f);
  EXPECT_FLOAT_EQ(view.bounds().width, 120.f);
  EXPECT_TRUE(view.contains({5.f, 6.f}));
  EXPECT_FALSE(view.contains({125.f, 46.f})); // bottom-right edge is exclusive
}

TEST(ZoneView, LabelAndEmptyStateArePure) {
  ZoneView view;
  view.setLabel("Lands");
  view.setEmpty(true);
  EXPECT_EQ(view.label(), "Lands");
  EXPECT_TRUE(view.empty());
  EXPECT_EQ(view.emptyHint(), "Empty");

  view.setEmpty(false);
  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.emptyHint(), "");
}

TEST(ZoneView, DefaultsToEmptyAndUnlabeled) {
  ZoneView view;
  EXPECT_TRUE(view.empty());
  EXPECT_TRUE(view.label().empty());
}

} // namespace
} // namespace mtgcpp::core
