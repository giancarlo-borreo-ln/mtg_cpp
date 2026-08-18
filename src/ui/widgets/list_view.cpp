// ListView implementation (M4.2): row hit-testing, selection, scrolling + draw.

#include "ui/widgets/list_view.h"

#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <utility>

namespace mtgcpp::core {

void ListView::setPosition(sf::Vector2f position) { position_ = position; }
void ListView::setSize(sf::Vector2f size) { size_ = size; }
sf::FloatRect ListView::bounds() const { return {position_, size_}; }
bool ListView::contains(sf::Vector2f point) const { return pointInside(position_, size_, point); }

void ListView::setRowHeight(float height) { rowHeight_ = std::max(height, 1.f); }

void ListView::setItems(std::vector<std::string> items) {
  items_ = std::move(items);
  scrollOffset_ = 0;
  selected_.reset();
}

void ListView::addItem(std::string item) {
  items_.push_back(std::move(item));
  // Auto-scroll so the freshly added row is visible: lists grow from the
  // bottom, and without this an addition silently disappears below the fold.
  const std::size_t visible = visibleRowCount();
  if (items_.size() > visible) {
    scrollOffset_ = items_.size() - visible;
  }
}

void ListView::clearItems() {
  items_.clear();
  scrollOffset_ = 0;
  selected_.reset();
}

std::optional<std::size_t> ListView::rowAt(sf::Vector2f point) const {
  if (!contains(point)) {
    return std::nullopt;
  }
  // Which visible row the y-coordinate falls in (integer division truncates).
  const std::size_t visible = static_cast<std::size_t>((point.y - position_.y) / rowHeight_);
  const std::size_t index = scrollOffset_ + visible;
  if (index >= items_.size()) {
    return std::nullopt; // past the last item (empty tail of the widget)
  }
  return index;
}

std::size_t ListView::visibleRowCount() const {
  return static_cast<std::size_t>(size_.y / rowHeight_);
}

void ListView::setScrollOffset(std::size_t offset) {
  scrollOffset_ = std::min(offset, items_.size());
}

void ListView::setSelected(std::optional<std::size_t> index) {
  selected_.reset();
  if (index.has_value() && index.value() < items_.size()) {
    selected_ = index; // assign the guarded optional directly (no value round-trip)
  }
}

bool ListView::mousePressed(sf::Vector2f point) {
  const std::optional<std::size_t> row = rowAt(point);
  if (row.has_value()) {
    setSelected(row);
    return true;
  }
  return false;
}

bool ListView::handleEvent(const sf::Event &event) {
  if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    return mousePressed(
        {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
  }
  return false;
}

void ListView::draw(sf::RenderTarget &target, const sf::Font &font) const {
  // The list frame: the window's background color so the rows read as cards
  // sitting in a dark slot inside its parent panel, framed by a gold border.
  sf::RectangleShape panel(size_);
  panel.setPosition(position_);
  panel.setFillColor(menuPalette().background);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(menuPalette().gold);
  target.draw(panel);

  const std::size_t visible = std::min(visibleRowCount(), items_.size());
  for (std::size_t row = 0; row < visible; ++row) {
    const std::size_t index = scrollOffset_ + row;
    if (index >= items_.size()) {
      break;
    }
    const float y = position_.y + (static_cast<float>(row) * rowHeight_);

    // Selected row gets a gold-tinted strip behind the text for contrast.
    const bool selected = selected_.has_value() && selected_.value() == index;
    if (selected) {
      sf::RectangleShape highlight({size_.x, rowHeight_});
      highlight.setPosition({position_.x, y});
      highlight.setFillColor(menuPalette().hoverFill);
      target.draw(highlight);
    }

    sf::Text rowText(items_.at(index), font, 15u);
    rowText.setFillColor(selected ? menuPalette().background : menuPalette().parchment);
    const float textY = y + ((rowHeight_ - rowText.getLocalBounds().height) / 2.f);
    rowText.setPosition({position_.x + 6.f, textY});
    target.draw(rowText);
  }
}

} // namespace mtgcpp::core
