// Responsive menu layout toolkit (M4.3 foundation; the table screen reuses the
// same ideas in M9.2).
//
// The menus are flexible to the window size. Design rules distilled from the
// responsive-layout review (CSS Grid `auto-fill minmax()`, flexbox `1fr`
// distribution, Qt/WPF anchor-based desktop layouts):
//
//   * One layout pass — a screen recomputes EVERY widget rect from the window
//     size in relayout(). Nothing is positioned once in a constructor, so a
//     resize re-flows the whole screen from the same rules.
//   * Height-based scale — the menu is authored against a 600px-tall reference
//     window; uiScale() scales spacing/fonts with the real window height
//     (clamped), so controls stay readable on small laptops and large monitors.
//   * Responsive grids — columns auto-fill from the available width with a
//     minimum cell width (the CSS `repeat(auto-fill, minmax(min, 1fr))`
//     pattern): responsiveColumns() picks the count, gridCells() distributes
//     the leftover space equally (the `1fr` part). Fewer columns on narrow
//     windows, more on wide ones.
//   * Flex rows — action buttons share a row evenly (rowOf = `1fr 1fr ...`).
//   * Minimum window size — layouts never collapse (enforced in App).
//
// Everything here is pure math over sf::FloatRect / sf::Vector2u, so the whole
// toolkit is unit-testable without a display.
#pragma once

#include "core/board.h"

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace mtgcpp::core {

// Reference height the menu layouts are authored for (matches the original
// 600px-tall window). Scaling against height keeps a consistent feel when the
// aspect ratio changes, not just when the window grows uniformly.
inline constexpr float kDesignHeight = 600.f;

// Smallest window we lay out for. App clamps the reported size up to these so
// no screen has to guard against a collapsed window.
inline constexpr unsigned kMinWindowWidth = 640u;
inline constexpr unsigned kMinWindowHeight = 480u;

// Uniform UI scale for a window of `size`: designHeight/600, clamped to a
// readable range so huge windows do not balloon the menu and tiny windows do
// not crush it. Width does not drive the scale (height-first design). A
// zero-sized window (before the first run()) yields the default scale 1.
float uiScale(sf::Vector2u size);

// A "design pixel" length (authored at 600px height) converted to this window's
// pixels: `designLength * uiScale(size)`.
float px(float designLength, sf::Vector2u size);

// Shrink a rect by `amount` on every side (positive values inset inward).
sf::FloatRect inset(const sf::FloatRect &rect, float amount);

// Split `rect` into `count` equal columns separated by `gap` — a flex row where
// every child is `1fr`. The row is centered inside `rect`; leftover space lands
// on the outer edges. Returns `count` rects in left-to-right order.
std::vector<sf::FloatRect> rowOf(const sf::FloatRect &rect, std::size_t count, float gap);

// Lay `count` cells into `rect` as a `columns`-wide grid (row-major) with `gap`
// spacing; leftover space is shared equally between cells (CSS `1fr`). All cells
// share the grid's height (they stretch to fill, like align-items: stretch).
// Returns `count` rects; an empty result for count == 0 or columns == 0.
std::vector<sf::FloatRect> gridCells(const sf::FloatRect &rect, std::size_t count,
                                     std::size_t columns, float gap);

// How many columns fit `availableWidth` given a minimum cell width and the gap
// between cells — the column count behind CSS `repeat(auto-fill, minmax(min,
// 1fr))`. Capped at `maxColumns`. Always returns at least 1.
std::size_t responsiveColumns(float availableWidth, float minCellWidth, float gap,
                              std::size_t maxColumns);

// ---------------------------------------------------------------------------
// Shandalar battlefield layout (M9.2) — pure slot math for the table screen.
//
// This is NOT a port of the webapp's CSS grid (owner decision §1b): the table
// is a central velvet mat with arched hand rows at the top/bottom, two
// 3-column zone grids (opponent's on top, yours below), and the Stack in the
// middle as the only permitted overlap. Everything is pure rect math, so the
// whole module is unit-testable without a display.
// ---------------------------------------------------------------------------

// Magic card face aspect ratio (width / height): a 63x88 mm card ~= 0.716.
inline constexpr float kCardAspect = 63.f / 88.f;

// The five horizontal bands of the table's content area, top to bottom.
struct TableBands {
  sf::FloatRect theirHand; // opponent's arched hand strip (top)
  sf::FloatRect theirGrid; // opponent's 3-column zone grid
  sf::FloatRect stack;     // the shared Stack (only permitted overlap)
  sf::FloatRect myGrid;    // your 3-column zone grid
  sf::FloatRect myHand;    // your arched hand strip (bottom)
};

// Split `content` into the five table bands. Each band is a fixed fraction of
// the height left after subtracting the 4 inter-band `gap`s; the stack band is
// centered horizontally at `stackWidthFraction` of the content width.
TableBands tableBands(const sf::FloatRect &content, float gap, float stackWidthFraction);

// Vertical arch offset for a hand card at `progress` in [0, 1] across the row
// (0 at the edges, peak at 0.5). Positive offsets move the card up. `amplitude`
// is in pixels (the caller scales it with the window).
float handArch(float progress, float amplitude);

// Lay `count` hand cards into `handRect` as a rigid arched row: each card snaps
// to an equal-width cell (no overlap), keeps kCardAspect, and is arched by
// handArch — the opponent's top hand rises toward the middle (`archUp`), yours
// dips. Returns count rects (empty when count == 0).
std::vector<sf::FloatRect> handCardRects(const sf::FloatRect &handRect, std::size_t count,
                                         float archAmplitude, bool archUp);

// The rect for one zone inside a half's 3-column zone grid. The grid is laid
// out exactly like the webapp's grid-template-areas shape (3x3): creatures
// top-left pair, instants/sorceries middle pair, lands the bottom row,
// graveyard and exile in the right-hand cells.
sf::FloatRect tableZoneRect(const sf::FloatRect &grid, PlayerZone zone);

// The card tiles for a zone pile: cards snap into rigid equal-width cells (no
// overlap), sized to fill the zone while keeping kCardAspect. Also returns the
// shared card size (both empty when count == 0).
struct ZoneCardLayout {
  std::vector<sf::FloatRect> cards;
  sf::Vector2f cardSize;
};
ZoneCardLayout zoneCardRects(const sf::FloatRect &zoneRect, std::size_t count);

// The Stack is the ONLY permitted overlap: card `i` sits at the stack's base
// card rect shifted by (kStackOffsetX, kStackOffsetY) * i, so the pile grows in
// play order. Returns count rects.
std::vector<sf::FloatRect> stackCardRects(const sf::FloatRect &baseCard, std::size_t count);

// The index of the top-most rect at `point`, or nullopt when none contains it.
// Iterates last-to-first so overlapping cards hit the one drawn on top.
std::optional<std::size_t> indexAtPoint(const std::vector<sf::FloatRect> &rects,
                                        sf::Vector2f point);

} // namespace mtgcpp::core
