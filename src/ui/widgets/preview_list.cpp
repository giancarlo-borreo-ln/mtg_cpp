// PreviewList implementation (M5.2): preview rows, Replace/Remove hit-testing,
// scrolling and rendering.

#include "ui/widgets/preview_list.h"

#include "core/arena.h"
#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mtgcpp::core {

namespace {
// Row interior metrics, authored in design pixels and scaled by scale_.
constexpr float kRowPad = 10.f;   // left/right gutter inside a row
constexpr float kButtonGap = 6.f; // space between Replace and Remove
constexpr float kReplaceWidth = 76.f;
constexpr float kRemoveWidth = 64.f;
} // namespace

void PreviewList::setPosition(sf::Vector2f position) { position_ = position; }
void PreviewList::setSize(sf::Vector2f size) { size_ = size; }
sf::FloatRect PreviewList::bounds() const { return {position_, size_}; }
bool PreviewList::contains(sf::Vector2f point) const {
  return pointInside(position_, size_, point);
}

void PreviewList::setScale(float scale) { scale_ = std::max(scale, 1.f); }
void PreviewList::setRowHeight(float height) { rowHeight_ = std::max(height, 1.f); }

void PreviewList::setPreview(const ImportPreview &preview) {
  preview_ = preview;
  rebuildRows();
  scrollOffset_ = 0;
  replaceQueued_.reset();
  removeQueued_.reset();
}

void PreviewList::rebuildRows() {
  rows_.clear();
  // Resolved cards grouped by canonical section, each group under a header.
  const std::vector<ArenaSectionGroup<Card>> groups = groupBySection(preview_.cards);
  for (const ArenaSectionGroup<Card> &group : groups) {
    rows_.push_back({RowKind::Header, sectionLabel(group.section), std::nullopt});
    for (const Card &card : group.items) {
      rows_.push_back(
          {RowKind::Card, card.name + "  x" + std::to_string(card.quantity), std::nullopt});
    }
  }
  // Unresolved entries under a single "Flagged" header.
  if (!preview_.missing.empty()) {
    rows_.push_back({RowKind::Header, "Flagged", std::nullopt});
    for (std::size_t i = 0; i < preview_.missing.size(); ++i) {
      const MissingCard &entry = preview_.missing.at(i);
      rows_.push_back(
          {RowKind::Flagged, "? " + entry.name + "  x" + std::to_string(entry.quantity), i});
    }
  }
}

sf::FloatRect PreviewList::rowRect(std::size_t row) const {
  const float y = position_.y + (static_cast<float>(row) * rowHeight_);
  return {position_.x, y, size_.x, rowHeight_};
}

std::optional<std::size_t> PreviewList::rowAt(sf::Vector2f point) const {
  if (!contains(point)) {
    return std::nullopt;
  }
  // Which visible row the y-coordinate falls in (integer division truncates).
  const std::size_t visible = static_cast<std::size_t>((point.y - position_.y) / rowHeight_);
  const std::size_t index = scrollOffset_ + visible;
  if (index >= rows_.size()) {
    return std::nullopt; // past the last row (empty tail of the widget)
  }
  return index;
}

sf::FloatRect PreviewList::buttonRect(const sf::FloatRect &row, bool replace) const {
  const float buttonHeight = std::min(26.f * scale_, row.height);
  const float y = row.top + ((row.height - buttonHeight) / 2.f);
  const float removeLeft = row.left + row.width - (kRemoveWidth * scale_) - (kRowPad * scale_);
  const float replaceLeft = removeLeft - (kButtonGap * scale_) - (kReplaceWidth * scale_);
  if (replace) {
    return {replaceLeft, y, kReplaceWidth * scale_, buttonHeight};
  }
  return {removeLeft, y, kRemoveWidth * scale_, buttonHeight};
}

std::optional<std::size_t> PreviewList::buttonAt(sf::Vector2f point, bool replace) const {
  const std::optional<std::size_t> row = rowAt(point);
  if (!row.has_value()) {
    return std::nullopt;
  }
  const Row &entry = rows_.at(row.value());
  if (entry.kind != RowKind::Flagged || !entry.flaggedIndex.has_value()) {
    return std::nullopt; // only flagged rows have buttons
  }
  const std::size_t visible = row.value() - scrollOffset_;
  const sf::FloatRect button = buttonRect(rowRect(visible), replace);
  if (pointInside({button.left, button.top}, {button.width, button.height}, point)) {
    return entry.flaggedIndex;
  }
  return std::nullopt;
}

std::optional<std::size_t> PreviewList::replaceAt(sf::Vector2f point) const {
  return buttonAt(point, true);
}

std::optional<std::size_t> PreviewList::removeAt(sf::Vector2f point) const {
  return buttonAt(point, false);
}

std::size_t PreviewList::visibleRowCount() const {
  return static_cast<std::size_t>(size_.y / rowHeight_);
}

