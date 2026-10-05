// M5.2 TextArea widget tests: multi-line text editing, caret movement over
// lines, focus and wheel scrolling. Pure state + sf::Event adapters, no
// display needed (draw is verified in the running app).

#include "ui/widgets/text_area.h"
#include "ui/widgets/widget.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <string>

#include <SFML/Window/Event.hpp>

namespace mtgcpp::core {
namespace {

sf::Event mouseClick(float x, float y) {
  sf::Event event{};
  event.type = sf::Event::MouseButtonPressed;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

TEST(TextArea, InsertsTextAndNewlines) {
  TextArea area;
  area.insert("4 Forest (WAR) 263");
  area.newline();
  area.insert("2 Bolt (STA) 49");

  EXPECT_EQ(area.text(), "4 Forest (WAR) 263\n2 Bolt (STA) 49");
  EXPECT_EQ(area.caret(), 34u); // caret follows to the end
}

TEST(TextArea, NewlineCountsAsOneCharacter) {
  TextArea area;
  area.setText("ab");
  area.setCaret(1);
  area.newline();
  EXPECT_EQ(area.text(), "a\nb");
  EXPECT_EQ(area.caret(), 2u);
}

TEST(TextArea, BackspaceRemovesTheCharacterBeforeTheCaretIncludingNewlines) {
  TextArea area;
  area.setText("ab\ncd");
  area.setCaret(3); // just past the newline
  area.backspace();
  EXPECT_EQ(area.text(), "abcd");
  EXPECT_EQ(area.caret(), 2u);
}

TEST(TextArea, BackspaceAtTheStartIsANoOp) {
  TextArea area;
  area.setText("Hi");
  area.setCaret(0);
  area.backspace();
  EXPECT_EQ(area.text(), "Hi");
}

TEST(TextArea, CaretMovesLeftAndRightOverNewlines) {
  TextArea area;
  area.setText("ab\ncd");
  area.setCaret(0);
  area.moveCaretRight();
  area.moveCaretRight();
  EXPECT_EQ(area.caret(), 2u); // "ab"
  area.moveCaretRight();
  EXPECT_EQ(area.caret(), 3u); // past the newline
  area.moveCaretRight();
  area.moveCaretRight();
  EXPECT_EQ(area.caret(), 5u); // "cd" end
  area.moveCaretRight();       // clamped
  EXPECT_EQ(area.caret(), 5u);
}

TEST(TextArea, CaretMovesUpAndDownKeepingTheColumn) {
  TextArea area;
  area.setText("aaaa\nbb\ncccccc");
  area.setCaret(2); // on the first line, column 2

  area.moveCaretDown();
  EXPECT_EQ(area.caret(), 7u); // "bb" line start + column 2 (before its newline)
  area.moveCaretDown();
  EXPECT_EQ(area.caret(), 10u); // third line start + column 2
  area.moveCaretDown();         // already on the last line
  EXPECT_EQ(area.caret(), 14u); // end of text

  area.setCaret(10);
  area.moveCaretUp();
  EXPECT_EQ(area.caret(), 7u); // "bb" line, column 2
  area.moveCaretUp();
  EXPECT_EQ(area.caret(), 2u); // back to the first line
  area.moveCaretUp();          // already on the first line
  EXPECT_EQ(area.caret(), 0u);
}

TEST(TextArea, SetTextMovesTheCaretToTheEndAndResetsScroll) {
  TextArea area;
  area.setText("a\nb\nc");
  EXPECT_EQ(area.caret(), 5u);
  area.setLineOffset(2);
  area.setText("x");
  EXPECT_EQ(area.lineOffset(), 0u);
  EXPECT_EQ(area.caret(), 1u);
}

TEST(TextArea, FocusFollowsClicksInsideOnly) {
  TextArea area;
  area.setPosition({0.f, 0.f});
  area.setSize({200.f, 100.f});

  EXPECT_TRUE(area.mousePressed({50.f, 50.f}));
  EXPECT_TRUE(area.isFocused());

  EXPECT_FALSE(area.mousePressed({300.f, 50.f}));
  EXPECT_FALSE(area.isFocused());
}

TEST(TextArea, LineCountCountsNewlines) {
  TextArea area;
  EXPECT_EQ(area.lineCount(), 1u); // empty text is one line
  area.setText("abc");
  EXPECT_EQ(area.lineCount(), 1u);
  area.setText("a\nb");
  EXPECT_EQ(area.lineCount(), 2u);
  area.setText("a\nb\nc");
  EXPECT_EQ(area.lineCount(), 3u);
  area.setText("a\n\nb"); // empty middle line counts too
  EXPECT_EQ(area.lineCount(), 3u);
}

TEST(TextArea, LineOffsetClampsSoContentNeverScrollsAway) {
  TextArea area;
  area.setPosition({0.f, 0.f});
  area.setSize({200.f, 100.f}); // 100 / 18 => 5 visible lines
  area.setText("1\n2\n3\n4\n5\n6\n7\n8");

  area.setLineOffset(99);
  EXPECT_EQ(area.lineOffset(), 3u); // 8 lines - 5 visible
  area.setLineOffset(2);
  EXPECT_EQ(area.lineOffset(), 2u);

  area.setText("short");
  area.setLineOffset(5);
  EXPECT_EQ(area.lineOffset(), 0u); // content fits, no scrolling
}

TEST(TextArea, HandleEventTypesAndEntersIntoAFocusedField) {
  TextArea area;
  area.setPosition({0.f, 0.f});
  area.setSize({200.f, 100.f});
  area.handleEvent(mouseClick(10.f, 10.f)); // focus via a click inside
  EXPECT_TRUE(area.isFocused());

  const auto type = [&area](sf::Uint32 code) {
    sf::Event event{};
    event.type = sf::Event::TextEntered;
    event.text.unicode = code;
    area.handleEvent(event);
  };
  type('D');
  type('e');
  type('c');
  EXPECT_EQ(area.text(), "Dec");

  sf::Event enter{};
  enter.type = sf::Event::KeyPressed;
  enter.key.code = sf::Keyboard::Enter;
  area.handleEvent(enter);
  EXPECT_EQ(area.text(), "Dec\n");

  sf::Event backspace{};
  backspace.type = sf::Event::KeyPressed;
  backspace.key.code = sf::Keyboard::Backspace;
  area.handleEvent(backspace);
  EXPECT_EQ(area.text(), "Dec");
}

TEST(TextArea, UnfocusedFieldIgnoresKeyboard) {
  TextArea area;
  sf::Event text{};
  text.type = sf::Event::TextEntered;
  text.text.unicode = 'A';
  EXPECT_FALSE(area.handleEvent(text));
  EXPECT_TRUE(area.empty());
}

TEST(TextArea, WheelScrollingMovesTheLineOffsetOverTheCursor) {
  TextArea area;
  area.setPosition({0.f, 0.f});
  area.setSize({200.f, 100.f});
  area.setText("1\n2\n3\n4\n5\n6\n7\n8");
  EXPECT_EQ(area.lineOffset(), 0u);

  auto wheel = [&area](float delta, float x, float y) {
    sf::Event event{};
    event.type = sf::Event::MouseWheelScrolled;
    event.mouseWheelScroll.delta = delta;
    event.mouseWheelScroll.x = static_cast<int>(x);
    event.mouseWheelScroll.y = static_cast<int>(y);
    return area.handleEvent(event);
  };
  // Wheel down (delta < 0) over the field scrolls toward later lines.
  EXPECT_TRUE(wheel(-1.f, 50.f, 50.f));
  EXPECT_EQ(area.lineOffset(), 1u);
  EXPECT_TRUE(wheel(-1.f, 50.f, 50.f));
  EXPECT_EQ(area.lineOffset(), 2u);
  // Wheel up (delta > 0) scrolls back, but never before the top.
  EXPECT_TRUE(wheel(1.f, 50.f, 50.f));
  EXPECT_EQ(area.lineOffset(), 1u);
  EXPECT_TRUE(wheel(1.f, 50.f, 50.f));
  EXPECT_EQ(area.lineOffset(), 0u);
  EXPECT_TRUE(wheel(1.f, 50.f, 50.f));
  EXPECT_EQ(area.lineOffset(), 0u);
  // Outside the field the wheel is not consumed.
  EXPECT_FALSE(wheel(-1.f, 300.f, 300.f));
}

TEST(TextArea, CtrlVPastesTheArenaExportKeepingNewlines) {
  // Inject a fake clipboard reader so paste is verifiable without a display.
  struct Restore {
    explicit Restore(ClipboardReader r) : saved(std::move(r)) {}
    ClipboardReader saved;
    ~Restore() { clipboardReader() = saved; }
    Restore(const Restore &) = delete;
    Restore &operator=(const Restore &) = delete;
    Restore(Restore &&) = delete;
    Restore &operator=(Restore &&) = delete;
  } restore{clipboardReader()};
  clipboardReader() = [] { return sf::String("2 Grizzly Bears\n1 Lightning Bolt"); };

  TextArea area;
  area.setPosition({0.f, 0.f});
  area.setSize({200.f, 100.f});
  area.setFocused(true);

  sf::Event paste{};
  paste.type = sf::Event::KeyPressed;
  paste.key.code = sf::Keyboard::V;
  paste.key.control = true;
  EXPECT_TRUE(area.handleEvent(paste));
  EXPECT_EQ(area.text(), "2 Grizzly Bears\n1 Lightning Bolt");
  EXPECT_EQ(area.caret(), area.text().size()); // caret follows to the end
}

TEST(TextArea, CtrlVPastesAtTheCaretNotJustTheEnd) {
  struct Restore {
    explicit Restore(ClipboardReader r) : saved(std::move(r)) {}
    ClipboardReader saved;
    ~Restore() { clipboardReader() = saved; }
    Restore(const Restore &) = delete;
    Restore &operator=(const Restore &) = delete;
    Restore(Restore &&) = delete;
    Restore &operator=(Restore &&) = delete;
  } restore{clipboardReader()};
  clipboardReader() = [] { return sf::String("zz"); };

  TextArea area;
  area.setText("ab");
  area.setCaret(1);

  area.pasteFromClipboard();
  EXPECT_EQ(area.text(), "azzb");
  EXPECT_EQ(area.caret(), 3u);
}

TEST(TextArea, DeleteErasesTheCharacterAtTheCaret) {
  TextArea area;
  area.setText("ab\ncd");
  area.setCaret(3); // on 'c', just past the newline
  area.eraseAtCaret();
  EXPECT_EQ(area.text(), "ab\nd");
  area.setCaret(2); // on the newline itself
  area.eraseAtCaret();
  EXPECT_EQ(area.text(), "abd");
}

TEST(TextArea, HomeAndEndJumpToTheLineEdges) {
  TextArea area;
  area.setText("abc\ndef");
  area.setCaret(5); // on the second line, after 'd'
  area.moveCaretToLineStart();
  EXPECT_EQ(area.caret(), 4u); // start of the second line
  area.moveCaretToLineEnd();
  EXPECT_EQ(area.caret(), 7u); // end of the second line
}

TEST(TextArea, CaretMovementScrollsTheViewToKeepItVisible) {
  TextArea area;
  area.setPosition({0.f, 0.f});
  area.setSize({200.f, 100.f}); // 100 / 18 => 5 visible lines
  area.setText("1\n2\n3\n4\n5\n6\n7\n8");
  EXPECT_EQ(area.lineOffset(), 0u);

  // Walk the caret to the last line: the view scrolls so it stays visible.
  area.setCaret(0);
  for (int i = 0; i < 7; ++i) {
    area.moveCaretDown();
  }
  EXPECT_GE(area.lineOffset(), 3u); // 8 lines - 5 visible

  // Walk back to the first line: the view scrolls back to the top.
  area.moveCaretToLineStart();
  for (int i = 0; i < 7; ++i) {
    area.moveCaretUp();
  }
  EXPECT_EQ(area.lineOffset(), 0u);
}

} // namespace
} // namespace mtgcpp::core
