// DeckListView implementation (M4.3): row/delete hit-testing + rendering.

#include "ui/widgets/deck_list_view.h"

#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace mtgcpp::core {

namespace {
// Row interior metrics, authored in design pixels and scaled by scale_.
constexpr float kThumbWidth = 44.f; // thumbnail swatch, ~3:4 card aspect
constexpr float kThumbHeight = 56.f;
constexpr float kDeleteWidth = 84.f; // per-row Delete button
constexpr float kDeleteHeight = 30.f;
constexpr float kRowPad = 10.f;  // left/right gutter inside a row
constexpr float kTextGap = 68.f; // thumb width + gutter (name column starts here)
} // namespace

void DeckListView::setPosition(sf::Vector2f position) { position_ = position; }
void DeckListView::setSize(sf::Vector2f size) { size_ = size; }
sf::FloatRect DeckListView::bounds() const { return {position_, size_}; }
bool DeckListView::contains(sf::Vector2f point) const {
  return pointInside(position_, size_, point);
}

void DeckListView::setScale(float scale) { scale_ = std::max(scale, 1.f); }

void DeckListView::setRowHeight(float height) { rowHeight_ = std::max(height, 1.f); }

void DeckListView::setDecks(std::vector<DeckSummary> decks) {
  decks_ = std::move(decks);
  scrollOffset_ = 0;
  selected_.reset();
  deleteQueued_.reset();
}

sf::FloatRect DeckListView::rowRect(std::size_t row) const {
  const float y = position_.y + (static_cast<float>(row) * rowHeight_);
  return {position_.x, y, size_.x, rowHeight_};
}

std::optional<std::size_t> DeckListView::rowAt(sf::Vector2f point) const {
  if (!contains(point)) {
    return std::nullopt;
  }
  // Which visible row the y-coordinate falls in (integer division truncates).
  const std::size_t visible = static_cast<std::size_t>((point.y - position_.y) / rowHeight_);
  const std::size_t index = scrollOffset_ + visible;
  if (index >= decks_.size()) {
    return std::nullopt; // past the last deck (empty tail of the widget)
  }
  return index;
}

sf::FloatRect DeckListView::deleteButtonRect(const sf::FloatRect &row) const {
  const float width = kDeleteWidth * scale_;
  const float height = kDeleteHeight * scale_;
  const float y = row.top + ((row.height - height) / 2.f);
  const float x = row.left + row.width - width - (kRowPad * scale_);
  return {x, y, width, height};
}

std::optional<std::size_t> DeckListView::deleteAt(sf::Vector2f point) const {
  const std::optional<std::size_t> row = rowAt(point);
  if (!row.has_value()) {
    return std::nullopt;
  }
  const std::size_t visible = row.value() - scrollOffset_;
  const sf::FloatRect button = deleteButtonRect(rowRect(visible));
  if (pointInside({button.left, button.top}, {button.width, button.height}, point)) {
    return row;
  }
  return std::nullopt;
}

std::size_t DeckListView::visibleRowCount() const {
  return static_cast<std::size_t>(size_.y / rowHeight_);
}

void DeckListView::setScrollOffset(std::size_t offset) {
  scrollOffset_ = std::min(offset, decks_.size());
}

void DeckListView::setSelected(std::optional<std::size_t> index) {
  selected_.reset();
  if (index.has_value() && index.value() < decks_.size()) {
    selected_ = index;
  }
}

bool DeckListView::mousePressed(sf::Vector2f point) {
  // Delete wins over selection: the button is a discrete control inside the
  // row, and a click on it must never also select the deck.
  const std::optional<std::size_t> del = deleteAt(point);
  if (del.has_value()) {
    deleteQueued_ = del;
    return true;
  }
  const std::optional<std::size_t> row = rowAt(point);
  if (row.has_value()) {
    setSelected(row);
    return true;
  }
  return false;
}

std::optional<std::size_t> DeckListView::consumeDelete() {
  if (deleteQueued_.has_value()) {
    const std::optional<std::size_t> value = deleteQueued_;
    deleteQueued_.reset();
    return value;
  }
  return std::nullopt;
}

