// ContextMenu implementation (M9.4): a vertical list of command buttons.

#include "ui/widgets/context_menu.h"

#include "ui/theme.h"
#include "ui/widgets/widget.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <utility>

namespace mtgcpp::core {

void ContextMenu::setItems(std::vector<std::string> items) {
  items_ = std::move(items);
  hovered_.reset();
  pressed_.reset();
  queued_.reset();
}

void ContextMenu::clear() {
  items_.clear();
  hovered_.reset();
  pressed_.reset();
  queued_.reset();
}

void ContextMenu::setPosition(sf::Vector2f position) { position_ = position; }

void ContextMenu::setItemHeight(float height) {
  if (height > 0.f) {
    itemHeight_ = height;
  }
}

void ContextMenu::setWidth(float width) {
  if (width > 0.f) {
    width_ = width;
  }
}

sf::FloatRect ContextMenu::bounds() const {
  return {position_.x, position_.y, width_, itemHeight_ * static_cast<float>(items_.size())};
}

bool ContextMenu::contains(sf::Vector2f point) const { return bounds().contains(point); }

std::optional<std::size_t> ContextMenu::itemAt(sf::Vector2f point) const {
  if (!contains(point)) {
    return std::nullopt;
  }
  const std::size_t index = static_cast<std::size_t>((point.y - position_.y) / itemHeight_);
  if (index >= items_.size()) {
    return std::nullopt;
  }
  return index;
}

bool ContextMenu::mousePressed(sf::Vector2f point) {
  const std::optional<std::size_t> index = itemAt(point);
  if (index.has_value()) {
    pressed_ = index;
    return true;
  }
  pressed_.reset();
  return false;
}

bool ContextMenu::mouseReleased(sf::Vector2f point) {
  if (!pressed_.has_value()) {
    return false;
  }
  const std::optional<std::size_t> index = itemAt(point);
  if (index.has_value() && index.value() == pressed_.value()) {
    queued_ = index;
  }
  pressed_.reset();
  return true;
}

std::optional<std::size_t> ContextMenu::consumeSelection() {
  std::optional<std::size_t> result = queued_;
  queued_.reset();
  return result;
}

void ContextMenu::setMousePosition(sf::Vector2f point) { hovered_ = itemAt(point); }

bool ContextMenu::handleEvent(const sf::Event &event) {
  if (event.type == sf::Event::MouseMoved) {
    setMousePosition(
        {static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y)});
    return false; // hover is never "consumed" — the screen still routes the click
  }
  if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    return mousePressed(
        {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
  }
  if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {
    return mouseReleased(
        {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
  }
  return false;
}

void ContextMenu::draw(sf::RenderTarget &target, const sf::Font &font, float scale) const {
  if (items_.empty()) {
    return;
  }
  const float itemH = itemHeight_ * scale;
  // The panel behind the items.
  sf::RectangleShape panel({width_ * scale, itemH * static_cast<float>(items_.size())});
  panel.setPosition(position_);
  panel.setFillColor(menuPalette().panel);
  panel.setOutlineThickness(2.f * scale);
  panel.setOutlineColor(tablePalette().gold);
  target.draw(panel);

  for (std::size_t i = 0; i < items_.size(); ++i) {
    const float y = position_.y + (itemH * static_cast<float>(i));
    const bool hovered = hovered_.has_value() && hovered_.value() == i;
    const bool pressed = pressed_.has_value() && pressed_.value() == i;
    if (hovered) {
      sf::RectangleShape fill({width_ * scale, itemH});
      fill.setPosition({position_.x, y});
      fill.setFillColor(pressed ? menuPalette().pressedFill : menuPalette().hoverFill);
      target.draw(fill);
    }
    sf::Text label(items_.at(i), font, static_cast<unsigned>(16.f * scale));
    label.setFillColor(menuPalette().parchment);
    const sf::FloatRect labelBounds = label.getLocalBounds();
    label.setPosition({position_.x + (10.f * scale) - labelBounds.left,
                       y + ((itemH - labelBounds.height) / 2.f) - labelBounds.top});
    target.draw(label);
  }
}

} // namespace mtgcpp::core