void PreviewList::setScrollOffset(std::size_t offset) {
  scrollOffset_ = std::min(offset, rows_.size());
}

bool PreviewList::mousePressed(sf::Vector2f point) {
  // Replace wins over Remove (the primary action), but the buttons never
  // overlap so the order is just an ordering rule.
  const std::optional<std::size_t> replace = buttonAt(point, true);
  if (replace.has_value()) {
    replaceQueued_ = replace;
    return true;
  }
  const std::optional<std::size_t> remove = buttonAt(point, false);
  if (remove.has_value()) {
    removeQueued_ = remove;
    return true;
  }
  return false;
}

std::optional<std::size_t> PreviewList::consumeReplace() {
  if (replaceQueued_.has_value()) {
    const std::optional<std::size_t> value = replaceQueued_;
    replaceQueued_.reset();
    return value;
  }
  return std::nullopt;
}

std::optional<std::size_t> PreviewList::consumeRemove() {
  if (removeQueued_.has_value()) {
    const std::optional<std::size_t> value = removeQueued_;
    removeQueued_.reset();
    return value;
  }
  return std::nullopt;
}

bool PreviewList::handleEvent(const sf::Event &event) {
  if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    return mousePressed(
        {static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y)});
  }
  return false;
}

void PreviewList::draw(sf::RenderTarget &target, const sf::Font &font,
                       const sf::Font &boldFont) const {
  // The list frame: the window's background color so the rows read as cards
  // sitting in a dark slot inside its parent panel, framed by a gold border.
  sf::RectangleShape panel(size_);
  panel.setPosition(position_);
  panel.setFillColor(menuPalette().background);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(menuPalette().gold);
  target.draw(panel);

  const std::size_t visible = std::min(visibleRowCount(), rows_.size());
  const unsigned headerSize = static_cast<unsigned>(12.f * scale_);
  const unsigned rowSize = static_cast<unsigned>(13.f * scale_);
  const unsigned buttonSize = static_cast<unsigned>(12.f * scale_);
  for (std::size_t row = 0; row < visible; ++row) {
    const std::size_t index = scrollOffset_ + row;
    if (index >= rows_.size()) {
      break;
    }
    const Row &entry = rows_.at(index);
    const sf::FloatRect rect = rowRect(row);

    if (entry.kind == RowKind::Header) {
      // Section labels render muted and letter-spaced, like a table of
      // contents. ASCII only (sf::Text decodes const char* via the C locale).
      sf::Text header(entry.text, font, headerSize);
      header.setLetterSpacing(1.f);
      header.setFillColor(menuPalette().gold);
      header.setPosition({rect.left + (kRowPad * scale_), rect.top + (4.f * scale_)});
      target.draw(header);
      continue;
    }

    // Card rows: the resolved name/qty, or the flagged "? name xN".
    sf::Text rowText(entry.text, boldFont, rowSize);
    rowText.setFillColor(entry.kind == RowKind::Flagged ? menuPalette().danger
                                                        : menuPalette().parchment);
    rowText.setPosition({rect.left + (kRowPad * scale_), rect.top + (4.f * scale_)});
    target.draw(rowText);

    if (entry.kind == RowKind::Flagged) {
      // Replace (primary, parchment) and Remove (danger) buttons on the right.
      const sf::FloatRect replace = buttonRect(rect, true);
      sf::RectangleShape replaceShape({replace.width, replace.height});
      replaceShape.setPosition({replace.left, replace.top});
      replaceShape.setFillColor(menuPalette().parchment);
      replaceShape.setOutlineThickness(1.f);
      replaceShape.setOutlineColor(menuPalette().gold);
      target.draw(replaceShape);
      sf::Text replaceLabel("Replace", font, buttonSize);
      replaceLabel.setFillColor(menuPalette().background);
      const sf::FloatRect replaceBounds = replaceLabel.getLocalBounds();
      replaceLabel.setPosition(
          {replace.left + ((replace.width - replaceBounds.width) / 2.f) - replaceBounds.left,
           replace.top + ((replace.height - replaceBounds.height) / 2.f) - replaceBounds.top});
      target.draw(replaceLabel);

      const sf::FloatRect remove = buttonRect(rect, false);
      sf::RectangleShape removeShape({remove.width, remove.height});
      removeShape.setPosition({remove.left, remove.top});
      removeShape.setFillColor(menuPalette().background);
      removeShape.setOutlineThickness(1.f);
      removeShape.setOutlineColor(menuPalette().danger);
      target.draw(removeShape);
      sf::Text removeLabel("Remove", font, buttonSize);
      removeLabel.setFillColor(menuPalette().danger);
      const sf::FloatRect removeBounds = removeLabel.getLocalBounds();
      removeLabel.setPosition(
          {remove.left + ((remove.width - removeBounds.width) / 2.f) - removeBounds.left,
           remove.top + ((remove.height - removeBounds.height) / 2.f) - removeBounds.top});
      target.draw(removeLabel);
    }
  }
}

} // namespace mtgcpp::core
