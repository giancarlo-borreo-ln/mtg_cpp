// M4.2 menu widget tests: pure geometry, hit-testing, selection, scrolling and
// text-editing logic for Button / TextInput / ListView.
//
// Rendering (draw) needs a window, so it is NOT exercised here — the "smoke"
// is that every widget constructs and mutates without a display, plus a few
// sf::Event adapter tests. The pixels are verified in the running app.

#include "ui/widgets/button.h"
#include "ui/widgets/list_view.h"
#include "ui/widgets/text_input.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <SFML/Graphics.hpp>

namespace mtgcpp::core {
namespace {

// A synthetic left-button click at (x, y), built directly as an sf::Event.
sf::Event mouseClick(float x, float y) {
  sf::Event event;
  event.type = sf::Event::MouseButtonPressed;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

TEST(Button, ContainsOnlyInsideItsRect) {
  Button button;
  button.setPosition({10.f, 20.f});
  button.setSize({100.f, 30.f});

  EXPECT_FALSE(button.contains({9.f, 20.f}));
  EXPECT_TRUE(button.contains({10.f, 20.f}));
  EXPECT_TRUE(button.contains({109.f, 49.f}));
  EXPECT_FALSE(button.contains({110.f, 50.f})); // bottom-right edge is exclusive
  EXPECT_FALSE(button.contains({50.f, 5.f}));
}

TEST(Button, HoverFollowsTheMouse) {
  Button button;
  button.setPosition({0.f, 0.f});
  button.setSize({50.f, 20.f});

  button.setMousePosition({25.f, 10.f});
  EXPECT_TRUE(button.isHovered());

  button.setMousePosition({60.f, 10.f});
  EXPECT_FALSE(button.isHovered());
}

TEST(Button, ClickFiresOnPressThenReleaseInside) {
  Button button;
  button.setPosition({0.f, 0.f});
  button.setSize({50.f, 20.f});

  EXPECT_TRUE(button.mouseButtonPressed({25.f, 10.f}));  // consumed: press inside
  EXPECT_FALSE(button.consumeClicked());                 // not yet — needs a release
  EXPECT_TRUE(button.mouseButtonReleased({25.f, 10.f})); // consumed: our release
  EXPECT_TRUE(button.consumeClicked());                  // click delivered once
  EXPECT_FALSE(button.consumeClicked());                 // and only once
}

TEST(Button, ReleaseOutsideNeverClicks) {
  Button button;
  button.setPosition({0.f, 0.f});
  button.setSize({50.f, 20.f});

  button.mouseButtonPressed({25.f, 10.f});
  button.setMousePosition({80.f, 10.f}); // leave while held -> press cancelled
  EXPECT_FALSE(button.isPressed());
  button.mouseButtonReleased({80.f, 10.f});
  EXPECT_FALSE(button.consumeClicked());
}

TEST(Button, PressOutsideIsNotConsumed) {
  Button button;
  button.setPosition({0.f, 0.f});
  button.setSize({50.f, 20.f});

  EXPECT_FALSE(button.mouseButtonPressed({100.f, 100.f}));
}

TEST(Button, DisabledButtonIgnoresInput) {
  Button button;
  button.setPosition({0.f, 0.f});
  button.setSize({50.f, 20.f});
  button.setEnabled(false);

  EXPECT_FALSE(button.mouseButtonPressed({25.f, 10.f}));
  button.setMousePosition({25.f, 10.f});
  EXPECT_FALSE(button.isHovered());
  EXPECT_FALSE(button.consumeClicked());
}

TEST(Button, StoresItsLabel) {
  Button button;
  button.setLabel("Add");
  EXPECT_EQ(button.label(), "Add");
}

TEST(Button, StoresAnOptionalSubLabel) {
  Button button;
  EXPECT_TRUE(button.subLabel().empty());
  button.setSubLabel("Play as Carlo");
  EXPECT_EQ(button.subLabel(), "Play as Carlo");
  button.setSubLabel("");
  EXPECT_TRUE(button.subLabel().empty());
}

TEST(Button, HandleEventTranslatesMouseClicks) {
  Button button;
  button.setPosition({0.f, 0.f});
  button.setSize({50.f, 20.f});

  EXPECT_TRUE(button.handleEvent(mouseClick(25.f, 10.f)));

  sf::Event release;
  release.type = sf::Event::MouseButtonReleased;
  release.mouseButton.button = sf::Mouse::Left;
  release.mouseButton.x = 25;
  release.mouseButton.y = 10;
  EXPECT_TRUE(button.handleEvent(release));
  EXPECT_TRUE(button.consumeClicked());
}

// ---------------------------------------------------------------------------
// TextInput
// ---------------------------------------------------------------------------

TEST(TextInput, InsertsAtTheCaret) {
  TextInput input;
  input.setText("Hello");
  input.setCaret(2);
  input.insert("X");

  EXPECT_EQ(input.text(), "HeXllo");
  EXPECT_EQ(input.caret(), 3u);
}

TEST(TextInput, BuildsTextFromScratchByInserting) {
  TextInput input;
  input.insert("For");
  input.insert("est");

  EXPECT_EQ(input.text(), "Forest");
  EXPECT_EQ(input.caret(), 6u);
}

TEST(TextInput, BackspaceRemovesTheCharacterBeforeTheCaret) {
  TextInput input;
  input.setText("Hello");
  input.setCaret(4);
  input.backspace();

  EXPECT_EQ(input.text(), "Helo");
  EXPECT_EQ(input.caret(), 3u);
}

TEST(TextInput, BackspaceAtTheStartIsANoOp) {
  TextInput input;
  input.setText("Hi");
  input.setCaret(0);
  input.backspace();

  EXPECT_EQ(input.text(), "Hi");
  EXPECT_EQ(input.caret(), 0u);
}

TEST(TextInput, CaretMovesAndClampsToTheText) {
  TextInput input;
  input.setText("abc");

  input.moveCaretLeft();
  EXPECT_EQ(input.caret(), 2u);
  input.moveCaretLeft();
  input.moveCaretLeft();
  input.moveCaretLeft(); // clamped at 0
  EXPECT_EQ(input.caret(), 0u);

  input.moveCaretRight();
  input.moveCaretRight();
  input.moveCaretRight();
  EXPECT_EQ(input.caret(), 3u);
  input.moveCaretRight(); // clamped at size
  EXPECT_EQ(input.caret(), 3u);
}

TEST(TextInput, SetTextMovesTheCaretToTheEnd) {
  TextInput input;
  input.setText("deck");
  EXPECT_EQ(input.caret(), 4u);
}

TEST(TextInput, SetCaretClampsToTheTextLength) {
  TextInput input;
  input.setText("ab");
  input.setCaret(99);
  EXPECT_EQ(input.caret(), 2u);
}

TEST(TextInput, FocusFollowsClicksInsideOnly) {
  TextInput input;
  input.setPosition({0.f, 0.f});
  input.setSize({100.f, 24.f});

  EXPECT_TRUE(input.mousePressed({50.f, 12.f}));
  EXPECT_TRUE(input.isFocused());

  EXPECT_FALSE(input.mousePressed({200.f, 12.f}));
  EXPECT_FALSE(input.isFocused());
}

TEST(TextInput, HandleEventTypesIntoAFocusedField) {
  TextInput input;
  input.setPosition({0.f, 0.f});
  input.setSize({100.f, 24.f});

  input.handleEvent(mouseClick(10.f, 10.f)); // focus via a click inside
  EXPECT_TRUE(input.isFocused());

  const auto type = [&input](sf::Uint32 code) {
    sf::Event event;
    event.type = sf::Event::TextEntered;
    event.text.unicode = code;
    input.handleEvent(event);
  };
  type('F');
  type('o');
  type('r');
  EXPECT_EQ(input.text(), "For");

  sf::Event backspace;
  backspace.type = sf::Event::KeyPressed;
  backspace.key.code = sf::Keyboard::Backspace;
  input.handleEvent(backspace);
  EXPECT_EQ(input.text(), "Fo");
}

TEST(TextInput, UnfocusedFieldIgnoresKeyboard) {
  TextInput input;
  sf::Event text;
  text.type = sf::Event::TextEntered;
  text.text.unicode = 'A';
  EXPECT_FALSE(input.handleEvent(text));
  EXPECT_TRUE(input.empty());
}

TEST(TextInput, FocusedFieldSwallowsUnrelatedKeys) {
  TextInput input;
  input.setFocused(true);

  sf::Event key;
  key.type = sf::Event::KeyPressed;
  key.key.code = sf::Keyboard::Num1;
  EXPECT_TRUE(input.handleEvent(key)); // consumed so the screen router ignores it
}

TEST(TextInput, EnterWhileFocusedQueuesASubmit) {
  TextInput input;
  input.setFocused(true);

  sf::Event enter;
  enter.type = sf::Event::KeyPressed;
  enter.key.code = sf::Keyboard::Enter;
  EXPECT_TRUE(input.handleEvent(enter));
  EXPECT_TRUE(input.consumeSubmitted());
  EXPECT_FALSE(input.consumeSubmitted()); // exactly once
}

TEST(TextInput, EnterWhileUnfocusedQueuesNothing) {
  TextInput input;

  sf::Event enter;
  enter.type = sf::Event::KeyPressed;
  enter.key.code = sf::Keyboard::Enter;
  EXPECT_FALSE(input.handleEvent(enter));
  EXPECT_FALSE(input.consumeSubmitted());
}

// ---------------------------------------------------------------------------
// ListView
// ---------------------------------------------------------------------------

TEST(ListView, RowAtMapsPointsToRows) {
  ListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({200.f, 72.f});
  list.setRowHeight(24.f);
  list.setItems({"A", "B", "C", "D"});

  EXPECT_EQ(list.rowAt({5.f, 2.f}), std::make_optional<std::size_t>(0));
  EXPECT_EQ(list.rowAt({5.f, 24.f}), std::make_optional<std::size_t>(1));
  EXPECT_EQ(list.rowAt({5.f, 48.f}), std::make_optional<std::size_t>(2));
  EXPECT_EQ(list.rowAt({5.f, 71.f}), std::make_optional<std::size_t>(2));
  EXPECT_FALSE(list.rowAt({5.f, 73.f}).has_value()); // outside the widget
  EXPECT_FALSE(list.rowAt({300.f, 10.f}).has_value());
}

TEST(ListView, RowAtAccountsForTheScrollOffset) {
  ListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({200.f, 72.f});
  list.setRowHeight(24.f);
  list.setItems({"A", "B", "C", "D"});
  list.setScrollOffset(2);

  EXPECT_EQ(list.rowAt({5.f, 2.f}), std::make_optional<std::size_t>(2)); // C
  EXPECT_FALSE(list.rowAt({5.f, 71.f}).has_value()); // row 2 -> index 4, past items
}

TEST(ListView, MouseClickSelectsTheClickedRow) {
  ListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({200.f, 72.f});
  list.setRowHeight(24.f);
  list.setItems({"A", "B", "C"});

  EXPECT_TRUE(list.mousePressed({5.f, 25.f}));
  EXPECT_EQ(list.selectedIndex(), std::make_optional<std::size_t>(1));

  EXPECT_FALSE(list.mousePressed({300.f, 10.f})); // outside: unchanged
  EXPECT_EQ(list.selectedIndex(), std::make_optional<std::size_t>(1));
}

TEST(ListView, SelectionClampsToTheItems) {
  ListView list;
  list.setItems({"A", "B"});

  list.setSelected(1);
  EXPECT_EQ(list.selectedIndex(), std::make_optional<std::size_t>(1));

  list.setSelected(5);
  EXPECT_FALSE(list.selectedIndex().has_value());
}

TEST(ListView, VisibleRowCountFollowsSizeAndRowHeight) {
  ListView list;
  list.setSize({200.f, 72.f});
  list.setRowHeight(24.f);

  EXPECT_EQ(list.visibleRowCount(), 3u);
}

TEST(ListView, ScrollOffsetClampsToTheItemCount) {
  ListView list;
  list.setItems({"A", "B"});

  list.setScrollOffset(99);
  EXPECT_EQ(list.scrollOffset(), 2u);
}

TEST(ListView, AddItemAppendsAndClearRemoves) {
  ListView list;
  list.addItem("X");
  list.addItem("Y");

  ASSERT_EQ(list.items().size(), 2u);
  EXPECT_EQ(list.items().at(0), "X");

  list.clearItems();
  EXPECT_TRUE(list.empty());
}

TEST(ListView, AddItemAutoScrollsSoTheNewRowStaysVisible) {
  ListView list;
  list.setPosition({0.f, 0.f});
  list.setSize({200.f, 78.f});
  list.setRowHeight(26.f); // exactly 3 rows fit
  list.setItems({"A", "B", "C"});
  EXPECT_EQ(list.scrollOffset(), 0u);

  list.addItem("D");
  ASSERT_EQ(list.items().size(), 4u);
  // One row no longer fits, so the list scrolls to reveal the new bottom row.
  EXPECT_EQ(list.scrollOffset(), 1u);
  EXPECT_EQ(list.rowAt({5.f, 2.f}), std::make_optional<std::size_t>(1));  // B
  EXPECT_EQ(list.rowAt({5.f, 60.f}), std::make_optional<std::size_t>(3)); // D
}

} // namespace
} // namespace mtgcpp::core
