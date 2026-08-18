// TextInput implementation (M4.2): text/caret/focus state + rendering.

#include "ui/widgets/text_input.h"

#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <utility>

namespace mtgcpp::core {

void TextInput::setPosition(sf::Vector2f position) { position_ = position; }
void TextInput::setSize(sf::Vector2f size) { size_ = size; }
sf::FloatRect TextInput::bounds() const { return {position_, size_}; }
bool TextInput::contains(sf::Vector2f point) const { return pointInside(position_, size_, point); }

void TextInput::setPlaceholder(std::string placeholder) { placeholder_ = std::move(placeholder); }

void TextInput::setText(std::string text) {
  text_ = std::move(text);
  caret_ = text_.size(); // replacing the text wholesale puts the caret at the end
}

void TextInput::insert(std::string_view chunk) {
  // The (position, pointer, count) overload inserts into the middle of the
  // string at the caret; std::string::insert(pos, string_view) does not exist.
  text_.insert(caret_, chunk.data(), chunk.size());
  caret_ += chunk.size();
}

void TextInput::backspace() {
  if (caret_ == 0) {
    return;
  }
  text_.erase(caret_ - 1, 1);
  caret_ -= 1;
}

void TextInput::moveCaretLeft() {
  if (caret_ > 0) {
    caret_ -= 1;
  }
}

void TextInput::moveCaretRight() {
  if (caret_ < text_.size()) {
    caret_ += 1;
  }
}

void TextInput::setCaret(std::size_t index) { caret_ = std::min(index, text_.size()); }

void TextInput::setFocused(bool focused) { focused_ = focused; }

bool TextInput::consumeSubmitted() {
  if (submitQueued_) {
    submitQueued_ = false;
    return true;
  }
  return false;
}

bool TextInput::mousePressed(sf::Vector2f point) {
  focused_ = contains(point); // click inside focuses, click outside unfocuses
  return focused_;
}

bool TextInput::handleEvent(const sf::Event &event) {
  if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    return mousePressed(
        {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
  }

  // Keyboard and text events only mean something while the field is focused.
  if (!focused_) {
    return false;
  }

  if (event.type == sf::Event::TextEntered) {
    const sf::Uint32 code = event.text.unicode;
    if (code >= 0x20 && code <= 0x7e) { // printable ASCII only
      const char ascii = static_cast<char>(code);
      insert(std::string_view(&ascii, 1));
    }
    return true; // a focused field owns the keyboard
  }

  if (event.type == sf::Event::KeyPressed) {
    switch (event.key.code) {
    case sf::Keyboard::Backspace:
      backspace();
      break;
    case sf::Keyboard::Left:
      moveCaretLeft();
      break;
    case sf::Keyboard::Right:
      moveCaretRight();
      break;
    case sf::Keyboard::Enter:
      submitQueued_ = true;
      break;
    default:
      break; // every other key is swallowed so screens do not react to typing
    }
    return true; // see above: a focused field owns the keyboard
  }

  return false;
}

void TextInput::draw(sf::RenderTarget &target, const sf::Font &font) const {
  // Panel: parchment like a physical card; the outline is gold while focused
  // so the user can see where the keyboard is going.
  sf::RectangleShape panel(size_);
  panel.setPosition(position_);
  panel.setFillColor(menuPalette().parchment);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(focused_ ? menuPalette().gold : menuPalette().muted);
  target.draw(panel);

  // The placeholder renders in the muted color only while the field is empty;
  // real text renders dark on the parchment panel.
  const std::string shown = text_.empty() ? placeholder_ : text_;
  sf::Text label(shown, font, 16u);
  label.setFillColor(text_.empty() ? menuPalette().muted : menuPalette().background);
  const sf::FloatRect textBounds = label.getLocalBounds();
  label.setPosition(
      {position_.x + 6.f, position_.y + ((size_.y - textBounds.height) / 2.f) - textBounds.top});
  target.draw(label);

  if (focused_) {
    // Caret: a thin gold bar at the text position of the caret. findCharacterPos
    // returns the glyph position inside `label`, so we add the label's origin.
    const sf::Vector2f caretPosition = label.findCharacterPos(caret_);
    sf::RectangleShape caret(sf::Vector2f(1.f, static_cast<float>(label.getCharacterSize()) + 4.f));
    caret.setPosition({label.getPosition().x + caretPosition.x, position_.y + 4.f});
    caret.setFillColor(menuPalette().gold);
    target.draw(caret);
  }
}

} // namespace mtgcpp::core
