// A vertical list of text rows with selection and scrolling (M4.2).
//
// The list owns no drawables until draw() runs; every interaction is pure
// math over a rect + a row height, so hit-testing, selection and scrolling are
// fully unit-testable. Rows are rendered from `scrollOffset_` onward, only as
// many as fit in the widget height (`visibleRowCount`).
#pragma once

#include "ui/widgets/widget.h"

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

class ListView {
public:
  // --- Geometry -------------------------------------------------------------
  void setPosition(sf::Vector2f position);
  void setSize(sf::Vector2f size);
  sf::Vector2f position() const { return position_; }
  sf::Vector2f size() const { return size_; }
  sf::FloatRect bounds() const;
  bool contains(sf::Vector2f point) const;

  // --- Rows -----------------------------------------------------------------
  void setRowHeight(float height); // clamped >= 1 so the row math never divides by 0
  float rowHeight() const { return rowHeight_; }

  void setItems(std::vector<std::string> items); // replaces, resets scroll+selection
  void addItem(std::string item);
  void clearItems();
  const std::vector<std::string> &items() const { return items_; }
  bool empty() const { return items_.empty(); }

  // --- Hit-testing (pure) ---------------------------------------------------
  // The item index under `point` (scrolling accounted for), or nullopt when
  // the point is outside the widget or below the last visible item.
  std::optional<std::size_t> rowAt(sf::Vector2f point) const;
  std::size_t visibleRowCount() const;

  // --- Scrolling ------------------------------------------------------------
  void setScrollOffset(std::size_t offset); // clamped to the item count
  std::size_t scrollOffset() const { return scrollOffset_; }

  // --- Selection ------------------------------------------------------------
  void setSelected(std::optional<std::size_t> index); // out-of-range clears
  std::optional<std::size_t> selectedIndex() const { return selected_; }

  // --- Mouse + events + rendering -------------------------------------------
  // Select the clicked row; consumed when the click lands on an item.
  bool mousePressed(sf::Vector2f point);
  bool handleEvent(const sf::Event &event);
  void draw(sf::RenderTarget &target, const sf::Font &font) const;

private:
  sf::Vector2f position_{0.f, 0.f};
  sf::Vector2f size_{0.f, 0.f};
  float rowHeight_ = 24.f;
  std::vector<std::string> items_;
  std::size_t scrollOffset_ = 0;
  std::optional<std::size_t> selected_;
};

} // namespace mtgcpp::core
