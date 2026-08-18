// A scrollable list of card search results (M5.1).
//
// The Deck Editor shows the local card database search results here: each row
// is one card (name over a "SET number" meta line), and clicking a row queues
// an "add" (consumeAdd) instead of selecting — the row is a discrete control
// that adds that card to the deck, the same pattern DeckListView uses for its
// per-row Delete button.
//
// Like every M4.2/M4.3 widget the interaction is pure math over rects (rowAt /
// consumeAdd), so it is headless-testable; draw() is the only windowed part.
// Row metrics are authored in design pixels and scaled by the responsive UI
// scale (setScale), matching the deck list's row treatment.
#pragma once

#include "core/card.h"
#include "ui/widgets/widget.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace mtgcpp::core {

class CardList {
public:
  // --- Geometry -------------------------------------------------------------
  void setPosition(sf::Vector2f position);
  void setSize(sf::Vector2f size);
  sf::Vector2f position() const { return position_; }
  sf::Vector2f size() const { return size_; }
  sf::FloatRect bounds() const;
  bool contains(sf::Vector2f point) const;

  // The responsive UI scale; row fonts are multiplied by it so the rows stay
  // proportional on any window height.
  void setScale(float scale);
  float scale() const { return scale_; }

  void setRowHeight(float height); // clamped >= 1 so the row math never divides by 0
  float rowHeight() const { return rowHeight_; }

  // --- Rows -----------------------------------------------------------------
  // Replaces the result list; resets scroll and any pending add.
  void setCards(std::vector<Card> cards);
  const std::vector<Card> &cards() const { return cards_; }
  bool empty() const { return cards_.empty(); }

  // --- Hit-testing (pure) ---------------------------------------------------
  // The result index under `point` (scrolling accounted for), or nullopt when
  // the point is outside the widget or below the last visible row.
  std::optional<std::size_t> rowAt(sf::Vector2f point) const;
  std::size_t visibleRowCount() const;

  // --- Scrolling ------------------------------------------------------------
  void setScrollOffset(std::size_t offset); // clamped to the item count
  std::size_t scrollOffset() const { return scrollOffset_; }

  // --- Mouse + events + rendering -------------------------------------------
  // Clicking a result row queues an add; the screen polls consumeAdd.
  bool mousePressed(sf::Vector2f point);
  // The row queued by mousePressed, delivered exactly once (then cleared).
  std::optional<std::size_t> consumeAdd();
  bool handleEvent(const sf::Event &event);
  void draw(sf::RenderTarget &target, const sf::Font &font, const sf::Font &boldFont) const;

private:
  sf::Vector2f position_{0.f, 0.f};
  sf::Vector2f size_{0.f, 0.f};
  float scale_ = 1.f;
  float rowHeight_ = 56.f;
  std::vector<Card> cards_;
  std::size_t scrollOffset_ = 0;
  std::optional<std::size_t> addQueued_;
};

} // namespace mtgcpp::core
