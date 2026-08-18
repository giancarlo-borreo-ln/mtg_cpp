// DeckBuilderList implementation (M5.1): quantity-control hit-testing + rows.

#include "ui/widgets/deck_builder_list.h"

#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace mtgcpp::core {

namespace {
// Row interior metrics, authored in design pixels and scaled by scale_.
constexpr float kRowPad = 10.f;   // left/right gutter inside a row
constexpr float kButtonGap = 6.f; // space between adjacent control buttons
constexpr float kRemoveWidth = 40.f;
constexpr float kPlusWidth = 36.f;
constexpr float kQtyWidth = 36.f;
constexpr float kMinusWidth = 36.f;
constexpr float kTextGap = 58.f; // name/type column starts after this gutter
} // namespace

void DeckBuilderList::setPosition(sf::Vector2f position) { position_ = position; }
void DeckBuilderList::setSize(sf::Vector2f size) { size_ = size; }
sf::FloatRect DeckBuilderList::bounds() const { return {position_, size_}; }
bool DeckBuilderList::contains(sf::Vector2f point) const {
  return pointInside(position_, size_, point);
}

void DeckBuilderList::setScale(float scale) { scale_ = std::max(scale, 1.f); }
void DeckBuilderList::setRowHeight(float height) { rowHeight_ = std::max(height, 1.f); }

void DeckBuilderList::setCards(std::vector<Card> cards) {
  cards_ = std::move(cards);
  scrollOffset_ = 0;
  incrementQueued_.reset();
  decrementQueued_.reset();
  removeQueued_.reset();
}

sf::FloatRect DeckBuilderList::rowRect(std::size_t row) const {
  const float y = position_.y + (static_cast<float>(row) * rowHeight_);
  return {position_.x, y, size_.x, rowHeight_};
}

std::optional<std::size_t> DeckBuilderList::rowAt(sf::Vector2f point) const {
  if (!contains(point)) {
    return std::nullopt;
  }
  // Which visible row the y-coordinate falls in (integer division truncates).
  const std::size_t visible = static_cast<std::size_t>((point.y - position_.y) / rowHeight_);
  const std::size_t index = scrollOffset_ + visible;
  if (index >= cards_.size()) {
    return std::nullopt; // past the last row (empty tail of the widget)
  }
  return index;
}

sf::FloatRect DeckBuilderList::controlRect(const sf::FloatRect &row, Control control) const {
  const float buttonHeight = std::min(30.f * scale_, row.height);
  const float y = row.top + ((row.height - buttonHeight) / 2.f);
  // The cluster is right-aligned: remove at the edge, then plus, then the
  // quantity readout between the two steppers.
  const float removeLeft = row.left + row.width - (kRemoveWidth * scale_) - (kRowPad * scale_);
  const float plusLeft = removeLeft - (kButtonGap * scale_) - (kPlusWidth * scale_);
  const float qtyLeft = plusLeft - (kButtonGap * scale_) - (kQtyWidth * scale_);
  const float minusLeft = qtyLeft - (kButtonGap * scale_) - (kMinusWidth * scale_);
  switch (control) {
  case Control::Minus:
    return {minusLeft, y, kMinusWidth * scale_, buttonHeight};
  case Control::Plus:
    return {plusLeft, y, kPlusWidth * scale_, buttonHeight};
  case Control::Remove:
    return {removeLeft, y, kRemoveWidth * scale_, buttonHeight};
  }
  return {qtyLeft, y, kQtyWidth * scale_, buttonHeight}; // unreachable, keeps the compiler happy
}

std::optional<std::size_t> DeckBuilderList::controlAt(sf::Vector2f point, Control control) const {
  const std::optional<std::size_t> row = rowAt(point);
  if (!row.has_value()) {
    return std::nullopt;
  }
  const std::size_t visible = row.value() - scrollOffset_;
  const sf::FloatRect button = controlRect(rowRect(visible), control);
  if (pointInside({button.left, button.top}, {button.width, button.height}, point)) {
    return row;
  }
  return std::nullopt;
}

std::optional<std::size_t> DeckBuilderList::minusAt(sf::Vector2f point) const {
  return controlAt(point, Control::Minus);
}

std::optional<std::size_t> DeckBuilderList::plusAt(sf::Vector2f point) const {
  return controlAt(point, Control::Plus);
}

std::optional<std::size_t> DeckBuilderList::removeAt(sf::Vector2f point) const {
  return controlAt(point, Control::Remove);
}

std::size_t DeckBuilderList::visibleRowCount() const {
  return static_cast<std::size_t>(size_.y / rowHeight_);
}

void DeckBuilderList::setScrollOffset(std::size_t offset) {
  scrollOffset_ = std::min(offset, cards_.size());
}

bool DeckBuilderList::mousePressed(sf::Vector2f point) {
  // Remove is the destructive control and sits at the row's edge; it wins over
  // the steppers so a misclick on x can never silently change a quantity.
  const std::optional<std::size_t> remove = controlAt(point, Control::Remove);
  if (remove.has_value()) {
    removeQueued_ = remove;
    return true;
  }
  const std::optional<std::size_t> plus = controlAt(point, Control::Plus);
  if (plus.has_value()) {
    incrementQueued_ = plus;
    return true;
  }
  const std::optional<std::size_t> minus = controlAt(point, Control::Minus);
  if (minus.has_value()) {
    decrementQueued_ = minus;
    return true;
  }
  return false; // the row body is not interactive in M5.1
}

std::optional<std::size_t> DeckBuilderList::consumeIncrement() {
  if (incrementQueued_.has_value()) {
    const std::optional<std::size_t> value = incrementQueued_;
    incrementQueued_.reset();
    return value;
  }
  return std::nullopt;
}

