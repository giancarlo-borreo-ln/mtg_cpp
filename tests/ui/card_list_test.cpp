// M5.1 CardList widget tests: search-result row hit-testing, add-queuing and
// scrolling. Pure rect math, no display needed.

#include "core/card.h"
#include "ui/widgets/card_list.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <SFML/System/Vector2.hpp>

namespace mtgcpp::core {
namespace {

Card resultCard(std::string name, const std::string &set, const std::string &number) {
  Card c;
  c.name = std::move(name);
  c.set_code = set;
  c.collector_number = number;
  return c;
}

TEST(CardList, RowAtMapsPointsToRows) {
  CardList list;
  list.setPosition({0.f, 0.f});
  list.setSize({200.f, 180.f});
  list.setRowHeight(60.f);
  list.setCards(
      {resultCard("A", "sta", "1"), resultCard("B", "war", "2"), resultCard("C", "m20", "3")});

  EXPECT_EQ(list.rowAt({5.f, 2.f}), std::make_optional<std::size_t>(0));
  EXPECT_EQ(list.rowAt({5.f, 60.f}), std::make_optional<std::size_t>(1));
  EXPECT_EQ(list.rowAt({5.f, 179.f}), std::make_optional<std::size_t>(2));
  EXPECT_FALSE(list.rowAt({5.f, 181.f}).has_value()); // outside the widget
  EXPECT_FALSE(list.rowAt({300.f, 10.f}).has_value());
}

TEST(CardList, RowAtAccountsForTheScrollOffset) {
  CardList list;
  list.setPosition({0.f, 0.f});
  list.setSize({200.f, 120.f});
  list.setRowHeight(60.f);
  list.setCards(
      {resultCard("A", "sta", "1"), resultCard("B", "war", "2"), resultCard("C", "m20", "3")});
  list.setScrollOffset(1);

  // Visible rows are B (index 1) and C (index 2).
  EXPECT_EQ(list.rowAt({5.f, 2.f}), std::make_optional<std::size_t>(1));  // B
  EXPECT_EQ(list.rowAt({5.f, 70.f}), std::make_optional<std::size_t>(2)); // C

  // Scrolled to the last item: the second visible row is past the items.
  list.setScrollOffset(2);
  EXPECT_EQ(list.rowAt({5.f, 2.f}), std::make_optional<std::size_t>(2)); // C
  EXPECT_FALSE(list.rowAt({5.f, 70.f}).has_value());                     // index 3, past items
}

TEST(CardList, ClickingARowQueuesAnAddExactlyOnce) {
  CardList list;
  list.setPosition({0.f, 0.f});
  list.setSize({200.f, 120.f});
  list.setRowHeight(60.f);
  list.setCards({resultCard("A", "sta", "1"), resultCard("B", "war", "2")});

  EXPECT_TRUE(list.mousePressed({5.f, 70.f})); // second row
  EXPECT_EQ(list.consumeAdd(), std::make_optional<std::size_t>(1));
  EXPECT_FALSE(list.consumeAdd().has_value()); // delivered exactly once
}

TEST(CardList, ClickingOutsideQueuesNothing) {
  CardList list;
  list.setPosition({0.f, 0.f});
  list.setSize({200.f, 120.f});
  list.setRowHeight(60.f);
  list.setCards({resultCard("A", "sta", "1")});

  EXPECT_FALSE(list.mousePressed({300.f, 10.f}));
  EXPECT_FALSE(list.consumeAdd().has_value());
}

TEST(CardList, SettingNewCardsResetsScrollAndPendingAdds) {
  CardList list;
  list.setPosition({0.f, 0.f});
  list.setSize({200.f, 120.f});
  list.setRowHeight(60.f);
  list.setCards({resultCard("A", "sta", "1")});
  list.mousePressed({5.f, 10.f});
  EXPECT_TRUE(list.consumeAdd().has_value());

  list.setCards({resultCard("B", "war", "2"), resultCard("C", "m20", "3")});
  EXPECT_EQ(list.scrollOffset(), 0u);
  EXPECT_FALSE(list.consumeAdd().has_value());
}

TEST(CardList, VisibleRowCountFollowsSizeAndRowHeight) {
  CardList list;
  list.setSize({200.f, 180.f});
  list.setRowHeight(60.f);
  EXPECT_EQ(list.visibleRowCount(), 3u);
}

TEST(CardList, ScrollOffsetClampsToTheItemCount) {
  CardList list;
  list.setCards({resultCard("A", "sta", "1"), resultCard("B", "war", "2")});
  list.setScrollOffset(99);
  EXPECT_EQ(list.scrollOffset(), 2u);
}

TEST(CardList, EmptyListReportsNoRows) {
  CardList list;
  EXPECT_TRUE(list.empty());
  EXPECT_FALSE(list.rowAt({5.f, 5.f}).has_value());
}

} // namespace
} // namespace mtgcpp::core
