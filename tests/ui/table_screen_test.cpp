// M9.4 + M9.5 TableScreen tests: click-to-select, the command menu, keyboard
// verbs, life editing and the hand-reveal flow. All pure logic — hit-testing
// and action building — driven through the same entry points the App uses.

#include "ui/screens/table_screen.h"

#include "state/board_state.h"
#include "ui/layout.h"

#include <gtest/gtest.h>

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace mtgcpp::core {
namespace {

// clang-tidy 22 does not model ASSERT_* as a guard for .value()/operator*;
// use this helper instead of guarding every optional manually (prompt.md §6).
template <typename T> const T &expectValue(const std::optional<T> &opt) {
  if (opt.has_value()) {
    return opt.value();
  }
  ADD_FAILURE() << "expected an optional value";
  static const T empty{};
  return empty;
}

// --- Fixtures ---------------------------------------------------------------

BoardCard bc(std::string id, std::string name, std::string type_line) {
  BoardCard card;
  card.id = std::move(id);
  card.name = std::move(name);
  card.type_line = std::move(type_line);
  return card;
}

// A board where both seats have a 2-card hand; the host also has a creature in
// the Creatures zone and one card on the shared Stack.
state::BoardState testBoard() {
  state::BoardState board = state::initialBoardState();
  SeatBoard host = emptySeatBoard();
  host.hand.push_back(bc("host-1", "Bears", "Creature - Bear"));
  host.hand.push_back(bc("host-2", "Forest", "Basic Land"));
  host.zones.at(state::zoneIndex(PlayerZone::Creatures))
      .push_back(bc("host-3", "Grizzly Bears", "Creature - Bear"));
  board.seats.at(0) = host;

  SeatBoard guest = emptySeatBoard();
  guest.hand.push_back(bc("guest-1", "Goblin", "Creature - Goblin"));
  board.seats.at(1) = guest;

  board.stack.push_back(bc("host-9", "Lightning Bolt", "Instant"));
  return board;
}

// The battlefield bands the screen lays out (mirrors TableScreen::relayout).
struct Geo {
  sf::FloatRect battlefield;
  TableBands bands;
};

Geo geometry(float width = 960.f, float height = 600.f) {
  constexpr float kGap = 6.f;
  constexpr float kToolbarHeight = 36.f;
  Geo geo;
  geo.battlefield = {0.f, kToolbarHeight + kGap, width, height - kToolbarHeight - kGap};
  geo.bands = tableBands(geo.battlefield, kGap, 0.44f);
  return geo;
}

sf::Vector2f centerOf(const sf::FloatRect &rect) {
  return {rect.left + (rect.width / 2.f), rect.top + (rect.height / 2.f)};
}

// The stack's base card rect (mirrors TableScreen::relayout).
sf::FloatRect stackBaseRect(const Geo &geo) {
  const float h = std::min(geo.bands.stack.height, geo.bands.stack.width / kCardAspect);
  const float w = h * kCardAspect;
  return {geo.bands.stack.left + ((geo.bands.stack.width - w) / 2.f),
          geo.bands.stack.top + ((geo.bands.stack.height - h) / 2.f), w, h};
}

// The hand-card rects for a seat's strip (the arch amplitude the screen uses).
std::vector<sf::FloatRect> handRects(const sf::FloatRect &strip, std::size_t count, bool archUp) {
  return handCardRects(strip, count, strip.height * 0.12f, archUp);
}

sf::Event mouseLeft(bool pressed, float x, float y) {
  sf::Event event;
  event.type = pressed ? sf::Event::MouseButtonPressed : sf::Event::MouseButtonReleased;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

sf::Event mouseRight(float x, float y) {
  sf::Event event;
  event.type = sf::Event::MouseButtonPressed;
  event.mouseButton.button = sf::Mouse::Right;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

sf::Event::KeyEvent key(sf::Keyboard::Key code) {
  sf::Event::KeyEvent keyEvent{};
  keyEvent.code = code;
  return keyEvent;
}

// --- Selection --------------------------------------------------------------

TEST(TableScreen, ClickingMyHandCardSelectsIt) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.myHand, 2, false);
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_TRUE(screen.mousePressed(centerOf(cards.at(0))));

  const CardSelection &sel = expectValue(screen.selection());
  EXPECT_EQ(sel.card.id, "host-1");
  EXPECT_TRUE(sel.in_hand);
  EXPECT_EQ(sel.seat, PlayerSeat::Host);
}

TEST(TableScreen, OpponentCardsAreReadOnly) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.theirHand, 1, true);
  ASSERT_EQ(cards.size(), 1u);
  screen.mousePressed(centerOf(cards.at(0)));
  EXPECT_FALSE(screen.selection().has_value());
}

