// A scrollable list of saved decks with a per-row Delete button (M4.3).
//
// Unlike the plain ListView, each row is a deck card: a thumbnail swatch, the
// deck name with a "format - N cards" line, and a Delete button on the right.
// The thumbnail is a procedural stand-in for the deck's preview art — real
// images arrive with the Sprint 10 art cache.
//
// Like the other M4.2 widgets, every interaction is pure math over rects:
//   * clicks on a row select it,
//   * clicks on its Delete button queue a deletion (consumeDelete), never a
//     selection — the button is a discrete control inside the row,
//   * scrolling reveals rows that overflow the widget height.
// draw() is the only part needing a window; the rest is headless-testable.
// Row metrics are authored in design pixels and scaled by the responsive UI
// scale (setScale), keeping the thumbnails and buttons proportional on any
// window height.
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

class DeckListView {
public:
  // --- Geometry -------------------------------------------------------------
  void setPosition(sf::Vector2f position);
  void setSize(sf::Vector2f size);
  sf::Vector2f position() const { return position_; }
  sf::Vector2f size() const { return size_; }
  sf::FloatRect bounds() const;
  bool contains(sf::Vector2f point) const;

  // The responsive UI scale; row parts (thumbnail, delete button, fonts) are
  // authored in design pixels and multiplied by it so they stay proportional.
  void setScale(float scale);
  float scale() const { return scale_; }

  // --- Rows -----------------------------------------------------------------
  void setRowHeight(float height); // clamped >= 1 so the row math never divides by 0
  float rowHeight() const { return rowHeight_; }

  // Replaces the deck list; resets scroll, selection and any pending delete.
  void setDecks(std::vector<DeckSummary> decks);
  const std::vector<DeckSummary> &decks() const { return decks_; }
  bool empty() const { return decks_.empty(); }

  // --- Hit-testing (pure) ---------------------------------------------------
  // The deck under `point` (scrolling accounted for), or nullopt when the point
  // is outside the widget or below the last visible row.
  std::optional<std::size_t> rowAt(sf::Vector2f point) const;
  // The deck whose Delete button contains `point`, or nullopt.
  std::optional<std::size_t> deleteAt(sf::Vector2f point) const;
  std::size_t visibleRowCount() const;

  // --- Scrolling ------------------------------------------------------------
  void setScrollOffset(std::size_t offset); // clamped to the item count
  std::size_t scrollOffset() const { return scrollOffset_; }

  // --- Selection ------------------------------------------------------------
  void setSelected(std::optional<std::size_t> index); // out-of-range clears
  std::optional<std::size_t> selectedIndex() const { return selected_; }

  // --- Mouse + events + rendering -------------------------------------------
  // Row click selects; a Delete-button click queues a deletion instead.
  bool mousePressed(sf::Vector2f point);
  // The index queued by mousePressed, delivered exactly once (then cleared).
  std::optional<std::size_t> consumeDelete();
  bool handleEvent(const sf::Event &event);
  void draw(sf::RenderTarget &target, const sf::Font &font, const sf::Font &boldFont) const;

private:
  // The on-screen rect of visible row `row` (0-based, scroll already applied).
  sf::FloatRect rowRect(std::size_t row) const;
  // The Delete button rect for a row (right-aligned inside the row).
  sf::FloatRect deleteButtonRect(const sf::FloatRect &row) const;

  sf::Vector2f position_{0.f, 0.f};
  sf::Vector2f size_{0.f, 0.f};
  float scale_ = 1.f;
  float rowHeight_ = 64.f;
  std::vector<DeckSummary> decks_;
  std::size_t scrollOffset_ = 0;
  std::optional<std::size_t> selected_;
  std::optional<std::size_t> deleteQueued_;
};

} // namespace mtgcpp::core
