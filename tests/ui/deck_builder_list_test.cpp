// M5.1 DeckBuilderList widget tests: quantity-control hit-testing (minus,
// plus, remove) and the queued-action semantics. Pure rect math, no display.

#include "core/card.h"
#include "ui/widgets/deck_builder_list.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

namespace mtgcpp::core {
namespace {

Card deckCard(std::string name, int quantity) {
  Card c;
  c.name = std::move(name);
  c.set_code = "sta";
  c.collector_number = "1";
  c.quantity = quantity;
  c.type_line = "Instant";
  return c;
}

// The control rect for `which` (minus/plus/remove) on the first visible row,
// mirroring the widget's private math: the cluster is right-aligned inside a
// row of the given geometry.
sf::FloatRect controlRectFor(sf::Vector2f pos, sf::Vector2f size, float rowHeight,
                             const std::string &which) {
  const float buttonHeight = 30.f;
  const float mid = pos.y + (rowHeight / 2.f);
  const float removeLeft = pos.x + size.x - 40.f - 10.f;
  const float plusLeft = removeLeft - 6.f - 36.f;
  const float minusLeft = plusLeft - 6.f - 36.f - 6.f - 36.f;
  if (which == "remove") {
    return {removeLeft, mid - (buttonHeight / 2.f), 40.f, buttonHeight};
  }
  if (which == "plus") {
    return {plusLeft, mid - (buttonHeight / 2.f), 36.f, buttonHeight};
  }
  return {minusLeft, mid - (buttonHeight / 2.f), 36.f, buttonHeight};
}

TEST(DeckBuilderList, RowAtMapsPointsToRows) {
  DeckBuilderList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 180.f});
  list.setRowHeight(60.f);
  list.setCards({deckCard("A", 1), deckCard("B", 2), deckCard("C", 3)});

  EXPECT_EQ(list.rowAt({200.f, 2.f}), std::make_optional<std::size_t>(0));
  EXPECT_EQ(list.rowAt({200.f, 60.f}), std::make_optional<std::size_t>(1));
  EXPECT_EQ(list.rowAt({200.f, 179.f}), std::make_optional<std::size_t>(2));
  EXPECT_FALSE(list.rowAt({200.f, 181.f}).has_value());
}

TEST(DeckBuilderList, ControlHitTestsReturnTheRightRow) {
  DeckBuilderList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 120.f});
  list.setRowHeight(60.f);
  list.setCards({deckCard("A", 1), deckCard("B", 2)});

  // First row's controls.
  const sf::FloatRect plus = controlRectFor(list.position(), list.size(), list.rowHeight(), "plus");
  const sf::FloatRect minus =
      controlRectFor(list.position(), list.size(), list.rowHeight(), "minus");
  const sf::FloatRect remove =
      controlRectFor(list.position(), list.size(), list.rowHeight(), "remove");
  EXPECT_EQ(list.plusAt({plus.left + (plus.width / 2.f), plus.top + (plus.height / 2.f)}),
            std::make_optional<std::size_t>(0));
  EXPECT_EQ(list.minusAt({minus.left + (minus.width / 2.f), minus.top + (minus.height / 2.f)}),
            std::make_optional<std::size_t>(0));
  EXPECT_EQ(list.removeAt({remove.left + (remove.width / 2.f), remove.top + (remove.height / 2.f)}),
            std::make_optional<std::size_t>(0));

  // The row body (far left) is not a control.
  EXPECT_FALSE(list.plusAt({5.f, 30.f}).has_value());
  EXPECT_FALSE(list.minusAt({5.f, 30.f}).has_value());
  EXPECT_FALSE(list.removeAt({5.f, 30.f}).has_value());
}

TEST(DeckBuilderList, ClickingPlusQueuesAnIncrement) {
  DeckBuilderList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 120.f});
  list.setRowHeight(60.f);
  list.setCards({deckCard("A", 1), deckCard("B", 2)});

  const sf::FloatRect plus = controlRectFor(list.position(), list.size(), list.rowHeight(), "plus");
  const sf::Vector2f point{plus.left + (plus.width / 2.f), plus.top + (plus.height / 2.f)};
  EXPECT_TRUE(list.mousePressed(point));
  EXPECT_EQ(list.consumeIncrement(), std::make_optional<std::size_t>(0));
  EXPECT_FALSE(list.consumeIncrement().has_value()); // exactly once
  EXPECT_FALSE(list.consumeDecrement().has_value());
  EXPECT_FALSE(list.consumeRemove().has_value());
}

TEST(DeckBuilderList, ClickingMinusQueuesADecrement) {
  DeckBuilderList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 120.f});
  list.setRowHeight(60.f);
  list.setCards({deckCard("A", 1)});

  const sf::FloatRect minus =
      controlRectFor(list.position(), list.size(), list.rowHeight(), "minus");
  const sf::Vector2f point{minus.left + (minus.width / 2.f), minus.top + (minus.height / 2.f)};
  EXPECT_TRUE(list.mousePressed(point));
  EXPECT_EQ(list.consumeDecrement(), std::make_optional<std::size_t>(0));
}

TEST(DeckBuilderList, ClickingRemoveQueuesARemoval) {
  DeckBuilderList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 120.f});
  list.setRowHeight(60.f);
  list.setCards({deckCard("A", 1)});

  const sf::FloatRect remove =
      controlRectFor(list.position(), list.size(), list.rowHeight(), "remove");
  const sf::Vector2f point{remove.left + (remove.width / 2.f), remove.top + (remove.height / 2.f)};
  EXPECT_TRUE(list.mousePressed(point));
  EXPECT_EQ(list.consumeRemove(), std::make_optional<std::size_t>(0));
  EXPECT_FALSE(list.consumeIncrement().has_value()); // remove wins, no increment
}

TEST(DeckBuilderList, ControlsRespectTheScrollOffset) {
  DeckBuilderList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 120.f});
  list.setRowHeight(60.f);
  list.setCards({deckCard("A", 1), deckCard("B", 2), deckCard("C", 3)});
  list.setScrollOffset(1); // visible rows are B (row 1) and C (row 2)

  // Click the plus of the first VISIBLE row -> deck row 1 (B).
  const sf::FloatRect plus = controlRectFor(list.position(), list.size(), list.rowHeight(), "plus");
  const sf::Vector2f point{plus.left + (plus.width / 2.f), plus.top + (plus.height / 2.f)};
  EXPECT_TRUE(list.mousePressed(point));
  EXPECT_EQ(list.consumeIncrement(), std::make_optional<std::size_t>(1));
}

TEST(DeckBuilderList, SettingNewCardsResetsPendingControls) {
  DeckBuilderList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 120.f});
  list.setRowHeight(60.f);
  list.setCards({deckCard("A", 1)});
  list.mousePressed({5.f, 30.f}); // row body: not a control

  list.setCards({deckCard("B", 2)});
  EXPECT_FALSE(list.consumeIncrement().has_value());
  EXPECT_FALSE(list.consumeDecrement().has_value());
  EXPECT_FALSE(list.consumeRemove().has_value());
}

TEST(DeckBuilderList, EmptyListReportsNoControls) {
  DeckBuilderList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 120.f});
  list.setRowHeight(60.f);

  EXPECT_TRUE(list.empty());
  EXPECT_FALSE(list.minusAt({10.f, 10.f}).has_value());
  EXPECT_FALSE(list.plusAt({10.f, 10.f}).has_value());
  EXPECT_FALSE(list.removeAt({10.f, 10.f}).has_value());
}

} // namespace
} // namespace mtgcpp::core