TEST(TableScreen, StackCardsAreNotSelectable) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> stack = stackCardRects(stackBaseRect(geo), 1);
  ASSERT_EQ(stack.size(), 1u);
  screen.mousePressed(centerOf(stack.at(0)));
  EXPECT_FALSE(screen.selection().has_value());
}

TEST(TableScreen, ClickingEmptySpaceClearsTheSelection) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.myHand, 2, false);
  screen.mousePressed(centerOf(cards.at(0)));
  ASSERT_TRUE(screen.selection().has_value());

  // A point in the empty stack band clears it.
  screen.mousePressed({960.f / 2.f, centerOf(geo.bands.stack).y});
  EXPECT_FALSE(screen.selection().has_value());
}

TEST(TableScreen, ClickingMyZoneCardSelectsIt) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const sf::FloatRect zone = tableZoneRect(geo.bands.myGrid, PlayerZone::Creatures);
  screen.mousePressed(centerOf(zone));
  const CardSelection &zoneSel = expectValue(screen.selection());
  EXPECT_EQ(zoneSel.card.id, "host-3");
  EXPECT_EQ(zoneSel.zone, PlayerZone::Creatures);
}

// --- Command menu -----------------------------------------------------------

TEST(TableScreen, RightClickOpensTheMenuWithStateDependentLabels) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.myHand, 2, false);
  EXPECT_TRUE(screen.routeEvent(mouseRight(centerOf(cards.at(0)).x, centerOf(cards.at(0)).y)));
  EXPECT_TRUE(screen.menuOpen());
  const std::vector<std::string> items = screen.menuItems();
  ASSERT_EQ(items.size(), 5u);
  EXPECT_EQ(items.at(0), "Tap"); // Bears is untapped
  EXPECT_EQ(items.at(1), "+1/+1 Counter");
  EXPECT_EQ(items.at(2), "Create Token");
  EXPECT_EQ(items.at(3), "Flip to back");
  EXPECT_EQ(items.at(4), "Move...");
}

TEST(TableScreen, RightClickOnOpponentCardNeverOpensTheMenu) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.theirHand, 1, true);
  screen.routeEvent(mouseRight(centerOf(cards.at(0)).x, centerOf(cards.at(0)).y));
  EXPECT_FALSE(screen.menuOpen());
}

TEST(TableScreen, MenuTapQueuesATapAction) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.myHand, 2, false);
  const sf::Vector2f cardCenter = centerOf(cards.at(0));
  screen.routeEvent(mouseRight(cardCenter.x, cardCenter.y));
  ASSERT_TRUE(screen.menuOpen());

  // Click the first menu item (Tap).
  const float itemHeight = screen.menu().itemHeight();
  const sf::Vector2f itemCenter{screen.menu().position().x + (screen.menu().width() / 2.f),
                                screen.menu().position().y + (itemHeight / 2.f)};
  screen.routeEvent(mouseLeft(true, itemCenter.x, itemCenter.y));
  screen.routeEvent(mouseLeft(false, itemCenter.x, itemCenter.y));

  EXPECT_EQ(screen.pollAction(), TableAction::CardCommand);
  EXPECT_EQ(screen.action(), state::tapCard(PlayerSeat::Host, "host-1"));
  EXPECT_FALSE(screen.menuOpen());
}

TEST(TableScreen, MenuMoveTargetsThenClickMovesToAZone) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.myHand, 2, false);
  const sf::Vector2f cardCenter = centerOf(cards.at(0));
  screen.routeEvent(mouseRight(cardCenter.x, cardCenter.y));

  // "Move..." is item 4; click it, then the "Move to Lands" target (item 0).
  const float itemHeight = screen.menu().itemHeight();
  auto clickItem = [&](std::size_t index) {
    const sf::Vector2f center{screen.menu().position().x + (screen.menu().width() / 2.f),
                              screen.menu().position().y +
                                  (itemHeight * static_cast<float>(index)) + (itemHeight / 2.f)};
    screen.routeEvent(mouseLeft(true, center.x, center.y));
    screen.routeEvent(mouseLeft(false, center.x, center.y));
    return screen.pollAction();
  };
  clickItem(4); // opens the move-target list
  EXPECT_EQ(screen.menuItems().at(0), "Move to Lands");
  EXPECT_EQ(clickItem(0), TableAction::CardCommand);
  EXPECT_EQ(screen.action(), state::moveCardToZone(PlayerSeat::Host, "host-1", PlayerZone::Lands));
}

