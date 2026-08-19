// M10.3 cursor-state tests: each screen's pure `cursorAt(point)` hit-test —
// Hand over clickable widgets, Text over editable fields, Arrow elsewhere. The
// mapping is what the App applies to the OS cursor each frame.

#include "ui/cursor.h"
#include "ui/screens/deck_editor_screen.h"
#include "ui/screens/home_screen.h"
#include "ui/screens/lobby_screen.h"
#include "ui/screens/table_screen.h"

#include "state/board_state.h"
#include "ui/layout.h"

#include <gtest/gtest.h>

#include <SFML/Graphics.hpp>

#include <string>
#include <vector>

namespace mtgcpp::core {
namespace {

sf::Vector2f center(const sf::FloatRect &rect) {
  return {rect.left + (rect.width / 2.f), rect.top + (rect.height / 2.f)};
}

// The content band App hands a menu screen for a 960x600 window (the app's
// kMargin is 40 px at the reference scale).
sf::FloatRect contentFor(unsigned width) {
  const float margin = px(40.f, {width, 600u});
  return {margin, px(196.f, {width, 600u}), static_cast<float>(width) - (2.f * margin),
          (600.f - px(56.f, {width, 600u}) - px(16.f, {width, 600u})) - px(196.f, {width, 600u})};
}

DeckSummary summary(std::string name, int total) {
  DeckSummary s;
  s.id = name + "-id";
  s.name = std::move(name);
  s.format = "Standard";
  s.total_cards = total;
  return s;
}

TEST(Cursor, HomePickerShowsHandOverProfilesAndArrowElsewhere) {
  HomeScreen home;
  home.relayout(contentFor(960u), 1.f);
  EXPECT_EQ(home.cursorAt(center(home.profileButton(0).bounds())), CursorKind::Hand);
  EXPECT_EQ(home.cursorAt(center(home.profileButton(3).bounds())), CursorKind::Hand);
  EXPECT_EQ(home.cursorAt({5.f, 5.f}), CursorKind::Arrow);
}

TEST(Cursor, HomeVaultShowsHandOverDeckRowsAndButtons) {
  HomeScreen home;
  home.setPlayerId("carlo");
  home.setDecks({summary("Bears", 60), summary("Bolt", 60)});
  home.relayout(contentFor(960u), 1.f);

  EXPECT_EQ(home.cursorAt(center(home.newDeckButton().bounds())), CursorKind::Hand);
  // The deck-list pane itself is clickable (rows + per-row Delete buttons).
  const sf::FloatRect list = home.deckList().bounds();
  EXPECT_GT(list.width, 0.f);
  EXPECT_EQ(home.cursorAt({list.left + 5.f, list.top + 5.f}), CursorKind::Hand);
  EXPECT_EQ(home.cursorAt({5.f, 5.f}), CursorKind::Arrow);
}

TEST(Cursor, EditorShowsTextOverFieldsAndHandOverControls) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);

  EXPECT_EQ(editor.cursorAt(center(editor.searchInput().bounds())), CursorKind::Text);
  EXPECT_EQ(editor.cursorAt(center(editor.deckNameInput().bounds())), CursorKind::Text);
  EXPECT_EQ(editor.cursorAt(center(editor.searchButton().bounds())), CursorKind::Hand);
  EXPECT_EQ(editor.cursorAt(center(editor.backButton().bounds())), CursorKind::Hand);
  EXPECT_EQ(editor.cursorAt({5.f, 5.f}), CursorKind::Arrow);
}

TEST(Cursor, LobbyShowsTextOverJoinFieldAndHandOverButtons) {
  LobbyScreen lobby;
  lobby.relayout({40.f, 196.f, 880.f, 300.f}, 1.f);

  EXPECT_EQ(lobby.cursorAt(center(lobby.joinInput().bounds())), CursorKind::Text);
  EXPECT_EQ(lobby.cursorAt(center(lobby.createButton().bounds())), CursorKind::Hand);
  EXPECT_EQ(lobby.cursorAt(center(lobby.joinButton().bounds())), CursorKind::Hand);
  EXPECT_EQ(lobby.cursorAt({5.f, 5.f}), CursorKind::Arrow);
}

TEST(Cursor, TableShowsHandOverOwnCardsAndRingsButArrowOverEnemyHalf) {
  // A board where the host has a 2-card hand and the guest has one zone card.
  state::BoardState board = state::initialBoardState();
  SeatBoard host = emptySeatBoard();
  BoardCard forest;
  forest.id = "host-1";
  forest.name = "Forest";
  host.hand.push_back(forest);
  board.seats.at(0) = host;
  SeatBoard guest = emptySeatBoard();
  BoardCard bear;
  bear.id = "guest-1";
  bear.name = "Bear";
  guest.zones.at(state::zoneIndex(PlayerZone::Creatures)).push_back(bear);
  board.seats.at(1) = guest;

  TableScreen table;
  table.setBoard(board);
  table.setRole(PlayerSeat::Host);
  table.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  // The host's hand card is interactive.
  constexpr float kGap = 6.f;
  constexpr float kToolbarHeight = 36.f;
  const sf::FloatRect battlefield{0.f, kToolbarHeight + kGap, 960.f, 600.f - kToolbarHeight - kGap};
  const TableBands bands = tableBands(battlefield, kGap, 0.44f);
  const std::vector<sf::FloatRect> hand =
      handCardRects(bands.myHand, host.hand.size(), bands.myHand.height * 0.12f, false);
  ASSERT_EQ(hand.size(), 1u);
  EXPECT_EQ(table.cursorAt(center(hand.at(0))), CursorKind::Hand);
  // The host's life ring is clickable.
  EXPECT_EQ(table.cursorAt(center(table.lifeRings().at(state::seatIndex(PlayerSeat::Host)))),
            CursorKind::Hand);
  // The opponent's zone card is read-only: arrow, not a hand.
  const sf::FloatRect guestZone = tableZoneRect(bands.theirGrid, PlayerZone::Creatures);
  EXPECT_EQ(table.cursorAt(center(guestZone)), CursorKind::Arrow);
  // Open playmat: arrow.
  EXPECT_EQ(table.cursorAt({480.f, 300.f}), CursorKind::Arrow);
}

} // namespace
} // namespace mtgcpp::core
