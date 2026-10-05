// TextArea implementation (M5.2): multi-line text/caret/focus + rendering.

#include "ui/widgets/text_area.h"

#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace mtgcpp::core {

namespace {
constexpr float kFontSize = 14.f;
constexpr float kLineHeight = 18.f; // vertical pitch, slightly taller than the glyphs
constexpr float kPadX = 6.f;        // horizontal inset for the text
} // namespace

void TextArea::setPosition(sf::Vector2f position) { position_ = position; }
void TextArea::setSize(sf::Vector2f size) { size_ = size; }
sf::FloatRect TextArea::bounds() const { return {position_, size_}; }
bool TextArea::contains(sf::Vector2f point) const { return pointInside(position_, size_, point); }

void TextArea::setPlaceholder(std::string placeholder) { placeholder_ = std::move(placeholder); }

void TextArea::setText(std::string text) {
  text_ = std::move(text);
  caret_ = text_.size();
  lineOffset_ = 0;
}

void TextArea::insert(std::string_view chunk) {
  text_.insert(caret_, chunk.data(), chunk.size());
  caret_ += chunk.size();
}

void TextArea::newline() {
  text_.insert(caret_, 1, '\n');
  caret_ += 1;
}

void TextArea::backspace() {
  if (caret_ == 0) {
    return;
  }
  text_.erase(caret_ - 1, 1);
  caret_ -= 1;
}

void TextArea::eraseAtCaret() {
  if (caret_ < text_.size()) {
    text_.erase(caret_, 1);
  }
}

void TextArea::pasteFromClipboard() {
  // The OS clipboard is a UTF-32 sf::String; this area is ASCII-only, so drop
  // every codepoint outside printable ASCII but KEEP newlines so a pasted
  // Arena export arrives line-by-line like the docs promise.
  const sf::String clip = clipboardReader()();
  std::string ascii;
  ascii.reserve(clip.getSize());
  for (const sf::Uint32 code : clip) {
    if (code == '\n') {
      ascii.push_back('\n');
    } else if (code >= 0x20 && code <= 0x7e) {
      ascii.push_back(static_cast<char>(code));
    }
  }
  if (!ascii.empty()) {
    insert(ascii);
  }
}

void TextArea::moveCaretLeft() {
  if (caret_ > 0) {
    caret_ -= 1;
  }
}

void TextArea::moveCaretRight() {
  if (caret_ < text_.size()) {
    caret_ += 1;
  }
}

std::size_t TextArea::lineStartOf(std::size_t index) const {
  // The newline before `index`, or 0 when there is none; +1 steps past it.
  if (index == 0) {
    return 0;
  }
  const std::size_t previous = text_.rfind('\n', index - 1);
  return previous == std::string::npos ? 0 : previous + 1;
}

std::size_t TextArea::lineEndOf(std::size_t index) const {
  const std::size_t next = text_.find('\n', index);
  return next == std::string::npos ? text_.size() : next;
}

void TextArea::moveCaretUp() {
  const std::size_t lineStart = lineStartOf(caret_);
  if (lineStart == 0) {
    caret_ = 0; // already on the first line
    return;
  }
  const std::size_t column = caret_ - lineStart;
  const std::size_t previousStart = lineStartOf(lineStart - 1);
  const std::size_t previousEnd = lineEndOf(previousStart);
  caret_ = std::min(previousStart + column, previousEnd);
  ensureCaretVisible();
}

void TextArea::moveCaretDown() {
  const std::size_t lineEnd = lineEndOf(caret_);
  const std::size_t column = caret_ - lineStartOf(caret_);
  if (lineEnd == text_.size()) {
    caret_ = text_.size(); // already on the last line
    return;
  }
  const std::size_t nextStart = lineEnd + 1;
  const std::size_t nextEnd = lineEndOf(nextStart);
  caret_ = std::min(nextStart + column, nextEnd);
  ensureCaretVisible();
}

void TextArea::moveCaretToLineStart() { caret_ = lineStartOf(caret_); }

void TextArea::moveCaretToLineEnd() { caret_ = lineEndOf(caret_); }

void TextArea::setCaret(std::size_t index) { caret_ = std::min(index, text_.size()); }

std::size_t TextArea::caretLine() const {
  // The caret's 1-based line = 1 + the newlines before the caret.
  const auto caretOffset = static_cast<std::string::difference_type>(caret_);
  return static_cast<std::size_t>(1 + std::count(text_.begin(), text_.begin() + caretOffset, '\n'));
}

void TextArea::ensureCaretVisible() {
  // `visible` is 1 at minimum so a tall offset cannot scroll past the content.
  const std::size_t visible =
      std::max<std::size_t>(1, static_cast<std::size_t>(size_.y / kLineHeight));
  const std::size_t line = caretLine();
  if (line < lineOffset_ + 1) {
    lineOffset_ = line - 1; // scrolled past the caret: jump it into view
  } else if (line > lineOffset_ + visible) {
    lineOffset_ = line - visible; // caret below the fold: reveal it
  }
}

void TextArea::setFocused(bool focused) { focused_ = focused; }