// --- Keyboard verbs ---------------------------------------------------------

TEST(TableScreen, KeyboardVerbsTapAndCounter) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.myHand, 2, false);
  screen.mousePressed(centerOf(cards.at(0)));

  EXPECT_TRUE(screen.keyPressed(key(sf::Keyboard::T)));
  EXPECT_EQ(screen.pollAction(), TableAction::CardCommand);
  EXPECT_EQ(screen.action(), state::tapCard(PlayerSeat::Host, "host-1"));

  screen.mousePressed(centerOf(cards.at(1))); // re-select the other card
  EXPECT_TRUE(screen.keyPressed(key(sf::Keyboard::C)));
  EXPECT_EQ(screen.pollAction(), TableAction::CardCommand);
  EXPECT_EQ(screen.action(), state::addCounter(PlayerSeat::Host, "host-2"));
}

TEST(TableScreen, KeyboardVerbCreatesAToken) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.myHand, 2, false);
  screen.mousePressed(centerOf(cards.at(0)));

  EXPECT_TRUE(screen.keyPressed(key(sf::Keyboard::K)));
  EXPECT_EQ(screen.pollAction(), TableAction::CardCommand);
  EXPECT_EQ(screen.action(), state::createToken(PlayerSeat::Host, PlayerZone::Creatures, "Bears"));
}

TEST(TableScreen, KeyboardVerbFlipsAndStacks) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.myHand, 2, false);
  screen.mousePressed(centerOf(cards.at(0)));
  EXPECT_TRUE(screen.keyPressed(key(sf::Keyboard::F)));
  EXPECT_EQ(screen.pollAction(), TableAction::CardCommand);
  EXPECT_EQ(screen.action(), state::flipCard(PlayerSeat::Host, "host-1"));

  screen.mousePressed(centerOf(cards.at(1)));
  EXPECT_TRUE(screen.keyPressed(key(sf::Keyboard::S)));
  EXPECT_EQ(screen.pollAction(), TableAction::CardCommand);
  EXPECT_EQ(screen.action(), state::moveCardToStack("host-2"));
}

TEST(TableScreen, MoveMenuNumberKeysPickTheTargetZone) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.myHand, 2, false);
  screen.mousePressed(centerOf(cards.at(0)));

  EXPECT_TRUE(screen.keyPressed(key(sf::Keyboard::M)));
  EXPECT_TRUE(screen.menuOpen());
  EXPECT_TRUE(screen.keyPressed(key(sf::Keyboard::Num3))); // Instants / Sorceries
  EXPECT_EQ(screen.pollAction(), TableAction::CardCommand);
  EXPECT_EQ(screen.action(),
            state::moveCardToZone(PlayerSeat::Host, "host-1", PlayerZone::InstantsSorceries));
}

TEST(TableScreen, KeysWithoutASelectionAreNotConsumed) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  // No selection: the verb is not consumed (the App keeps routing keys).
  EXPECT_FALSE(screen.keyPressed(key(sf::Keyboard::T)));
  EXPECT_EQ(screen.pollAction(), TableAction::None);
}

// --- Life editing -----------------------------------------------------------

TEST(TableScreen, ClickingTheLifeRingStartsEditing) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  screen.mousePressed(centerOf(screen.lifeRings().at(state::seatIndex(PlayerSeat::Host))));
  EXPECT_EQ(screen.lifeEditingSeat(), PlayerSeat::Host);
  EXPECT_EQ(screen.lifeEditText(), "20");
}

TEST(TableScreen, TypingLifeAndEnterCommitsSetLife) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  screen.mousePressed(centerOf(screen.lifeRings().at(state::seatIndex(PlayerSeat::Host))));
  screen.keyPressed(key(sf::Keyboard::Num1)); // first digit replaces "20"
  screen.keyPressed(key(sf::Keyboard::Num8)); // "18"
  EXPECT_EQ(screen.lifeEditText(), "18");
  EXPECT_TRUE(screen.keyPressed(key(sf::Keyboard::Enter)));

  EXPECT_EQ(screen.pollAction(), TableAction::CardCommand);
  EXPECT_EQ(screen.action(), state::setLife(PlayerSeat::Host, 18));
  EXPECT_FALSE(screen.lifeEditingSeat().has_value());
}

