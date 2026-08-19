// A single card on the battlefield (M9.3).
//
// The table is click + command, not drag-and-drop, and there is no art cache
// until Sprint 10, so a CardView is mostly a *renderer*: it paints one
// BoardCard into a slot using the procedural front/back/token textures, with
// the card name overlaid, a counter badge when counters exist, and a 90-degree
// tap rotation (instant snap, zero animation — design rule).
//
// Like every widget in this tree the card keeps its state pure and exposes the
// headless-testable logic (showsBack / showsToken / badgeText / tapAngle / hit
// tests); draw() is the only windowed part. The textures are owned by the table
// screen and passed in at draw time.
#pragma once

#include "core/board.h"
#include "ui/widgets/widget.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>

#include <string>

namespace mtgcpp::core {

class CardView {
public:
  // --- Card data ------------------------------------------------------------
  void setCard(BoardCard card);
  const BoardCard &card() const { return card_; }

  // --- Geometry -------------------------------------------------------------
  void setPosition(sf::Vector2f position);
  void setSize(sf::Vector2f size);
  sf::Vector2f position() const { return position_; }
  sf::Vector2f size() const { return size_; }
  sf::FloatRect bounds() const;
  bool contains(sf::Vector2f point) const;

  // --- Pure derived state (headless-testable) -------------------------------
  // A face-down card shows the ornate back; a token shows the token texture.
  bool showsBack() const { return card_.flipped; }
  bool showsToken() const { return card_.is_token; }
  // The +N/+N counter badge only appears when counters exist.
  bool showsBadge() const { return card_.counters > 0; }
  std::string badgeText() const;
  // The name and mana-cost lines overlaid on the face.
  std::string nameText() const { return card_.name; }
  std::string costText() const { return card_.mana_cost; }
  // 90 degrees when tapped, 0 otherwise (the "instant snap" rotation).
  float tapAngleDegrees() const { return card_.tapped ? 90.f : 0.f; }

  // --- Rendering ------------------------------------------------------------
  // Paints the card into the slot. `front`/`back`/`token` are the procedural
  // table textures (owned by the table screen); `art` is the cached per-card
  // art texture (M10.1) or nullptr to keep the procedural front; `font` draws
  // the overlays.
  void draw(sf::RenderTarget &target, const sf::Font &font, const sf::Texture &front,
            const sf::Texture &back, const sf::Texture &token,
            const sf::Texture *art = nullptr) const;

private:
  BoardCard card_;
  sf::Vector2f position_{0.f, 0.f};
  sf::Vector2f size_{0.f, 0.f};
};

} // namespace mtgcpp::core