bool DeckListView::handleEvent(const sf::Event &event) {
  if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    return mousePressed(
        {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
  }
  return false;
}

void DeckListView::draw(sf::RenderTarget &target, const sf::Font &font,
                        const sf::Font &boldFont) const {
  // The list frame: the window's background color so the rows read as cards
  // sitting in a dark slot inside its parent panel, framed by a gold border.
  sf::RectangleShape panel(size_);
  panel.setPosition(position_);
  panel.setFillColor(menuPalette().background);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(menuPalette().gold);
  target.draw(panel);

  const std::size_t visible = std::min(visibleRowCount(), decks_.size());
  const unsigned nameSize = static_cast<unsigned>(16.f * scale_);
  const unsigned metaSize = static_cast<unsigned>(13.f * scale_);
  const unsigned deleteSize = static_cast<unsigned>(13.f * scale_);
  for (std::size_t row = 0; row < visible; ++row) {
    const std::size_t index = scrollOffset_ + row;
    if (index >= decks_.size()) {
      break;
    }
    const DeckSummary &deck = decks_.at(index);
    const sf::FloatRect rect = rowRect(row);

    // Selected row gets a gold-tinted strip behind the text for contrast.
    const bool selected = selected_.has_value() && selected_.value() == index;
    if (selected) {
      sf::RectangleShape highlight({rect.width, rect.height});
      highlight.setPosition({rect.left, rect.top});
      highlight.setFillColor(menuPalette().hoverFill);
      target.draw(highlight);
    }

    // Thumbnail swatch: a parchment-toned stand-in for the deck's preview art
    // (real images arrive with the Sprint 10 cache). Keeps a ~3:4 card aspect.
    const float thumbWidth = kThumbWidth * scale_;
    const float thumbHeight = kThumbHeight * scale_;
    sf::RectangleShape thumb({thumbWidth, thumbHeight});
    thumb.setPosition(
        {rect.left + (kRowPad * scale_), rect.top + ((rect.height - thumbHeight) / 2.f)});
    thumb.setFillColor(selected ? menuPalette().pressedFill : menuPalette().parchment);
    thumb.setOutlineThickness(1.f);
    thumb.setOutlineColor(menuPalette().gold);
    target.draw(thumb);

    // Name (bold) above the "format - N cards" meta line. ASCII only: sf::Text
    // decodes const char* through the C locale, which garbles non-ASCII.
    sf::Text name(deck.name, boldFont, nameSize);
    name.setFillColor(selected ? menuPalette().background : menuPalette().parchment);
    name.setPosition({rect.left + (kTextGap * scale_), rect.top + (10.f * scale_)});
    target.draw(name);
    const std::string meta = deck.format + " - " + std::to_string(deck.total_cards) + " cards";
    sf::Text metaText(meta, font, metaSize);
    metaText.setFillColor(selected ? menuPalette().ink : menuPalette().muted);
    metaText.setPosition({rect.left + (kTextGap * scale_), rect.top + (30.f * scale_)});
    target.draw(metaText);

    // The per-row Delete button (danger-styled: a red label on a dark field).
    const sf::FloatRect deleteRect = deleteButtonRect(rect);
    sf::RectangleShape button({deleteRect.width, deleteRect.height});
    button.setPosition({deleteRect.left, deleteRect.top});
    button.setFillColor(menuPalette().background);
    button.setOutlineThickness(1.f);
    button.setOutlineColor(menuPalette().danger);
    target.draw(button);
    sf::Text del("Delete", font, deleteSize);
    del.setFillColor(menuPalette().danger);
    const sf::FloatRect delBounds = del.getLocalBounds();
    del.setPosition(
        {deleteRect.left + ((deleteRect.width - delBounds.width) / 2.f) - delBounds.left,
         deleteRect.top + ((deleteRect.height - delBounds.height) / 2.f) - delBounds.top});
    target.draw(del);
  }
}

} // namespace mtgcpp::core
