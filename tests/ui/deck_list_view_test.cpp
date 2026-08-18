// M4.3 DeckListView tests: pure row hit-testing, selection, per-row Delete
// button hit-testing and scrolling. Rendering (draw) needs a window and is NOT
// exercised here — the "smoke" is that every interaction mutates state without
// a display; the pixels are verified in the running app.

#include "ui/widgets/deck_list_view.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <SFML/System/Vector2.hpp>

namespace mtgcpp::core {
namespace {

// A minimal saved deck summary with a distinct name.
DeckSummary deck(std::string name, int total) {
  DeckSummary summary;
  summary.id = name + "-id";
  summary.name = std::move(name);
  summary.format = "Standard";
  summary.total_cards = total;
  return summary;
}

// The list geometry used by most tests: two rows of 68px in a 600x136 slot.
// The Delete button of a row is right-aligned: 84px wide, 30px tall, 10px from
// the row's right edge, vertically centered.
constexpr float kRowHeight = 68.f;

TEST(DeckListView, ContainsOnlyInsideItsRect) {
  DeckListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({600.f, kRowHeight});
  list.setRowHeight(kRowHeight);
  list.setDecks({deck("A", 1)});

  EXPECT_FALSE(list.contains({-1.f, 10.f}));
  EXPECT_TRUE(list.contains({300.f, 10.f}));
  EXPECT_FALSE(list.contains({300.f, 200.f}));
}

TEST(DeckListView, RowAtMapsPointsToDeckRows) {
  DeckListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({600.f, 2.f * kRowHeight});
  list.setRowHeight(kRowHeight);
  list.setDecks({deck("A", 1), deck("B", 2)});

  EXPECT_EQ(list.rowAt({300.f, 10.f}), std::make_optional<std::size_t>(0));
  EXPECT_EQ(list.rowAt({300.f, kRowHeight + 10.f}), std::make_optional<std::size_t>(1));
  EXPECT_FALSE(list.rowAt({300.f, (2.f * kRowHeight) + 5.f}).has_value());
}

TEST(DeckListView, RowAtAccountsForTheScrollOffset) {
  DeckListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({600.f, kRowHeight});
  list.setRowHeight(kRowHeight);
  list.setDecks({deck("A", 1), deck("B", 2), deck("C", 3)});
  list.setScrollOffset(1);

  // The first visible row is B (index 1), not A.
  EXPECT_EQ(list.rowAt({300.f, 10.f}), std::make_optional<std::size_t>(1));
}

TEST(DeckListView, DeleteAtHitsOnlyTheDeleteButton) {
  DeckListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({600.f, kRowHeight});
  list.setRowHeight(kRowHeight);
  list.setDecks({deck("A", 1)});

  // The Delete button: x 506..590, y 19..49.
  EXPECT_EQ(list.deleteAt({550.f, 30.f}), std::make_optional<std::size_t>(0));
  EXPECT_FALSE(list.deleteAt({400.f, 30.f}).has_value()); // row body, not the button
  EXPECT_FALSE(list.deleteAt({550.f, 60.f}).has_value()); // below the button
  EXPECT_FALSE(list.deleteAt({700.f, 30.f}).has_value()); // outside the widget
}

TEST(DeckListView, DeleteAtTracksTheScrolledRow) {
  DeckListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({600.f, kRowHeight});
  list.setRowHeight(kRowHeight);
  list.setDecks({deck("A", 1), deck("B", 2), deck("C", 3)});
  list.setScrollOffset(1); // B is now the first visible row

  // The visible Delete button belongs to B (index 1), not A.
  EXPECT_EQ(list.deleteAt({550.f, 30.f}), std::make_optional<std::size_t>(1));
}

TEST(DeckListView, RowClickSelectsButNeverQueuesADelete) {
  DeckListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({600.f, 2.f * kRowHeight});
  list.setRowHeight(kRowHeight);
  list.setDecks({deck("A", 1), deck("B", 2)});

  EXPECT_TRUE(list.mousePressed({300.f, kRowHeight + 10.f}));
  EXPECT_EQ(list.selectedIndex(), std::make_optional<std::size_t>(1));
  EXPECT_FALSE(list.consumeDelete().has_value());
}

TEST(DeckListView, DeleteButtonClickQueuesExactlyOneDeleteAndNoSelection) {
  DeckListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({600.f, 2.f * kRowHeight});
  list.setRowHeight(kRowHeight);
  list.setDecks({deck("A", 1), deck("B", 2)});

  EXPECT_TRUE(list.mousePressed({550.f, 30.f}));
  // A Delete-button click must never also select the deck.
  EXPECT_FALSE(list.selectedIndex().has_value());
  EXPECT_EQ(list.consumeDelete(), std::make_optional<std::size_t>(0));
  EXPECT_FALSE(list.consumeDelete().has_value()); // delivered exactly once
}

TEST(DeckListView, ClickOutsideIsNotConsumed) {
  DeckListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({600.f, kRowHeight});
  list.setRowHeight(kRowHeight);
  list.setDecks({deck("A", 1)});

  EXPECT_FALSE(list.mousePressed({700.f, 30.f}));
  EXPECT_FALSE(list.selectedIndex().has_value());
  EXPECT_FALSE(list.consumeDelete().has_value());
}

TEST(DeckListView, SelectionClampsToTheDecks) {
  DeckListView list;
  list.setDecks({deck("A", 1), deck("B", 2)});

  list.setSelected(1);
  EXPECT_EQ(list.selectedIndex(), std::make_optional<std::size_t>(1));

  list.setSelected(5);
  EXPECT_FALSE(list.selectedIndex().has_value());
}

TEST(DeckListView, VisibleRowCountFollowsSizeAndRowHeight) {
  DeckListView list;
  list.setSize({600.f, 3.f * kRowHeight});
  list.setRowHeight(kRowHeight);
  EXPECT_EQ(list.visibleRowCount(), 3u);
}

TEST(DeckListView, ScrollOffsetClampsToTheDeckCount) {
  DeckListView list;
  list.setDecks({deck("A", 1), deck("B", 2)});
  list.setScrollOffset(99);
  EXPECT_EQ(list.scrollOffset(), 2u);
}

TEST(DeckListView, SetDecksReplacesAndClearsState) {
  DeckListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({600.f, kRowHeight});
  list.setRowHeight(kRowHeight);
  list.setDecks({deck("A", 1)});
  list.mousePressed({550.f, 30.f}); // queues a delete + no selection
  list.setSelected(0);
  list.setScrollOffset(0);

  list.setDecks({deck("B", 2), deck("C", 3)});
  EXPECT_EQ(list.decks().size(), 2u);
  EXPECT_EQ(list.decks().at(0).name, "B");
  EXPECT_FALSE(list.selectedIndex().has_value());
  EXPECT_FALSE(list.consumeDelete().has_value());
  EXPECT_EQ(list.scrollOffset(), 0u);
}

TEST(DeckListView, RowMetricsScaleWithTheUiScale) {
  // At scale 2 the Delete button doubles in size and moves in by the doubled
  // gutter: x = 600 - 168 - 20 = 412..580, y = (68 - 60)/2 = 4..64.
  DeckListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({600.f, kRowHeight});
  list.setRowHeight(kRowHeight);
  list.setScale(2.f);
  list.setDecks({deck("A", 1)});

  EXPECT_EQ(list.deleteAt({550.f, 30.f}), std::make_optional<std::size_t>(0));
  EXPECT_FALSE(list.deleteAt({600.f, 30.f}).has_value());
  EXPECT_FALSE(list.deleteAt({540.f, 80.f}).has_value()); // below the taller button
}

} // namespace
} // namespace mtgcpp::core
