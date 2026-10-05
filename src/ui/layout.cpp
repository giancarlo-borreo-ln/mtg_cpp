// Responsive layout toolkit implementation (M4.3 + M9.2): pure rect math.

#include "ui/layout.h"

#include <algorithm>
#include <cmath>

namespace mtgcpp::core {

namespace {
// Clamp a float between two bounds (the project avoids raw math in headers).
float clamp(float value, float lo, float hi) { return std::min(std::max(value, lo), hi); }
} // namespace

float uiScale(sf::Vector2u size) {
  const float height = static_cast<float>(size.y);
  if (height <= 0.f) {
    return 1.f; // no window yet — the default design scale
  }
  // The menu is authored at 600px height. Clamped to a readable range: a tiny
  // window never crushes the menu below 0.75x, and a high-resolution monitor
  // (1440p/4K) scales the UI up to 3x instead of pegging it at 1.5x, which used
  // to leave menus tiny on large displays.
  return clamp(height / kDesignHeight, 0.75f, 3.0f);
}

float px(float designLength, sf::Vector2u size) { return designLength * uiScale(size); }

sf::FloatRect inset(const sf::FloatRect &rect, float amount) {
  return {rect.left + amount, rect.top + amount, rect.width - (2.f * amount),
          rect.height - (2.f * amount)};
}

std::vector<sf::FloatRect> rowOf(const sf::FloatRect &rect, std::size_t count, float gap) {
  std::vector<sf::FloatRect> cells;
  if (count == 0) {
    return cells;
  }
  cells.reserve(count);
  // Every child is `1fr`: each gets an equal share of the space the gaps leave.
  const float totalGap = (static_cast<float>(count) - 1.f) * gap;
  const float cellWidth = (rect.width - totalGap) / static_cast<float>(count);
  const float usedWidth = (cellWidth * static_cast<float>(count)) + totalGap;
  const float startX = rect.left + ((rect.width - usedWidth) / 2.f);
  for (std::size_t i = 0; i < count; ++i) {
    cells.push_back(
        {startX + (static_cast<float>(i) * (cellWidth + gap)), rect.top, cellWidth, rect.height});
  }
  return cells;
}

std::vector<sf::FloatRect> gridCells(const sf::FloatRect &rect, std::size_t count,
                                     std::size_t columns, float gap) {
  std::vector<sf::FloatRect> cells;
  if (count == 0 || columns == 0) {
    return cells;
  }
  cells.reserve(count);
  // Row-major placement: ceil(count / columns) rows, each cell a `1fr` share of
  // the space the gaps leave on both axes.
  const std::size_t rows = (count + columns - 1) / columns;
  const float totalGapX = (static_cast<float>(columns) - 1.f) * gap;
  const float totalGapY = (static_cast<float>(rows) - 1.f) * gap;
  const float cellWidth = (rect.width - totalGapX) / static_cast<float>(columns);
  const float cellHeight = (rect.height - totalGapY) / static_cast<float>(rows);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t row = i / columns;
    const std::size_t col = i % columns;
    cells.push_back({rect.left + (static_cast<float>(col) * (cellWidth + gap)),
                     rect.top + (static_cast<float>(row) * (cellHeight + gap)), cellWidth,
                     cellHeight});
  }
  return cells;
}

std::size_t responsiveColumns(float availableWidth, float minCellWidth, float gap,
                              std::size_t maxColumns) {
  // Greedily add a column as long as `n` cells of minCellWidth plus the gaps
  // between them still fit; the first n that does not fit stops the loop.
  std::size_t columns = 1;
  for (std::size_t n = 1; n <= maxColumns; ++n) {
    const float needed =
        (static_cast<float>(n) * minCellWidth) + ((static_cast<float>(n) - 1.f) * gap);
    if (needed > availableWidth) {
      break;
    }
    columns = n;
  }
  return columns;
}

// -- Shandalar battlefield slot math (M9.2) ---------------------------------

// The five bands' vertical fractions of the gap-free height. Opponent first
// (top) so the table reads top-down: their hand, their zones, the stack, your
// zones, your hand. The hands get more room than the webapp's strips so card
// names stay legible on the arch.
inline constexpr float kTheirHandFrac = 0.16f;
inline constexpr float kTheirGridFrac = 0.27f;
inline constexpr float kStackFrac = 0.14f;
inline constexpr float kMyGridFrac = 0.27f;
inline constexpr float kMyHandFrac = 0.16f;

TableBands tableBands(const sf::FloatRect &content, float gap, float stackWidthFraction) {
  // Four gaps separate the five bands; the fractions then tile the remainder.
  const float gapFree = content.height - (4.f * gap);
  auto bandHeight = [gapFree](float frac) { return gapFree * frac; };
  TableBands bands;
  float y = content.top;
  bands.theirHand = {content.left, y, content.width, bandHeight(kTheirHandFrac)};
  y += bands.theirHand.height + gap;
  bands.theirGrid = {content.left, y, content.width, bandHeight(kTheirGridFrac)};
  y += bands.theirGrid.height + gap;
  const float stackWidth = content.width * stackWidthFraction;
  bands.stack = {content.left + ((content.width - stackWidth) / 2.f), y, stackWidth,
                 bandHeight(kStackFrac)};
  y += bands.stack.height + gap;
  bands.myGrid = {content.left, y, content.width, bandHeight(kMyGridFrac)};
  y += bands.myGrid.height + gap;
  bands.myHand = {content.left, y, content.width, bandHeight(kMyHandFrac)};
  return bands;
}

