// A single-line text entry field (M4.2).
//
// Same structure as Button: the text/caret/focus state is plain data and fully
// unit-testable; handleEvent() adapts sf::Event; draw() needs a window.
//
// Keyboard input arrives as sf::Event::TextEntered, which carries ONE Unicode
// codepoint per event. We accept printable ASCII (0x20..0x7E) — that covers
// deck names, and avoids the UTF-8 encoding work full Unicode would need.
// Clicking the field focuses it and moves the caret to the end; pixel-accurate
// click-to-place-caret is left as a later refinement.
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

class TextInput {
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
  // Remove the character just before the caret.
  void backspace();
  void moveCaretLeft();
  void moveCaretRight();
  void setCaret(std::size_t index); // clamped to the text length
  std::size_t caret() const { return caret_; }

  // --- Focus ----------------------------------------------------------------
  void setFocused(bool focused);
  bool isFocused() const { return focused_; }

  // --- Submit ---------------------------------------------------------------
  // Pressing Enter/Return while focused queues a submit; the screen polls this
  // each frame (the same pattern Button uses for clicks).
  bool consumeSubmitted();

  // --- Mouse ----------------------------------------------------------------
  // Focus the field when clicked inside; clicking elsewhere unfocuses.
  bool mousePressed(sf::Vector2f point);

  // --- Events + rendering ---------------------------------------------------
  bool handleEvent(const sf::Event &event);
  void draw(sf::RenderTarget &target, const sf::Font &font) const;

private:
  sf::Vector2f position_{0.f, 0.f};
  sf::Vector2f size_{0.f, 0.f};
  std::string text_;
  std::string placeholder_;
  std::size_t caret_ = 0;
  bool focused_ = false;
  bool submitQueued_ = false;
};

} // namespace mtgcpp::core
