// A battlefield zone slot (M9.3).
//
// One of the five rigid zones on a board half: a beveled stone/parchment tile
// with a label and an empty-state hint. The ZoneView is deliberately a *slot*,
// not a pile container — the table screen lays the zone's cards out on top of
// it with the M9.2 slot math (zoneCardRects) and owns hit-testing. It only
// knows its own geometry, label and emptiness, which keeps it headless-testable.
#pragma once

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>

#include <string>

namespace mtgcpp::core {

class ZoneView {
public:
  // --- Geometry -------------------------------------------------------------
  void setPosition(sf::Vector2f position);
  void setSize(sf::Vector2f size);
  sf::Vector2f position() const { return position_; }
  sf::Vector2f size() const { return size_; }
  sf::FloatRect bounds() const;
  bool contains(sf::Vector2f point) const;

  // --- Label + empty state (pure) -------------------------------------------
  void setLabel(std::string label);
  const std::string &label() const { return label_; }
  void setEmpty(bool empty);
  bool empty() const { return empty_; }

  // The empty-state hint text ("" when the zone is occupied). Exposed so tests
  // pin the wording; draw() renders it under the label.
  std::string emptyHint() const;

  // --- Rendering ------------------------------------------------------------
  // Draws the tile (beveled texture), the label and, when empty, the hint.
  // `tile` is the procedural zone tile owned by the table screen.
  void draw(sf::RenderTarget &target, const sf::Font &font, const sf::Texture &tile) const;

private:
  sf::Vector2f position_{0.f, 0.f};
  sf::Vector2f size_{0.f, 0.f};
  std::string label_;
  bool empty_ = true;
};

} // namespace mtgcpp::core
