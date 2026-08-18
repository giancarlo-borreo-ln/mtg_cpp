// CardList implementation (M5.1): search-result row hit-testing + rendering.

#include "ui/widgets/card_list.h"

#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace mtgcpp::core {

namespace {
// Row interior metrics, authored in design pixels and scaled by scale_.
constexpr float kRowPad = 10.f; // left/right gutter inside a row
} // namespace

void CardList::setPosition(sf::Vector2f position) { position_ = position; }
void CardList::setSize(sf::Vector2f size) { size_ = size; }
sf::FloatRect CardList::bounds() const { return {position_, size_}; }
bool CardList::contains(sf::Vector2f point) const { return pointInside(position_, size_, point); }

void CardList::setScale(float scale) { scale_ = std::max(scale, 1.f); }
void CardList::setRowHeight(float height) { rowHeight_ = std::max(height, 1.f); }

void CardList::setCards(std::vector<Card> cards) {
  cards_ = std::move(cards);
  scrollOffset_ = 0;
  addQueued_.reset();
}

std::optional<std::size_t> CardList::rowAt(sf::Vector2f point) const {
  if (!contains(point)) {
    return std::nullopt;
  }
  // Which visible row the y-coordinate falls in (integer division truncates).
  const std::size_t visible = static_cast<std::size_t>((point.y - position_.y) / rowHeight_);
  const std::size_t index = scrollOffset_ + visible;
  if (index >= cards_.size()) {
    return std::nullopt; // past the last result (empty tail of the widget)
  }
  return index;
}

std::size_t CardList::visibleRowCount() const {
  return static_cast<std::size_t>(size_.y / rowHeight_);
}

void CardList::setScrollOffset(std::size_t offset) {
  scrollOffset_ = std::min(offset, cards_.size());
}

bool CardList::mousePressed(sf::Vector2f point) {
  const std::optional<std::size_t> row = rowAt(point);
  if (row.has_value()) {
    addQueued_ = row; // a result row click always means "add this card"
    return true;
  }
  return false;
}

std::optional<std::size_t> CardList::consumeAdd() {
  if (addQueued_.has_value()) {
    const std::optional<std::size_t> value = addQueued_;
    addQueued_.reset();
    return value;
  }
  return std::nullopt;
}

bool CardList::handleEvent(const sf::Event &event) {
  if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    return mousePressed(
        {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
  }
  return false;
}

void CardList::draw(sf::RenderTarget &target, const sf::Font &font,
                    const sf::Font &boldFont) const {
  // The list frame: the window's background color so the rows read as cards
  // sitting in a dark slot inside the parent panel, framed by a gold border.
  sf::RectangleShape panel(size_);
  panel.setPosition(position_);
  panel.setFillColor(menuPalette().background);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(menuPalette().gold);
  target.draw(panel);

  const std::size_t visible = std::min(visibleRowCount(), cards_.size());
  const unsigned nameSize = static_cast<unsigned>(15.f * scale_);
  const unsigned metaSize = static_cast<unsigned>(12.f * scale_);
  for (std::size_t row = 0; row < visible; ++row) {
    const std::size_t index = scrollOffset_ + row;
    if (index >= cards_.size()) {
      break;
    }
    const Card &card = cards_.at(index);
    const float y = position_.y + (static_cast<float>(row) * rowHeight_);

    // Card name (bold) over the "SET number" meta line; ASCII only (sf::Text
    // decodes const char* through the C locale).
    sf::Text name(card.name, boldFont, nameSize);
    name.setFillColor(menuPalette().parchment);
    name.setPosition({position_.x + (kRowPad * scale_), y + (6.f * scale_)});
    target.draw(name);
    const std::string meta = card.set_code + " " + card.collector_number;
    sf::Text metaText(meta, font, metaSize);
    metaText.setFillColor(menuPalette().muted);
    metaText.setPosition({position_.x + (kRowPad * scale_), y + (26.f * scale_)});
    target.draw(metaText);
  }
}

} // namespace mtgcpp::core