TEST(TableScreen, EscapeCancelsLifeEditing) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  screen.mousePressed(centerOf(screen.lifeRings().at(state::seatIndex(PlayerSeat::Host))));
  screen.keyPressed(key(sf::Keyboard::Num5));
  screen.keyPressed(key(sf::Keyboard::Escape));
  EXPECT_FALSE(screen.lifeEditingSeat().has_value());
  EXPECT_EQ(screen.pollAction(), TableAction::None);
}

// --- Hand reveal + toolbar --------------------------------------------------

TEST(TableScreen, RevealButtonRequestsTheHand) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const sf::FloatRect bounds = screen.revealButton().bounds();
  screen.routeEvent(mouseLeft(true, centerOf(bounds).x, centerOf(bounds).y));
  screen.routeEvent(mouseLeft(false, centerOf(bounds).x, centerOf(bounds).y));
  EXPECT_EQ(screen.pollAction(), TableAction::RequestReveal);
}

TEST(TableScreen, AcceptRevealCarriesMyHand) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);
  screen.setRevealRequest("player_1");

  const sf::FloatRect bounds = screen.acceptButton().bounds();
  screen.routeEvent(mouseLeft(true, centerOf(bounds).x, centerOf(bounds).y));
  screen.routeEvent(mouseLeft(false, centerOf(bounds).x, centerOf(bounds).y));
  EXPECT_EQ(screen.pollAction(), TableAction::AcceptReveal);

  const std::vector<RevealCard> cards = screen.revealCards();
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards.at(0).id, "host-1");
  EXPECT_EQ(cards.at(0).name, "Bears");
}

TEST(TableScreen, DenyAndDismissReveal) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);
  screen.setRevealRequest("player_1");

  const sf::FloatRect deny = screen.denyButton().bounds();
  screen.routeEvent(mouseLeft(true, centerOf(deny).x, centerOf(deny).y));
  screen.routeEvent(mouseLeft(false, centerOf(deny).x, centerOf(deny).y));
  EXPECT_EQ(screen.pollAction(), TableAction::DenyReveal);

  screen.setRevealRequest("player_1");
  const sf::FloatRect dismiss = screen.dismissButton().bounds();
  screen.routeEvent(mouseLeft(true, centerOf(dismiss).x, centerOf(dismiss).y));
  screen.routeEvent(mouseLeft(false, centerOf(dismiss).x, centerOf(dismiss).y));
  EXPECT_EQ(screen.pollAction(), TableAction::DismissReveal);
}

TEST(TableScreen, CloseRevealedPanelClearsTheReveal) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);
  screen.setRevealResult(true, {RevealCard{"guest-1", "Goblin", ""}});

  const sf::FloatRect bounds = screen.closeRevealedButton().bounds();
  screen.routeEvent(mouseLeft(true, centerOf(bounds).x, centerOf(bounds).y));
  screen.routeEvent(mouseLeft(false, centerOf(bounds).x, centerOf(bounds).y));
  EXPECT_EQ(screen.pollAction(), TableAction::ClearRevealed);
}

TEST(TableScreen, LeaveButtonLeaves) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const sf::FloatRect bounds = screen.leaveButton().bounds();
  screen.routeEvent(mouseLeft(true, centerOf(bounds).x, centerOf(bounds).y));
  screen.routeEvent(mouseLeft(false, centerOf(bounds).x, centerOf(bounds).y));
  EXPECT_EQ(screen.pollAction(), TableAction::Leave);
}

// --- Misc -------------------------------------------------------------------

TEST(TableScreen, EscClosesTheMenuThenClearsSelectionThenFallsThrough) {
  TableScreen screen;
  screen.setBoard(testBoard());
  screen.setRole(PlayerSeat::Host);
  screen.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);

  const Geo geo = geometry();
  const std::vector<sf::FloatRect> cards = handRects(geo.bands.myHand, 2, false);
  screen.mousePressed(centerOf(cards.at(0)));
  screen.routeEvent(mouseRight(centerOf(cards.at(0)).x, centerOf(cards.at(0)).y));
  ASSERT_TRUE(screen.menuOpen());

  // Esc closes the menu (consumed)...
  EXPECT_TRUE(screen.keyPressed(key(sf::Keyboard::Escape)));
  EXPECT_FALSE(screen.menuOpen());
  // ...then clears the selection (consumed)...
  EXPECT_TRUE(screen.keyPressed(key(sf::Keyboard::Escape)));
  EXPECT_FALSE(screen.selection().has_value());
  // ...then nothing is left, so Esc is not consumed (the App may quit).
  EXPECT_FALSE(screen.keyPressed(key(sf::Keyboard::Escape)));
}

} // namespace
} // namespace mtgcpp::core
