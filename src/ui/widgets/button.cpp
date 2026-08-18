// Button implementation (M4.2): pure mouse-state transitions + rendering.

#include "ui/widgets/button.h"

#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <utility>

namespace mtgcpp::core {

void Button::setPosition(sf::Vector2f position) { position_ = position; }
void Button::setSize(sf::Vector2f size) { size_ = size; }

sf::FloatRect Button::bounds() const { return {position_, size_}; }
bool Button::contains(sf::Vector2f point) const { return pointInside(position_, size_, point); }

void Button::setLabel(std::string label) { label_ = std::move(label); }
void Button::setSubLabel(std::string subLabel) { subLabel_ = std::move(subLabel); }
void Button::setEnabled(bool enabled) { enabled_ = enabled; }

void Button::setMousePosition(sf::Vector2f point) {
  hovered_ = enabled_ && contains(point);
  if (!hovered_) {
    // The cursor left while the button was held down: cancel the press so a
    // release outside can never trigger a click.
    pressed_ = false;
  }
}

bool Button::mouseButtonPressed(sf::Vector2f point) {
  if (!enabled_ || !contains(point)) {
    return false; // not ours — leave the event unconsumed
  }
  pressed_ = true;
  return true;
}

bool Button::mouseButtonReleased(sf::Vector2f point) {
  if (!enabled_ || !pressed_) {
    return false;
  }
  pressed_ = false;
  if (contains(point)) {
    clickQueued_ = true;
  }
  return true; // the press started on us, so the release belongs to us too
}

bool Button::consumeClicked() {
  if (clickQueued_) {
    clickQueued_ = false;
    return true;
  }
  return false;
}

bool Button::handleEvent(const sf::Event &event) {
  switch (event.type) {
  case sf::Event::MouseMoved:
    setMousePosition(
        {static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y)});
    return false; // hover is passive — nothing is consumed
  case sf::Event::MouseButtonPressed:
    if (event.mouseButton.button == sf::Mouse::Left) {
      return mouseButtonPressed(
          {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
    }
    return false;
  case sf::Event::MouseButtonReleased:
    if (event.mouseButton.button == sf::Mouse::Left) {
      return mouseButtonReleased(
          {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
    }
    return false;
  default:
    return false;
  }
}

void Button::draw(sf::RenderTarget &target, const sf::Font &font, float scale) const {
  // Panel: a plain rectangle. Fill changes with state so the user sees
  // hover/press feedback; a gold outline keeps it framed on the menu.
  sf::RectangleShape panel(size_);
  panel.setPosition(position_);
  panel.setOutlineThickness(std::max(1.f, 2.f * scale));
  panel.setOutlineColor(menuPalette().gold);
  sf::Color fill = menuPalette().parchment;
  if (!enabled_) {
    fill = menuPalette().muted;
  } else if (pressed_) {
    fill = menuPalette().pressedFill;
  } else if (hovered_) {
    fill = menuPalette().hoverFill;
  }
  panel.setFillColor(fill);
  target.draw(panel);

  // The label (and optional subtitle) are vertically centered as a pair so the
  // card reads as one unit. getLocalBounds() measures the text; the origin of
  // the measured box is usually not (0,0) (ascenders/descenders), so both the
  // offset and the origin must be subtracted to truly center it.
  const unsigned labelSize = static_cast<unsigned>(17.f * scale);
  const unsigned subSize = static_cast<unsigned>(13.f * scale);
  if (subLabel_.empty()) {
    sf::Text label(label_, font, labelSize);
    label.setFillColor(menuPalette().background);
    const sf::FloatRect textBounds = label.getLocalBounds();
    label.setPosition({position_.x + ((size_.x - textBounds.width) / 2.f) - textBounds.left,
                       position_.y + ((size_.y - textBounds.height) / 2.f) - textBounds.top});
    target.draw(label);
    return;
  }

  // Two-line card: the label sits above the subtitle, and the pair is centered
  // as a block so a taller button still reads balanced.
  sf::Text label(label_, font, labelSize);
  label.setFillColor(menuPalette().background);
  sf::Text sub(subLabel_, font, subSize);
  sub.setFillColor(menuPalette().ink);
  const sf::FloatRect labelBounds = label.getLocalBounds();
  const sf::FloatRect subBounds = sub.getLocalBounds();
  const float blockHeight = labelBounds.height + 4.f + subBounds.height;
  const float blockY = position_.y + ((size_.y - blockHeight) / 2.f);
  label.setPosition({position_.x + ((size_.x - labelBounds.width) / 2.f) - labelBounds.left,
                     blockY - labelBounds.top});
  target.draw(label);
  sub.setPosition({position_.x + ((size_.x - subBounds.width) / 2.f) - subBounds.left,
                   blockY + labelBounds.height + 4.f - subBounds.top});
  target.draw(sub);
}

} // namespace mtgcpp::core
