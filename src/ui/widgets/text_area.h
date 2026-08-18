// A multi-line ASCII text entry field (M5.2), used to paste Arena deck
// exports into the Deck Editor.
//
// Same structure as TextInput: the text/caret/focus state is plain data and
// fully unit-testable; handleEvent() adapts sf::Event; draw() needs a window.
// It differs from TextInput in the two ways a text area must:
//   * Enter inserts a newline instead of submitting, and
//   * the caret moves over lines (Up/Down walk lines at a fixed column).
//
// Keyboard input arrives as sf::Event::TextEntered (one Unicode codepoint per
// event). We accept printable ASCII (0x20..0x7E) plus '\n' via Enter — that
// covers Arena export text, and avoids the UTF-8 encoding work full Unicode
// would need. Long text scrolls vertically with the mouse wheel (lines beyond
// the visible height are drawn clipped); the caret follows the text.
#pragma once

#include "ui/widgets/widget.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace mtgcpp::core {

class TextArea {
public:
  // --- Geometry -------------------------------------------------------------
  void setPosition(sf::Vector2f position);
  void setSize(sf::Vector2f size);
  sf::Vector2f position() const { return position_; }
  sf::Vector2f size() const { return size_; }
  sf::FloatRect bounds() const;
  bool contains(sf::Vector2f point) const;

  // --- Placeholder ----------------------------------------------------------
  void setPlaceholder(std::string placeholder);
  const std::string &placeholder() const { return placeholder_; }

  // --- Text state (pure, unit-testable) -------------------------------------
  // Replace the whole text; the caret follows to the end.
  void setText(std::string text);
  const std::string &text() const { return text_; }
  bool empty() const { return text_.empty(); }
  // Insert `chunk` at the caret (the caret advances past it).
  void insert(std::string_view chunk);
  // Insert a newline at the caret (the text area's Enter).
  void newline();
  // Remove the character just before the caret (a newline counts as one char).
  void backspace();
  void moveCaretLeft();
  void moveCaretRight();
  void moveCaretUp();
  void moveCaretDown();
  void setCaret(std::size_t index); // clamped to the text length
  std::size_t caret() const { return caret_; }

  // --- Focus ----------------------------------------------------------------
  void setFocused(bool focused);
  bool isFocused() const { return focused_; }

  // --- Vertical scrolling ---------------------------------------------------
  // First visible line (0 = top); used by draw() and adjusted by the wheel.
  void setLineOffset(std::size_t offset); // clamped so content never scrolls away
  std::size_t lineOffset() const { return lineOffset_; }
  // Number of lines the text spans (empty text counts as one line).
  std::size_t lineCount() const;

  // --- Mouse ----------------------------------------------------------------
  // Focus the field when clicked inside; clicking elsewhere unfocuses.
  bool mousePressed(sf::Vector2f point);

  // --- Events + rendering ---------------------------------------------------
  bool handleEvent(const sf::Event &event);
  void draw(sf::RenderTarget &target, const sf::Font &font) const;

private:
  // The byte index of the start of the line containing `index`.
  std::size_t lineStartOf(std::size_t index) const;
  // The byte index just past the end of the line containing `index`.
  std::size_t lineEndOf(std::size_t index) const;

  sf::Vector2f position_{0.f, 0.f};
  sf::Vector2f size_{0.f, 0.f};
  std::string text_;
  std::string placeholder_;
  std::size_t caret_ = 0;
  std::size_t lineOffset_ = 0;
  bool focused_ = false;
};

} // namespace mtgcpp::core
