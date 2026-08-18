// M4.3 responsive layout tests: pure geometry for uiScale, insets, flex rows,
// responsive grids and the auto-fill column math. These are the rules the menu
// screens re-flow by, so they pin the exact behaviour behind "flexible to any
// window size".

#include "ui/layout.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

namespace mtgcpp::core {
namespace {

TEST(UiScale, ReferenceHeightIsOne) { EXPECT_FLOAT_EQ(uiScale({960u, 600u}), 1.f); }

TEST(UiScale, ScalesWithWindowHeightNotWidth) {
  // Width never drives the scale; height does.
  EXPECT_FLOAT_EQ(uiScale({640u, 600u}), 1.f);
  EXPECT_FLOAT_EQ(uiScale({1600u, 600u}), 1.f);
  EXPECT_NEAR(uiScale({960u, 900u}), 1.5f, 1e-4f);
}

TEST(UiScale, ClampsToAReadableRange) {
  // A tiny window must not crush the menu below 0.75x; a huge one must not
  // balloon it above 1.5x.
  EXPECT_FLOAT_EQ(uiScale({640u, 320u}), 0.75f);
  EXPECT_FLOAT_EQ(uiScale({640u, 100u}), 0.75f);
  EXPECT_FLOAT_EQ(uiScale({1600u, 1600u}), 1.5f);
  EXPECT_FLOAT_EQ(uiScale({1600u, 2000u}), 1.5f);
}

TEST(UiScale, ZeroSizedWindowDefaultsToOne) { EXPECT_FLOAT_EQ(uiScale({0u, 0u}), 1.f); }

TEST(Px, ScalesDesignPixels) {
  EXPECT_FLOAT_EQ(px(40.f, {960u, 600u}), 40.f);
  EXPECT_NEAR(px(40.f, {960u, 900u}), 60.f, 1e-4f);
}

TEST(Inset, ShrinksEverySide) {
  const sf::FloatRect rect{10.f, 20.f, 100.f, 80.f};
  const sf::FloatRect inner = inset(rect, 5.f);
  EXPECT_FLOAT_EQ(inner.left, 15.f);
  EXPECT_FLOAT_EQ(inner.top, 25.f);
  EXPECT_FLOAT_EQ(inner.width, 90.f);
  EXPECT_FLOAT_EQ(inner.height, 70.f);
}

TEST(RowOf, SplitsARectIntoEqualColumnsWithGaps) {
  const sf::FloatRect rect{0.f, 0.f, 100.f, 30.f};
  const std::vector<sf::FloatRect> cells = rowOf(rect, 3, 10.f);
  ASSERT_EQ(cells.size(), 3u);
  // Each column: (100 - 2*10) / 3 = 26.67, laid out left to right.
  EXPECT_NEAR(cells.at(0).left, 0.f, 1e-4f);
  EXPECT_NEAR(cells.at(0).width, 26.666f, 1e-2f);
  EXPECT_NEAR(cells.at(1).left, 36.666f, 1e-2f);
  EXPECT_NEAR(cells.at(2).left, 73.333f, 1e-2f);
  // Every cell keeps the row's height.
  EXPECT_FLOAT_EQ(cells.at(1).height, 30.f);
}

TEST(RowOf, EmptyCountYieldsNoCells) {
  EXPECT_TRUE(rowOf({0.f, 0.f, 100.f, 30.f}, 0, 10.f).empty());
}

TEST(GridCells, LaysOutRowMajorTwoColumns) {
  const sf::FloatRect rect{0.f, 0.f, 100.f, 100.f};
  const std::vector<sf::FloatRect> cells = gridCells(rect, 4, 2, 10.f);
  ASSERT_EQ(cells.size(), 4u);
  // 2 columns x 2 rows, each cell (100 - 10) / 2 = 45 wide and tall.
  EXPECT_NEAR(cells.at(0).left, 0.f, 1e-4f);
  EXPECT_NEAR(cells.at(0).top, 0.f, 1e-4f);
  EXPECT_NEAR(cells.at(1).left, 55.f, 1e-4f);
  EXPECT_NEAR(cells.at(2).left, 0.f, 1e-4f);
  EXPECT_NEAR(cells.at(2).top, 55.f, 1e-4f);
  EXPECT_NEAR(cells.at(3).left, 55.f, 1e-4f);
  EXPECT_NEAR(cells.at(3).top, 55.f, 1e-4f);
  EXPECT_NEAR(cells.at(0).width, 45.f, 1e-4f);
  EXPECT_NEAR(cells.at(0).height, 45.f, 1e-4f);
}

TEST(GridCells, LastRowOfCellsKeepsGridSize) {
  // 4 cells in 3 columns => 2 rows; the 4th cell is the first of the second row.
  const sf::FloatRect rect{0.f, 0.f, 160.f, 100.f};
  const std::vector<sf::FloatRect> cells = gridCells(rect, 4, 3, 8.f);
  ASSERT_EQ(cells.size(), 4u);
  EXPECT_NEAR(cells.at(3).left, 0.f, 1e-4f); // col 0 of the second row
  EXPECT_NEAR(cells.at(3).top, 54.f, 1e-4f); // (100 - 8)/2 = 46 tall + 8 gap
}

TEST(GridCells, ZeroCountOrColumnsYieldsNoCells) {
  EXPECT_TRUE(gridCells({0.f, 0.f, 100.f, 100.f}, 0, 2, 10.f).empty());
  EXPECT_TRUE(gridCells({0.f, 0.f, 100.f, 100.f}, 4, 0, 10.f).empty());
}

TEST(ResponsiveColumns, AddsColumnsAsWidthAllows) {
  constexpr float kGap = 16.f;
  constexpr float kMin = 200.f;
  // One 200px column plus no gap fits anywhere wide enough for 200.
  EXPECT_EQ(responsiveColumns(200.f, kMin, kGap, 4), 1u);
  // Two columns need 200*2 + 16 = 416.
  EXPECT_EQ(responsiveColumns(416.f, kMin, kGap, 4), 2u);
  EXPECT_EQ(responsiveColumns(415.f, kMin, kGap, 4), 1u);
  // Four columns need 200*4 + 16*3 = 848.
  EXPECT_EQ(responsiveColumns(848.f, kMin, kGap, 4), 4u);
  EXPECT_EQ(responsiveColumns(847.f, kMin, kGap, 4), 3u);
}

TEST(ResponsiveColumns, NeverExceedsTheCapAndAlwaysReturnsOne) {
  EXPECT_EQ(responsiveColumns(2000.f, 200.f, 16.f, 4), 4u); // capped at 4
  EXPECT_EQ(responsiveColumns(10.f, 200.f, 16.f, 4), 1u);   // min 1
}

// ---------------------------------------------------------------------------
// M9.2 Shandalar battlefield slot math
// ---------------------------------------------------------------------------

TEST(TableBands, TilesTheContentWithGaps) {
  const TableBands bands = tableBands({0.f, 0.f, 800.f, 600.f}, 8.f, 0.44f);
  // Five bands separated by four gaps must tile the whole content height.
  const float sum = bands.theirHand.height + bands.theirGrid.height + bands.stack.height +
                    bands.myGrid.height + bands.myHand.height;
  EXPECT_NEAR(sum + (4.f * 8.f), 600.f, 1e-3f);
  // The opponent is on top, you are at the bottom.
  EXPECT_FLOAT_EQ(bands.theirHand.top, 0.f);
  EXPECT_NEAR(bands.theirHand.top + bands.theirHand.height + 8.f, bands.theirGrid.top, 1e-3f);
  EXPECT_GT(bands.myHand.top, bands.myGrid.top);
  EXPECT_NEAR(bands.myHand.top + bands.myHand.height, 600.f, 1e-3f);
  // The stack is horizontally centered at 44% of the width.
  EXPECT_NEAR(bands.stack.width, 800.f * 0.44f, 1e-3f);
  EXPECT_NEAR(bands.stack.left, (800.f - (800.f * 0.44f)) / 2.f, 1e-3f);
}

TEST(HandArch, PeaksAtTheMiddle) {
  EXPECT_NEAR(handArch(0.f, 10.f), 0.f, 1e-3f);
  EXPECT_NEAR(handArch(0.5f, 10.f), 10.f, 1e-3f);
  EXPECT_NEAR(handArch(1.f, 10.f), 0.f, 1e-3f);
}

TEST(HandCardRects, SnapsOneCardPerCellNoOverlap) {
  const sf::FloatRect hand{0.f, 0.f, 300.f, 80.f};
  const std::vector<sf::FloatRect> rects = handCardRects(hand, 3, 8.f, true);
  ASSERT_EQ(rects.size(), 3u);
  // Cells are 100 wide; cards keep the card aspect and stay inside the band.
  EXPECT_NEAR(rects.at(0).width, rects.at(1).width, 1e-4f);
  for (const sf::FloatRect &rect : rects) {
    EXPECT_GE(rect.top, 0.f);
    EXPECT_LE(rect.top + rect.height, 80.f);
  }
  // The arch lifts the middle card above the edges (smaller top value).
  EXPECT_GE(rects.at(0).top, rects.at(1).top);
  // No overlap: consecutive cards never share space.
  EXPECT_LE(rects.at(0).left + rects.at(0).width, rects.at(1).left);
  EXPECT_LE(rects.at(1).left + rects.at(1).width, rects.at(2).left);
}

TEST(HandCardRects, ArchesDownForYourOwnHand) {
  const sf::FloatRect hand{0.f, 0.f, 300.f, 80.f};
  const std::vector<sf::FloatRect> up = handCardRects(hand, 3, 8.f, true);
  const std::vector<sf::FloatRect> down = handCardRects(hand, 3, 8.f, false);
  // The opponent's hand rises toward the middle; yours dips (mirror).
  EXPECT_GT(up.at(0).top, up.at(1).top);
  EXPECT_LT(down.at(0).top, down.at(1).top);
}

TEST(HandCardRects, EmptyCountYieldsNoCards) {
  EXPECT_TRUE(handCardRects({0.f, 0.f, 100.f, 40.f}, 0, 5.f, true).empty());
}

TEST(TableZoneRect, MirrorsTheGridTemplateAreas) {
  const sf::FloatRect grid{0.f, 0.f, 300.f, 90.f};
  const sf::FloatRect lands = tableZoneRect(grid, PlayerZone::Lands);
  const sf::FloatRect creatures = tableZoneRect(grid, PlayerZone::Creatures);
  const sf::FloatRect instants = tableZoneRect(grid, PlayerZone::InstantsSorceries);
  const sf::FloatRect graveyard = tableZoneRect(grid, PlayerZone::Graveyard);
  const sf::FloatRect exile = tableZoneRect(grid, PlayerZone::Exile);
  // Lands span the whole bottom row.
  EXPECT_NEAR(lands.left, 0.f, 1e-3f);
  EXPECT_NEAR(lands.top, 60.f, 1e-3f);
  EXPECT_NEAR(lands.width, 300.f, 1e-3f);
  EXPECT_NEAR(lands.height, 30.f, 1e-3f);
  // Creatures take the top-left pair.
  EXPECT_NEAR(creatures.width, 200.f, 1e-3f);
  EXPECT_NEAR(creatures.top, 0.f, 1e-3f);
  // Instants/sorceries the middle pair; graveyard/exile the right cells.
  EXPECT_NEAR(instants.top, 30.f, 1e-3f);
  EXPECT_NEAR(graveyard.left, 200.f, 1e-3f);
  EXPECT_NEAR(graveyard.top, 0.f, 1e-3f);
  EXPECT_NEAR(exile.left, 200.f, 1e-3f);
  EXPECT_NEAR(exile.top, 30.f, 1e-3f);
}

TEST(ZoneCardRects, RigidCellsKeepTheAspect) {
  const sf::FloatRect zone{0.f, 0.f, 300.f, 40.f};
  const ZoneCardLayout layout = zoneCardRects(zone, 3);
  ASSERT_EQ(layout.cards.size(), 3u);
  EXPECT_NEAR(layout.cardSize.x / layout.cardSize.y, kCardAspect, 1e-3f);
  // Cards are top-aligned and never overlap.
  EXPECT_FLOAT_EQ(layout.cards.at(0).top, layout.cards.at(1).top);
  EXPECT_LE(layout.cards.at(0).left + layout.cards.at(0).width, layout.cards.at(1).left);
  EXPECT_LE(layout.cards.at(1).left + layout.cards.at(1).width, layout.cards.at(2).left);
}

TEST(ZoneCardRects, EmptyCountYieldsNothing) {
  const ZoneCardLayout layout = zoneCardRects({0.f, 0.f, 100.f, 40.f}, 0);
  EXPECT_TRUE(layout.cards.empty());
  EXPECT_FLOAT_EQ(layout.cardSize.x, 0.f);
}

TEST(StackCardRects, OffsetsEachLaterCard) {
  const sf::FloatRect base{10.f, 20.f, 50.f, 70.f};
  const std::vector<sf::FloatRect> rects = stackCardRects(base, 3);
  ASSERT_EQ(rects.size(), 3u);
  EXPECT_FLOAT_EQ(rects.at(0).left, 10.f);
  EXPECT_FLOAT_EQ(rects.at(1).left, 10.f + static_cast<float>(kStackOffsetX));
  EXPECT_FLOAT_EQ(rects.at(1).top, 20.f + static_cast<float>(kStackOffsetY));
  EXPECT_FLOAT_EQ(rects.at(2).left, 10.f + static_cast<float>(2 * kStackOffsetX));
}

TEST(IndexAtPoint, TopMostRectWins) {
  const std::vector<sf::FloatRect> rects{{0.f, 0.f, 100.f, 50.f}, {10.f, 10.f, 100.f, 50.f}};
  // The second (later) rect is on top: a point inside both resolves to it.
  const std::optional<std::size_t> hit = indexAtPoint(rects, {20.f, 20.f});
  if (hit.has_value()) {
    EXPECT_EQ(hit.value(), 1u);
  } else {
    FAIL() << "expected a hit on the overlapping rects";
  }
  // Outside everything: nullopt.
  EXPECT_FALSE(indexAtPoint(rects, {200.f, 200.f}).has_value());
  // Empty list: nullopt.
  EXPECT_FALSE(indexAtPoint({}, {10.f, 10.f}).has_value());
}

} // namespace
} // namespace mtgcpp::core
