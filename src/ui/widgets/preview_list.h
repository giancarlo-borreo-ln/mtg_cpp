// The import-preview panel list (M5.2): resolved cards grouped by section plus
// flagged (unresolved) entries with Replace / Remove controls.
//
// Rows are laid out in a single scrollable list:
//   * a section header row ("Deck" / "Sideboard" / "Commander") for each
//     non-empty group of resolved cards, followed by that group's card rows,
//   * a "Flagged" header followed by one row per unresolved entry, each with a
//     Replace button and a Remove button on the right.
//
// Like every M4.2/M5.x widget the hit-testing is pure math over rects:
// `replaceAt` / `removeAt` locate a flagged row's buttons, clicks queue the
// matching action (consumeReplace / consumeRemove), and scrolling reveals rows
// that overflow the widget. draw() is the only windowed part. Row metrics are
// authored in design pixels and scaled by the responsive UI scale.
#pragma once

#include "core/card.h"
#include "core/import_preview.h"
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

class PreviewList {
public:
  // --- Geometry -------------------------------------------------------------
  void setPosition(sf::Vector2f position);
  void setSize(sf::Vector2f size);
  sf::Vector2f position() const { return position_; }
  sf::Vector2f size() const { return size_; }
  sf::FloatRect bounds() const;
  bool contains(sf::Vector2f point) const;

  // The responsive UI scale; row fonts and buttons are multiplied by it.
  void setScale(float scale);
  float scale() const { return scale_; }

  void setRowHeight(float height); // clamped >= 1 so the row math never divides by 0
  float rowHeight() const { return rowHeight_; }

  // --- Content --------------------------------------------------------------
  // Replaces the preview; recomputes the row list and resets scroll + pending
  // actions.
  void setPreview(const ImportPreview &preview);
  const ImportPreview &preview() const { return preview_; }

  // Number of rows (headers + cards + flagged entries).
  std::size_t rowCount() const { return rows_.size(); }
  bool empty() const { return rows_.empty(); }

  // --- Hit-testing (pure) ---------------------------------------------------
  // The row index under `point` (scrolling accounted for), or nullopt.
  std::optional<std::size_t> rowAt(sf::Vector2f point) const;
  // The flagged entry (index into preview().missing) whose Replace / Remove
  // button contains `point`, or nullopt.
  std::optional<std::size_t> replaceAt(sf::Vector2f point) const;
  std::optional<std::size_t> removeAt(sf::Vector2f point) const;
  std::size_t visibleRowCount() const;

  // --- Scrolling ------------------------------------------------------------
  void setScrollOffset(std::size_t offset); // clamped to the row count
  std::size_t scrollOffset() const { return scrollOffset_; }

  // --- Mouse + events + rendering -------------------------------------------
  // A flagged row's button click queues its action (Replace wins over Remove).
  bool mousePressed(sf::Vector2f point);
  // The flagged entry queued by mousePressed, delivered exactly once.
  std::optional<std::size_t> consumeReplace();
  std::optional<std::size_t> consumeRemove();
  bool handleEvent(const sf::Event &event);
  void draw(sf::RenderTarget &target, const sf::Font &font, const sf::Font &boldFont) const;

private:
  // One flattened preview row: a section header, a resolved card, or a flagged
  // entry (flaggedIndex indexes into preview_.missing).
  enum class RowKind { Header, Card, Flagged };
  struct Row {
    RowKind kind = RowKind::Card;
    std::string text;
    std::optional<std::size_t> flaggedIndex;

    bool operator==(const Row &) const = default;
  };

  // Rebuild `rows_` from the current preview (headers for non-empty section
  // groups, then a Flagged header + one row per unresolved entry).
  void rebuildRows();
  // The on-screen rect of visible row `row` (0-based, scroll already applied).
  sf::FloatRect rowRect(std::size_t row) const;
  // The Replace / Remove button rect, right-aligned in `row`.
  sf::FloatRect buttonRect(const sf::FloatRect &row, bool replace) const;
  // The flagged entry whose `replace`/`remove` button contains `point`.
  std::optional<std::size_t> buttonAt(sf::Vector2f point, bool replace) const;

  sf::Vector2f position_{0.f, 0.f};
  sf::Vector2f size_{0.f, 0.f};
  float scale_ = 1.f;
  float rowHeight_ = 44.f;
  ImportPreview preview_;
  std::vector<Row> rows_;
  std::size_t scrollOffset_ = 0;
  std::optional<std::size_t> replaceQueued_;
  std::optional<std::size_t> removeQueued_;
};

} // namespace mtgcpp::core
