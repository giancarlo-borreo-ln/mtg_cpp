// The Deck Editor's live deck list (M5.1): one row per card in the deck being
// built, with quantity controls.
//
// Each row shows the card name + type line and a control cluster on the right:
// a minus button, the current quantity, a plus button, and a remove (x)
// button. Clicking a control queues the matching action (consumeIncrement /
// consumeDecrement / consumeRemove); clicking anywhere else on the row is a
// no-op (rows in the deck list are not selectable in M5.1).
//
// Like every M4.2/M4.3 widget the hit-testing is pure math over rects, so the
// whole list is headless-testable; draw() is the only windowed part. The
// control cluster and fonts are authored in design pixels and scaled by the
// responsive UI scale (setScale).
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

class DeckBuilderList {
public:
  // --- Geometry -------------------------------------------------------------
  void setPosition(sf::Vector2f position);
  void setSize(sf::Vector2f size);
  sf::Vector2f position() const { return position_; }
  sf::Vector2f size() const { return size_; }
  sf::FloatRect bounds() const;
  bool contains(sf::Vector2f point) const;

  // The responsive UI scale; the control cluster and fonts are multiplied by
  // it so the rows stay proportional on any window height.
  void setScale(float scale);
  float scale() const { return scale_; }

  void setRowHeight(float height); // clamped >= 1 so the row math never divides by 0
  float rowHeight() const { return rowHeight_; }

  // --- Rows -----------------------------------------------------------------
  // Replaces the deck rows; resets scroll and any pending control actions.
  void setCards(std::vector<Card> cards);
  const std::vector<Card> &cards() const { return cards_; }
  bool empty() const { return cards_.empty(); }

  // --- Hit-testing (pure) ---------------------------------------------------
  // The deck row index under `point` (scrolling accounted for), or nullopt
  // when the point is outside the widget or below the last visible row.
  std::optional<std::size_t> rowAt(sf::Vector2f point) const;
  // The row whose minus / plus / remove button contains `point`, or nullopt.
  std::optional<std::size_t> minusAt(sf::Vector2f point) const;
  std::optional<std::size_t> plusAt(sf::Vector2f point) const;
  std::optional<std::size_t> removeAt(sf::Vector2f point) const;
  std::size_t visibleRowCount() const;

  // --- Scrolling ------------------------------------------------------------
  void setScrollOffset(std::size_t offset); // clamped to the item count
  std::size_t scrollOffset() const { return scrollOffset_; }

  // --- Mouse + events + rendering -------------------------------------------
  // A control click queues its action (remove wins over plus wins over minus).
  bool mousePressed(sf::Vector2f point);
  // The row queued by mousePressed for each control, delivered exactly once.
  std::optional<std::size_t> consumeIncrement();
  std::optional<std::size_t> consumeDecrement();
  std::optional<std::size_t> consumeRemove();
  bool handleEvent(const sf::Event &event);
  void draw(sf::RenderTarget &target, const sf::Font &font, const sf::Font &boldFont) const;

private:
  // The control buttons that can be hit-tested on a row.
  enum class Control { Minus, Plus, Remove };

  // The on-screen rect of visible row `row` (0-based, scroll already applied).
  sf::FloatRect rowRect(std::size_t row) const;
  // The control cluster (minus / quantity / plus / remove), right-aligned in
  // `row`.
  sf::FloatRect controlRect(const sf::FloatRect &row, Control control) const;
  // The row whose `control` rect contains `point`, or nullopt.
  std::optional<std::size_t> controlAt(sf::Vector2f point, Control control) const;

  sf::Vector2f position_{0.f, 0.f};
  sf::Vector2f size_{0.f, 0.f};
  float scale_ = 1.f;
  float rowHeight_ = 64.f;
  std::vector<Card> cards_;
  std::size_t scrollOffset_ = 0;
  std::optional<std::size_t> incrementQueued_;
  std::optional<std::size_t> decrementQueued_;
  std::optional<std::size_t> removeQueued_;
};

} // namespace mtgcpp::core