void TextArea::setLineOffset(std::size_t offset) {
  const std::size_t lines = lineCount();
  // `visible` is 1 at minimum so a tall offset cannot scroll past the content.
  const std::size_t visible =
      std::max<std::size_t>(1, static_cast<std::size_t>(size_.y / kLineHeight));
  lineOffset_ = lines <= visible ? 0 : std::min(offset, lines - visible);
}

std::size_t TextArea::lineCount() const {
  if (text_.empty()) {
    return 1; // an empty area still shows its single placeholder line
  }
  std::size_t lines = 1;
  for (const char c : text_) {
    if (c == '\n') {
      lines += 1;
    }
  }
  return lines;
}

bool TextArea::mousePressed(sf::Vector2f point) {
  focused_ = contains(point); // click inside focuses, click outside unfocuses
  return focused_;
}

bool TextArea::handleEvent(const sf::Event &event) {
  if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    return mousePressed(
        {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
  }

  // The wheel scrolls the text even while unfocused, as long as the cursor is
  // over the field (a passive read of the cursor position).
  if (event.type == sf::Event::MouseWheelScrolled) {
    if (contains({static_cast<float>(event.mouseWheelScroll.x),
                  static_cast<float>(event.mouseWheelScroll.y)})) {
      const int delta = event.mouseWheelScroll.delta > 0.f ? -1 : 1; // wheel up = earlier lines
      std::size_t next = lineOffset_;
      if (delta < 0) {
        if (next > 0) {
          next -= 1;
        }
      } else {
        next += 1;
      }
      setLineOffset(next);
      return true;
    }
    return false;
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
    case sf::Keyboard::Delete:
      eraseAtCaret();
      break;
    case sf::Keyboard::Enter:
      newline(); // Enter inserts a line in a text area (never submits)
      break;
    case sf::Keyboard::Left:
      moveCaretLeft();
      break;
    case sf::Keyboard::Right:
      moveCaretRight();
      break;
    case sf::Keyboard::Up:
      moveCaretUp();
      break;
    case sf::Keyboard::Down:
      moveCaretDown();
      break;
    case sf::Keyboard::Home:
      moveCaretToLineStart();
      break;
    case sf::Keyboard::End:
      moveCaretToLineEnd();
      break;
    case sf::Keyboard::V:
      if (event.key.control) {
        pasteFromClipboard();
      }
      break;
    default:
      break; // every other key is swallowed so screens do not react to typing
    }
    return true; // see above: a focused field owns the keyboard
  }

  return false;
}

void TextArea::draw(sf::RenderTarget &target, const sf::Font &font) const {
  // Panel: parchment like a physical card; the outline is gold while focused
  // so the user can see where the keyboard is going.
  sf::RectangleShape panel(size_);
  panel.setPosition(position_);
  panel.setFillColor(menuPalette().parchment);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(focused_ ? menuPalette().gold : menuPalette().muted);
  target.draw(panel);

  // Split the text into lines and render the visible window (lineOffset_..).
  // ASCII only: sf::Text decodes const char* through the C locale.
  const std::size_t visible =
      std::max<std::size_t>(1, static_cast<std::size_t>(size_.y / kLineHeight));
  const std::size_t lines = lineCount();
  const unsigned fontSize = static_cast<unsigned>(kFontSize);
  if (text_.empty()) {
    // Placeholder renders muted; it is never the real content.
    sf::Text hint(placeholder_, font, fontSize);
    hint.setFillColor(menuPalette().muted);
    hint.setPosition({position_.x + kPadX, position_.y + 4.f});
    target.draw(hint);
  } else {
    std::size_t lineStart = 0;
    for (std::size_t line = 0; line < lines; ++line) {
      const std::size_t lineEnd = lineEndOf(lineStart);
      if (line >= lineOffset_ && line < lineOffset_ + visible) {
        sf::Text row(text_.substr(lineStart, lineEnd - lineStart), font, fontSize);
        row.setFillColor(menuPalette().background);
        row.setPosition(
            {position_.x + kPadX,
             position_.y + (kLineHeight * static_cast<float>(line - lineOffset_)) + 2.f});
        target.draw(row);
      }
      if (lineEnd == text_.size()) {
        break;
      }
      lineStart = lineEnd + 1;
    }
  }

  if (focused_) {
    // Caret: a thin gold bar at the current line, placed after the text that
    // precedes the caret on that line.
    const std::size_t lineStart = lineStartOf(caret_);
    const auto caretOffset = static_cast<std::string::difference_type>(caret_);
    const std::size_t caretLine =
        static_cast<std::size_t>(1 + std::count(text_.begin(), text_.begin() + caretOffset, '\n'));
    const std::string prefix = text_.substr(lineStart, caret_ - lineStart);
    sf::Text prefixText(prefix, font, fontSize);
    const float caretX = prefixText.getLocalBounds().width;
    const float caretY =
        position_.y + (kLineHeight * static_cast<float>(caretLine - 1 - lineOffset_)) + 2.f;
    sf::RectangleShape caret(sf::Vector2f(1.f, static_cast<float>(fontSize) + 4.f));
    caret.setPosition({position_.x + kPadX + caretX, caretY});
    caret.setFillColor(menuPalette().gold);
    target.draw(caret);
  }
}

} // namespace mtgcpp::core
