// CardView implementation (M9.3): paints one battlefield card into a slot.

#include "ui/widgets/card_view.h"

#include "ui/theme.h"

#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <string>

namespace mtgcpp::core {

void CardView::setCard(BoardCard card) { card_ = std::move(card); }

void CardView::setPosition(sf::Vector2f position) { position_ = position; }

void CardView::setSize(sf::Vector2f size) { size_ = size; }

sf::FloatRect CardView::bounds() const { return {position_.x, position_.y, size_.x, size_.y}; }

bool CardView::contains(sf::Vector2f point) const { return bounds().contains(point); }

std::string CardView::badgeText() const {
  return "+" + std::to_string(card_.counters) + "/+" + std::to_string(card_.counters);
}

void CardView::draw(sf::RenderTarget &target, const sf::Font &font, const sf::Texture &front,
                    const sf::Texture &back, const sf::Texture &token) const {
  if (size_.x <= 0.f || size_.y <= 0.f) {
    return;
  }
  // The face texture: token placeholders and face-down cards swap the front.
  const sf::Texture *face = &front;
  if (showsToken()) {
    face = &token;
  } else if (showsBack()) {
    face = &back;
  }
  if (face->getSize().x == 0u || face->getSize().y == 0u) {
    return; // texture failed to build — nothing to paint
  }
  const sf::Vector2f center{position_.x + (size_.x / 2.f), position_.y + (size_.y / 2.f)};

  // Sprite scaled to the slot, rotated in place around its center when tapped.
  // The rotation snaps instantly (no animation); a 90-degree tap makes the card
  // read as tapped while keeping its slot's origin.
  sf::Sprite sprite(*face);
  const float scaleX = size_.x / static_cast<float>(face->getSize().x);
  const float scaleY = size_.y / static_cast<float>(face->getSize().y);
  sprite.setScale({scaleX, scaleY});
  sprite.setOrigin(
      {static_cast<float>(face->getSize().x) / 2.f, static_cast<float>(face->getSize().y) / 2.f});
  sprite.setPosition(center);
  sprite.setRotation(tapAngleDegrees());
  target.draw(sprite);

  // Card name (and mana cost) overlaid on the face. It rotates with the card
  // so a tapped card keeps its label readable on the rotated face. The badge
  // is drawn last so it always sits on top.
  const float nameSize = std::max(8.f, size_.y * 0.16f);
  sf::Text name(nameText(), font, static_cast<unsigned>(nameSize));
  name.setFillColor(tablePalette().ink);
  const sf::FloatRect nameBounds = name.getLocalBounds();
  name.setOrigin(
      {nameBounds.left + (nameBounds.width / 2.f), nameBounds.top + (nameBounds.height / 2.f)});
  name.setPosition({center.x, position_.y + size_.y - (nameSize * 0.9f)});
  name.setRotation(tapAngleDegrees());
  target.draw(name);

  if (showsBadge()) {
    const float badgeSize = std::max(8.f, size_.y * 0.14f);
    sf::Text badge(badgeText(), font, static_cast<unsigned>(badgeSize));
    badge.setFillColor(tablePalette().gold);
    badge.setOutlineColor(tablePalette().ink);
    badge.setOutlineThickness(1.f);
    const sf::FloatRect badgeBounds = badge.getLocalBounds();
    badge.setOrigin({badgeBounds.left + (badgeBounds.width / 2.f),
                     badgeBounds.top + (badgeBounds.height / 2.f)});
    badge.setPosition({position_.x + (size_.x / 2.f), position_.y + (badgeSize * 0.9f)});
    badge.setRotation(tapAngleDegrees());
    target.draw(badge);
  }
}

} // namespace mtgcpp::core
