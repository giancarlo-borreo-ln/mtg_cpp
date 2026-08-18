// ZoneView implementation (M9.3): a battlefield zone slot tile.

#include "ui/widgets/zone_view.h"

#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>

namespace mtgcpp::core {

void ZoneView::setPosition(sf::Vector2f position) { position_ = position; }

void ZoneView::setSize(sf::Vector2f size) { size_ = size; }

sf::FloatRect ZoneView::bounds() const { return {position_.x, position_.y, size_.x, size_.y}; }

bool ZoneView::contains(sf::Vector2f point) const { return bounds().contains(point); }

void ZoneView::setLabel(std::string label) { label_ = std::move(label); }

void ZoneView::setEmpty(bool empty) { empty_ = empty; }

std::string ZoneView::emptyHint() const { return empty_ ? "Empty" : ""; }

void ZoneView::draw(sf::RenderTarget &target, const sf::Font &font, const sf::Texture &tile) const {
  if (size_.x <= 0.f || size_.y <= 0.f) {
    return;
  }
  // The beveled tile fills the slot; the label sits along the top edge and the
  // empty hint centered below it, both muted so occupied cards read clearly.
  sf::Sprite sprite(tile);
  const float scaleX = size_.x / static_cast<float>(tile.getSize().x);
  const float scaleY = size_.y / static_cast<float>(tile.getSize().y);
  sprite.setScale({scaleX, scaleY});
  sprite.setPosition(position_);
  target.draw(sprite);

  const float labelSize = std::max(9.f, size_.y * 0.14f);
  sf::Text label(label_, font, static_cast<unsigned>(labelSize));
  label.setFillColor(tablePalette().gold);
  const sf::FloatRect labelBounds = label.getLocalBounds();
  label.setPosition({position_.x + ((size_.x - labelBounds.width) / 2.f) - labelBounds.left,
                     position_.y - labelBounds.top});
  target.draw(label);

  if (empty_) {
    const float hintSize = std::max(8.f, size_.y * 0.12f);
    sf::Text hint(emptyHint(), font, static_cast<unsigned>(hintSize));
    hint.setFillColor(tablePalette().goldDim);
    const sf::FloatRect hintBounds = hint.getLocalBounds();
    hint.setPosition({position_.x + ((size_.x - hintBounds.width) / 2.f) - hintBounds.left,
                      position_.y + (size_.y / 2.f) - hintBounds.top});
    target.draw(hint);
  }
}

} // namespace mtgcpp::core
