// The card command menu (M9.4): click-to-select + command, Shandalar-style.
//
// Right-clicking a card (or pressing a keyboard verb) opens this menu. The
// table screen rebuilds the item labels from the card's state (Tap / Untap,
// Flip to back / Flip to front), and a click on an item queues a selection the
// screen consumes. The menu is a plain vertical list of buttons with the same
// press-inside / release-inside click semantics as Button, fully headless.
#pragma once

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mtgcpp::core {

class ContextMenu {
public:
  // --- Items -----------------------------------------------------------------
  void setItems(std::vector<std::string> items);
  const std::vector<std::string> &items() const { return items_; }
  bool empty() const { return items_.empty(); }
  void clear();

  // --- Geometry --------------------------------------------------------------
  // The menu anchors its top-left at `position`; every item shares `itemHeight`.
  void setPosition(sf::Vector2f position);
  void setItemHeight(float height);
  void setWidth(float width);
  sf::Vector2f position() const { return position_; }
  float itemHeight() const { return itemHeight_; }
  float width() const { return width_; }
  sf::FloatRect bounds() const;
  bool contains(sf::Vector2f point) const;

  // --- Pure mouse logic ------------------------------------------------------
  // The item index under `point`, or nullopt when outside the menu.
  std::optional<std::size_t> itemAt(sf::Vector2f point) const;
  bool mousePressed(sf::Vector2f point);
  bool mouseReleased(sf::Vector2f point);
  // The item clicked (press + release inside), delivered exactly once.
  std::optional<std::size_t> consumeSelection();
  void setMousePosition(sf::Vector2f point);
  bool isHovered() const { return hovered_.has_value(); }

  bool handleEvent(const sf::Event &event);

  // --- Rendering --------------------------------------------------------------
  void draw(sf::RenderTarget &target, const sf::Font &font, float scale) const;

private:
  std::vector<std::string> items_;
  sf::Vector2f position_{0.f, 0.f};
  float itemHeight_ = 32.f;
  float width_ = 180.f;
  std::optional<std::size_t> hovered_;
  std::optional<std::size_t> pressed_;
  std::optional<std::size_t> queued_;
};

} // namespace mtgcpp::core
