// M9.4 ContextMenu tests: item hit-testing and press/release click semantics.

#include "ui/widgets/context_menu.h"

#include <gtest/gtest.h>

#include <SFML/Graphics.hpp>

namespace mtgcpp::core {
namespace {

// clang-tidy 22 does not model ASSERT_* as a guard for .value(); use this
// helper instead of guarding every optional manually (see prompt.md §6).
template <typename T> const T &expectValue(const std::optional<T> &opt) {
  if (opt.has_value()) {
    return opt.value();
  }
  ADD_FAILURE() << "expected an optional value";
  static const T empty{};
  return empty;
}

sf::Event mouseClick(float x, float y) {
  sf::Event event;
  event.type = sf::Event::MouseButtonPressed;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

ContextMenu menuWithItems() {
  ContextMenu menu;
  menu.setItems({"Tap", "+1/+1 Counter", "Create Token", "Flip to back", "Move..."});
  menu.setPosition({100.f, 50.f});
  menu.setItemHeight(30.f);
  menu.setWidth(160.f);
  return menu;
}

TEST(ContextMenu, ItemAtMapsYToAnIndex) {
  const ContextMenu menu = menuWithItems();
  EXPECT_EQ(menu.itemAt({100.f, 50.f}), 0u);
  EXPECT_EQ(menu.itemAt({100.f, 79.f}), 0u); // last row pixel of item 0
  EXPECT_EQ(menu.itemAt({100.f, 80.f}), 1u); // first pixel of item 1
  EXPECT_EQ(menu.itemAt({100.f, 50.f + (4.f * 30.f)}), 4u);
  EXPECT_FALSE(menu.itemAt({50.f, 50.f}).has_value());   // left of the menu
  EXPECT_FALSE(menu.itemAt({100.f, 200.f}).has_value()); // below the last item
}

TEST(ContextMenu, ClickFiresOnPressThenReleaseInside) {
  ContextMenu menu = menuWithItems();
  EXPECT_TRUE(menu.mousePressed({100.f, 50.f})); // item 0
  EXPECT_FALSE(menu.consumeSelection().has_value());
  EXPECT_TRUE(menu.mouseReleased({100.f, 50.f}));
  const std::optional<std::size_t> selected = menu.consumeSelection();
  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(expectValue(selected), 0u);
  EXPECT_FALSE(menu.consumeSelection().has_value()); // delivered exactly once
}

TEST(ContextMenu, ReleaseOutsideCancelsThePress) {
  ContextMenu menu = menuWithItems();
  menu.mousePressed({100.f, 50.f});
  menu.mouseReleased({100.f, 300.f}); // released off the menu
  EXPECT_FALSE(menu.consumeSelection().has_value());
}

TEST(ContextMenu, PressOutsideNeverPresses) {
  ContextMenu menu = menuWithItems();
  EXPECT_FALSE(menu.mousePressed({0.f, 0.f}));
  EXPECT_FALSE(menu.consumeSelection().has_value());
}

TEST(ContextMenu, HandleEventRoutesMouseEvents) {
  ContextMenu menu = menuWithItems();
  const sf::Event press = mouseClick(100.f, 50.f);
  EXPECT_TRUE(menu.handleEvent(press));
  sf::Event release;
  release.type = sf::Event::MouseButtonReleased;
  release.mouseButton.button = sf::Mouse::Left;
  release.mouseButton.x = 100;
  release.mouseButton.y = 50;
  EXPECT_TRUE(menu.handleEvent(release));
  EXPECT_TRUE(menu.consumeSelection().has_value());
}

TEST(ContextMenu, ClearRemovesAllItems) {
  ContextMenu menu = menuWithItems();
  menu.clear();
  EXPECT_TRUE(menu.empty());
  EXPECT_FALSE(menu.itemAt({100.f, 50.f}).has_value());
}

TEST(ContextMenu, SetItemsResetsHoverAndPress) {
  ContextMenu menu = menuWithItems();
  menu.mousePressed({100.f, 50.f});
  menu.setItems({"Only one"});
  // The press state was dropped with the old items; no stale selection.
  EXPECT_FALSE(menu.mouseReleased({100.f, 50.f}));
  EXPECT_FALSE(menu.consumeSelection().has_value());
}

} // namespace
} // namespace mtgcpp::core