float handArch(float progress, float amplitude) {
  // A half-sine hump: 0 at both edges, `amplitude` at the middle.
  return amplitude * std::sin(3.14159265f * progress);
}

std::vector<sf::FloatRect> handCardRects(const sf::FloatRect &handRect, std::size_t count,
                                         float archAmplitude, bool archUp) {
  std::vector<sf::FloatRect> rects;
  if (count == 0) {
    return rects;
  }
  rects.reserve(count);
  // One rigid cell per card; the card keeps its aspect and fits the band even
  // at the peak of the arch (the amplitude is reserved on the side the cards
  // move TOWARD, so every card stays inside the strip). Arching moves the card
  // within the band — instant snap, no overlap.
  const float room = std::max(0.f, archAmplitude);
  const float availHeight = std::max(1.f, handRect.height - room);
  const float cellWidth = handRect.width / static_cast<float>(count);
  const float cardHeight = std::min(cellWidth / kCardAspect, availHeight);
  const float cardWidth = cardHeight * kCardAspect;
  const float baseTop =
      archUp ? handRect.top + room : handRect.top + handRect.height - room - cardHeight;
  for (std::size_t i = 0; i < count; ++i) {
    const float cellLeft = handRect.left + (static_cast<float>(i) * cellWidth);
    const float progress =
        count == 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(count - 1);
    const float arch = handArch(progress, room);
    const float top = baseTop + (archUp ? -arch : arch);
    rects.push_back({cellLeft + ((cellWidth - cardWidth) / 2.f), top, cardWidth, cardHeight});
  }
  return rects;
}

sf::FloatRect tableZoneRect(const sf::FloatRect &grid, PlayerZone zone) {
  // 3x3 grid mirroring the webapp's grid-template-areas:
  //   "creatures creatures graveyard"
  //   "instants_sorceries instants_sorceries exile"
  //   "lands lands lands"
  const float col = grid.width / 3.f;
  const float row = grid.height / 3.f;
  switch (zone) {
  case PlayerZone::Lands:
    return {grid.left, grid.top + (2.f * row), 2.f * col, row};
  case PlayerZone::Creatures:
    return {grid.left, grid.top, 2.f * col, row};
  case PlayerZone::InstantsSorceries:
    return {grid.left, grid.top + row, 2.f * col, row};
  case PlayerZone::Graveyard:
    return {grid.left + (2.f * col), grid.top, col, row};
  case PlayerZone::Exile:
    return {grid.left + (2.f * col), grid.top + row, col, row};
  case PlayerZone::Artifacts:
    return {grid.left + (2.f * col), grid.top + (2.f * row), col, row};
  }
  // Exhaustive switch over PlayerZone; unreachable, but return a sane fallback.
  return grid;
}

ZoneCardLayout zoneCardRects(const sf::FloatRect &zoneRect, std::size_t count) {
  ZoneCardLayout layout;
  if (count == 0) {
    return layout;
  }
  layout.cards.reserve(count);
  // Each card snaps into an equal-width cell, sized to fill the zone height
  // while keeping the card aspect (so a wide zone shows small tiles, a narrow
  // one keeps readable ones).
  const float cellWidth = zoneRect.width / static_cast<float>(count);
  const float cardHeight = std::min(cellWidth / kCardAspect, zoneRect.height);
  const float cardWidth = cardHeight * kCardAspect;
  layout.cardSize = {cardWidth, cardHeight};
  const float baseTop = zoneRect.top + ((zoneRect.height - cardHeight) / 2.f);
  for (std::size_t i = 0; i < count; ++i) {
    const float cellLeft = zoneRect.left + (static_cast<float>(i) * cellWidth);
    layout.cards.push_back(
        {cellLeft + ((cellWidth - cardWidth) / 2.f), baseTop, cardWidth, cardHeight});
  }
  return layout;
}

std::vector<sf::FloatRect> stackCardRects(const sf::FloatRect &baseCard, std::size_t count) {
  std::vector<sf::FloatRect> rects;
  rects.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    sf::FloatRect rect = baseCard;
    rect.left += static_cast<float>(i * kStackOffsetX);
    rect.top += static_cast<float>(i * kStackOffsetY);
    rects.push_back(rect);
  }
  return rects;
}

std::optional<std::size_t> indexAtPoint(const std::vector<sf::FloatRect> &rects,
                                        sf::Vector2f point) {
  // Iterate in reverse so the top-most (last-drawn) rect wins on overlap.
  for (std::size_t i = rects.size(); i > 0; --i) {
    const std::size_t index = i - 1;
    if (rects.at(index).contains(point)) {
      return index;
    }
  }
  return std::nullopt;
}

} // namespace mtgcpp::core