std::optional<std::size_t> DeckBuilderList::consumeDecrement() {
  if (decrementQueued_.has_value()) {
    const std::optional<std::size_t> value = decrementQueued_;
    decrementQueued_.reset();
    return value;
  }
  return std::nullopt;
}

std::optional<std::size_t> DeckBuilderList::consumeRemove() {
  if (removeQueued_.has_value()) {
    const std::optional<std::size_t> value = removeQueued_;
    removeQueued_.reset();
    return value;
  }
  return std::nullopt;
}

bool DeckBuilderList::handleEvent(const sf::Event &event) {
  if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    return mousePressed(
        {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
  }
  return false;
}

void DeckBuilderList::draw(sf::RenderTarget &target, const sf::Font &font,
                           const sf::Font &boldFont) const {
  // The list frame: the window's background color so the rows read as cards
  // sitting in a dark slot inside its parent panel, framed by a gold border.
  sf::RectangleShape panel(size_);
  panel.setPosition(position_);
  panel.setFillColor(menuPalette().background);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(menuPalette().gold);
  target.draw(panel);

  const std::size_t visible = std::min(visibleRowCount(), cards_.size());
  const unsigned nameSize = static_cast<unsigned>(15.f * scale_);
  const unsigned typeSize = static_cast<unsigned>(12.f * scale_);
  const unsigned ctrlSize = static_cast<unsigned>(16.f * scale_);
  for (std::size_t row = 0; row < visible; ++row) {
    const std::size_t index = scrollOffset_ + row;
    if (index >= cards_.size()) {
      break;
    }
    const Card &card = cards_.at(index);
    const sf::FloatRect rect = rowRect(row);

    // Name (bold) over the muted type line. ASCII only: sf::Text decodes
    // const char* through the C locale, which garbles non-ASCII.
    sf::Text name(card.name, boldFont, nameSize);
    name.setFillColor(menuPalette().parchment);
    name.setPosition({rect.left + (kTextGap * scale_), rect.top + (8.f * scale_)});
    target.draw(name);
    sf::Text type(card.type_line.empty() ? "Unknown type" : card.type_line, font, typeSize);
    type.setFillColor(menuPalette().muted);
    type.setPosition({rect.left + (kTextGap * scale_), rect.top + (30.f * scale_)});
    target.draw(type);

    // The control cluster: minus, quantity readout, plus, remove. Parchment
    // buttons with gold outlines; the quantity sits centered between the
    // steppers and the remove button is danger-styled.
    const sf::FloatRect minus = controlRect(rect, Control::Minus);
    sf::RectangleShape minusShape({minus.width, minus.height});
    minusShape.setPosition({minus.left, minus.top});
    minusShape.setFillColor(menuPalette().parchment);
    minusShape.setOutlineThickness(1.f);
    minusShape.setOutlineColor(menuPalette().gold);
    target.draw(minusShape);
    sf::Text minusLabel("-", font, ctrlSize);
    minusLabel.setFillColor(menuPalette().background);
    const sf::FloatRect minusBounds = minusLabel.getLocalBounds();
    minusLabel.setPosition(
        {minus.left + ((minus.width - minusBounds.width) / 2.f) - minusBounds.left,
         minus.top + ((minus.height - minusBounds.height) / 2.f) - minusBounds.top});
    target.draw(minusLabel);

    const sf::FloatRect plus = controlRect(rect, Control::Plus);
    sf::RectangleShape plusShape({plus.width, plus.height});
    plusShape.setPosition({plus.left, plus.top});
    plusShape.setFillColor(menuPalette().parchment);
    plusShape.setOutlineThickness(1.f);
    plusShape.setOutlineColor(menuPalette().gold);
    target.draw(plusShape);
    sf::Text plusLabel("+", font, ctrlSize);
    plusLabel.setFillColor(menuPalette().background);
    const sf::FloatRect plusBounds = plusLabel.getLocalBounds();
    plusLabel.setPosition({plus.left + ((plus.width - plusBounds.width) / 2.f) - plusBounds.left,
                           plus.top + ((plus.height - plusBounds.height) / 2.f) - plusBounds.top});
    target.draw(plusLabel);

    // Quantity readout between the steppers (gold, bold).
    sf::Text qty(std::to_string(card.quantity), boldFont, ctrlSize);
    qty.setFillColor(menuPalette().gold);
    const sf::FloatRect qtyBounds = qty.getLocalBounds();
    qty.setPosition({rect.left + rect.width - (kRowPad * scale_) - (kRemoveWidth * scale_) -
                         (kButtonGap * scale_) - (kPlusWidth * scale_) -
                         (((kQtyWidth * scale_) - qtyBounds.width) / 2.f) - qtyBounds.left,
                     rect.top + ((rect.height - qtyBounds.height) / 2.f) - qtyBounds.top});
    target.draw(qty);

    const sf::FloatRect remove = controlRect(rect, Control::Remove);
    sf::RectangleShape removeShape({remove.width, remove.height});
    removeShape.setPosition({remove.left, remove.top});
    removeShape.setFillColor(menuPalette().background);
    removeShape.setOutlineThickness(1.f);
    removeShape.setOutlineColor(menuPalette().danger);
    target.draw(removeShape);
    sf::Text removeLabel("x", font, ctrlSize);
    removeLabel.setFillColor(menuPalette().danger);
    const sf::FloatRect removeBounds = removeLabel.getLocalBounds();
    removeLabel.setPosition(
        {remove.left + ((remove.width - removeBounds.width) / 2.f) - removeBounds.left,
         remove.top + ((remove.height - removeBounds.height) / 2.f) - removeBounds.top});
    target.draw(removeLabel);
  }
}

} // namespace mtgcpp::core
