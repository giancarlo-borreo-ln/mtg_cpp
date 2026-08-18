// M4.3 HomeScreen tests: the picker -> vault flow, deck selection/deletion,
// action buttons, and the responsive grid reflow at different window sizes.
// Everything here is headless: the screen's widgets are pure math over rects,
// so a synthesized click + relayout drives the full logic.

#include "ui/layout.h"
#include "ui/screens/home_screen.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

namespace mtgcpp::core {
namespace {

// The chrome margin app.cpp uses for the content band (mirrored here so the
// test drives the same content rect the real App hands to the Home screen).
constexpr float kMargin = 40.f;

// A synthetic left-button press/release pair at (x, y).
sf::Event mousePress(float x, float y) {
  sf::Event event{};
  event.type = sf::Event::MouseButtonPressed;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

sf::Event mouseRelease(float x, float y) {
  sf::Event event{};
  event.type = sf::Event::MouseButtonReleased;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

// Complete a click on a widget: press then release inside it, then drain the
// resulting action.
HomeAction click(HomeScreen &home, sf::Vector2f point) {
  home.routeEvent(mousePress(point.x, point.y));
  home.routeEvent(mouseRelease(point.x, point.y));
  return home.pollAction();
}

sf::Vector2f center(const sf::FloatRect &rect) {
  return {rect.left + (rect.width / 2.f), rect.top + (rect.height / 2.f)};
}

// The content band App hands to the Home screen for a window of `width` x 600
// (mirrors the relayout() math in app.cpp for the reference height).
sf::FloatRect contentFor(unsigned width) {
  const float margin = px(kMargin, {width, 600u});
  return {margin, px(196.f, {width, 600u}), static_cast<float>(width) - (2.f * margin),
          (600.f - px(56.f, {width, 600u}) - px(16.f, {width, 600u})) - px(196.f, {width, 600u})};
}

DeckSummary deck(std::string name, int total) {
  DeckSummary summary;
  summary.id = name + "-id";
  summary.name = std::move(name);
  summary.format = "Standard";
  summary.total_cards = total;
  return summary;
}

TEST(HomeScreen, StartsInTheProfilePicker) {
  HomeScreen home;
  EXPECT_TRUE(home.isProfilePicker());
  EXPECT_FALSE(home.playerId().has_value());
  EXPECT_EQ(home.pollAction(), HomeAction::None);
}

TEST(HomeScreen, RelayoutPositionsFourDistinctProfileCards) {
  HomeScreen home;
  home.relayout(contentFor(960u), 1.f);
  for (std::size_t i = 0; i < 4; ++i) {
    const sf::FloatRect rect = home.profileButton(i).bounds();
    EXPECT_GT(rect.width, 0.f) << i;
    EXPECT_GT(rect.height, 0.f) << i;
    EXPECT_TRUE(home.profileButton(i).contains(center(rect))) << i;
  }
  // All four cards live in different cells (no two overlap).
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t j = i + 1; j < 4; ++j) {
      const sf::FloatRect other = home.profileButton(j).bounds();
      EXPECT_FALSE(home.profileButton(i).contains({other.left, other.top})) << i << " vs " << j;
    }
  }
}

TEST(HomeScreen, ClickingAProfileCardSelectsItAndFlipsToTheVault) {
  HomeScreen home;
  home.relayout(contentFor(960u), 1.f);

  // Profile index 2 in the fixed list is "giancarlo".
  const sf::Vector2f point = center(home.profileButton(2).bounds());
  EXPECT_EQ(click(home, point), HomeAction::SelectProfile);
  const std::optional<std::string> &id = home.playerId();
  if (id.has_value()) {
    EXPECT_EQ(id.value(), "giancarlo");
  } else {
    FAIL() << "expected the picked profile to be active";
  }
  EXPECT_FALSE(home.isProfilePicker());
}

TEST(HomeScreen, ClearPlayerIdReturnsToThePicker) {
  HomeScreen home;
  home.setPlayerId("carlo");
  EXPECT_FALSE(home.isProfilePicker());
  home.setPlayerId(std::nullopt);
  EXPECT_TRUE(home.isProfilePicker());
}

TEST(HomeScreen, VaultStartsWithNoDecksAndAnEmptyState) {
  HomeScreen home;
  home.setPlayerId("carlo");
  home.relayout(contentFor(960u), 1.f);
  EXPECT_FALSE(home.hasDecks());
  EXPECT_TRUE(home.decks().empty());
  // The empty state is a no-op for polling: no action is pending.
  EXPECT_EQ(home.pollAction(), HomeAction::None);
}

TEST(HomeScreen, DeckClickSelectsTheClickedRow) {
  HomeScreen home;
  home.setPlayerId("carlo");
  home.setDecks({deck("A", 1), deck("B", 2), deck("C", 3)});
  home.relayout(contentFor(960u), 1.f);

  const sf::Vector2f list = home.deckList().position();
  const float rowHeight = home.deckList().rowHeight();
  // Click the second row, left of the Delete button (x = list x + 50).
  const sf::Vector2f point{list.x + 50.f, list.y + (1.5f * rowHeight)};
  EXPECT_EQ(click(home, point), HomeAction::None); // selection is not an action
  EXPECT_EQ(home.deckList().selectedIndex(), std::make_optional<std::size_t>(1));
}

TEST(HomeScreen, DeleteButtonReportsTheDeckToRemove) {
  HomeScreen home;
  home.setPlayerId("carlo");
  home.setDecks({deck("A", 1), deck("B", 2), deck("C", 3)});
  home.relayout(contentFor(960u), 1.f);

  // Delete button of the first row: right-aligned, 84 wide, 30 tall, 10 gutter.
  const sf::FloatRect listBounds = home.deckList().bounds();
  const float rowHeight = home.deckList().rowHeight();
  const sf::Vector2f point{listBounds.left + listBounds.width - 10.f - 42.f,
                           listBounds.top + (0.5f * rowHeight)};
  EXPECT_EQ(click(home, point), HomeAction::DeleteDeck);
  EXPECT_EQ(home.deleteIndex(), std::make_optional<std::size_t>(0));
  // Deleting must never also select the row.
  EXPECT_FALSE(home.deckList().selectedIndex().has_value());
}

TEST(HomeScreen, HighlightedDeckIsNullUntilASelectionIsMade) {
  HomeScreen home;
  home.setPlayerId("carlo");
  home.setDecks({deck("A", 1), deck("B", 2)});
  home.relayout(contentFor(960u), 1.f);

  EXPECT_EQ(home.highlightedDeck(), nullptr);
}

TEST(HomeScreen, HighlightingARowResolvesTheDetailDeck) {
  HomeScreen home;
  home.setPlayerId("carlo");
  home.setDecks({deck("A", 1), deck("B", 2), deck("C", 3)});
  home.relayout(contentFor(960u), 1.f);

  // Select the middle row (left of the Delete button).
  const sf::Vector2f list = home.deckList().position();
  const float rowHeight = home.deckList().rowHeight();
  click(home, {list.x + 50.f, list.y + (1.5f * rowHeight)});

  const DeckSummary *highlighted = home.highlightedDeck();
  ASSERT_NE(highlighted, nullptr);
  EXPECT_EQ(highlighted->name, "B");
}

TEST(HomeScreen, DetailPaneFollowsAReflowAndADelete) {
  HomeScreen home;
  home.setPlayerId("carlo");
  home.setDecks({deck("A", 1), deck("B", 2)});
  home.relayout(contentFor(640u), 1.f);
  const sf::Vector2f list = home.deckList().position();
  click(home, {list.x + 50.f, list.y + (0.5f * home.deckList().rowHeight())});
  ASSERT_NE(home.highlightedDeck(), nullptr);
  EXPECT_EQ(home.highlightedDeck()->name, "A");

  // Replacing the decks (as the App does after a delete) clears the highlight.
  home.setDecks({deck("C", 3)});
  EXPECT_EQ(home.highlightedDeck(), nullptr);
}

TEST(HomeScreen, ActionButtonsReportPlayNewAndSwitch) {
  HomeScreen home;
  home.setPlayerId("carlo");
  home.setDecks({deck("A", 1)});
  home.relayout(contentFor(960u), 1.f);

  // Click each action button at its own center and check the reported action.
  EXPECT_EQ(click(home, center(home.playButton().bounds())), HomeAction::PlayOnline);
  EXPECT_EQ(click(home, center(home.newDeckButton().bounds())), HomeAction::NewDeck);
  EXPECT_EQ(click(home, center(home.switchButton().bounds())), HomeAction::SwitchPlayer);
}

TEST(HomeScreen, EditDeckOpensTheHighlightedDeck) {
  HomeScreen home;
  home.setPlayerId("carlo");
  home.setDecks({deck("A", 1), deck("B", 2)});
  home.relayout(contentFor(960u), 1.f);

  // Without a highlight the Edit button is inert (no deck to open).
  EXPECT_EQ(click(home, center(home.editButton().bounds())), HomeAction::None);

  // Highlight the second row, then Edit reports the deck to open.
  const sf::Vector2f list = home.deckList().position();
  const float rowHeight = home.deckList().rowHeight();
  EXPECT_EQ(click(home, {list.x + 50.f, list.y + (1.5f * rowHeight)}), HomeAction::None);
  ASSERT_NE(home.highlightedDeck(), nullptr);
  EXPECT_EQ(home.highlightedDeck()->name, "B");

  EXPECT_EQ(click(home, center(home.editButton().bounds())), HomeAction::EditDeck);
  ASSERT_NE(home.highlightedDeck(), nullptr);
  EXPECT_EQ(home.highlightedDeck()->name, "B");
}

TEST(HomeScreen, PickerGridReflowsFromFourToThreeToTwoColumns) {
  HomeScreen home;

  // Wide (1280): 4 columns fit -> all cards in a single row (same top), the
  // last card pushed to the right of the first.
  home.relayout(contentFor(1280u), 1.f);
  EXPECT_FLOAT_EQ(home.profileButton(0).bounds().top, home.profileButton(3).bounds().top);
  EXPECT_GT(home.profileButton(3).bounds().left, home.profileButton(0).bounds().left);

  // Medium (960): only 3 columns fit the 200px minimum, so the last card wraps
  // to a second row directly under the first column.
  home.relayout(contentFor(960u), 1.f);
  EXPECT_GT(home.profileButton(3).bounds().top, home.profileButton(0).bounds().top);
  EXPECT_FLOAT_EQ(home.profileButton(3).bounds().left, home.profileButton(0).bounds().left);

  // Narrow (640): 2 columns -> the last card is on the second row, second col.
  home.relayout(contentFor(640u), 1.f);
  EXPECT_GT(home.profileButton(3).bounds().top, home.profileButton(1).bounds().top);
  EXPECT_FLOAT_EQ(home.profileButton(3).bounds().top, home.profileButton(2).bounds().top);
  EXPECT_FLOAT_EQ(home.profileButton(3).bounds().left, home.profileButton(1).bounds().left);
}

TEST(HomeScreen, LastProfileCardIsClickableAtEveryWindowSize) {
  for (const unsigned width : {640u, 960u, 1280u, 1920u}) {
    HomeScreen home;
    home.relayout(contentFor(width), 1.f);
    const sf::Vector2f point = center(home.profileButton(3).bounds());
    EXPECT_EQ(click(home, point), HomeAction::SelectProfile) << "width " << width;
    const std::optional<std::string> &id = home.playerId();
    if (id.has_value()) {
      EXPECT_EQ(id.value(), "nicola") << "width " << width;
    } else {
      FAIL() << "expected a profile to be picked at width " << width;
    }
  }
}

TEST(HomeScreen, RelayoutAfterViewChangeFlipsTheLayout) {
  HomeScreen home;
  home.relayout(contentFor(960u), 1.f);
  EXPECT_GT(home.profileButton(0).bounds().width, 0.f);

  home.setPlayerId("stefano");
  home.setDecks({deck("A", 1)});
  home.relayout(contentFor(960u), 1.f);
  EXPECT_FALSE(home.isProfilePicker());
  // The vault's action row and deck list are laid out and hittable.
  EXPECT_GT(home.switchButton().bounds().width, 0.f);
  EXPECT_GT(home.deckList().bounds().height, 0.f);
}

} // namespace
} // namespace mtgcpp::core
