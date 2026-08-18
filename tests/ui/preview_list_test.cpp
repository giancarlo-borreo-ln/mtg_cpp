// M5.2 PreviewList widget tests: the flattened preview rows (section headers +
// resolved cards + flagged entries), Replace/Remove hit-testing, queued-action
// delivery and scrolling. Pure rect math, no display needed.

#include "core/card.h"
#include "core/import_preview.h"
#include "ui/widgets/preview_list.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

namespace mtgcpp::core {
namespace {

Card resolvedCard(std::string name, int quantity, ArenaSection section) {
  Card c;
  c.name = std::move(name);
  c.set_code = "war";
  c.collector_number = "1";
  c.quantity = quantity;
  c.section = section;
  return c;
}

MissingCard flagged(std::string name, ArenaSection section) {
  MissingCard m;
  m.name = std::move(name);
  m.set = "ZZZ";
  m.number = "1";
  m.quantity = 2;
  m.section = section;
  return m;
}

ImportPreview fixture() {
  ImportPreview preview;
  preview.cards = {resolvedCard("Forest", 4, ArenaSection::Mainboard),
                   resolvedCard("Island", 2, ArenaSection::Sideboard)};
  preview.missing = {flagged("Mystery A", ArenaSection::Mainboard),
                     flagged("Mystery B", ArenaSection::Sideboard)};
  return preview;
}

// The Replace/Remove button rect on the first flagged row, mirroring the
// widget's private math (right-aligned cluster inside the row).
sf::FloatRect buttonRectFor(sf::Vector2f pos, sf::Vector2f size, float rowHeight, bool replace) {
  const float buttonHeight = 26.f;
  const float mid = pos.y + (rowHeight / 2.f);
  const float removeLeft = pos.x + size.x - 64.f - 10.f;
  const float replaceLeft = removeLeft - 6.f - 76.f;
  if (replace) {
    return {replaceLeft, mid - (buttonHeight / 2.f), 76.f, buttonHeight};
  }
  return {removeLeft, mid - (buttonHeight / 2.f), 64.f, buttonHeight};
}

TEST(PreviewList, RebuildsRowsFromThePreview) {
  PreviewList list;
  list.setPreview(fixture());

  // Header Deck + Forest, header Sideboard + Island, header Flagged + two
  // flagged rows.
  EXPECT_EQ(list.rowCount(), 7u);
  EXPECT_FALSE(list.empty());
}

TEST(PreviewList, RowAtMapsPointsToRows) {
  PreviewList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 132.f});
  list.setRowHeight(44.f);
  list.setPreview(fixture());

  EXPECT_EQ(list.rowAt({5.f, 2.f}), std::make_optional<std::size_t>(0));   // Deck header
  EXPECT_EQ(list.rowAt({5.f, 44.f}), std::make_optional<std::size_t>(1));  // Forest
  EXPECT_EQ(list.rowAt({5.f, 130.f}), std::make_optional<std::size_t>(2)); // Sideboard header
  EXPECT_FALSE(list.rowAt({5.f, 133.f}).has_value());                      // outside
}

TEST(PreviewList, ReplaceAndRemoveHitOnlyFlaggedRows) {
  PreviewList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 300.f});
  list.setRowHeight(44.f);
  list.setPreview(fixture());

  // Rows: 0 Deck, 1 Forest, 2 Sideboard, 3 Island, 4 Flagged, 5 Mystery A,
  // 6 Mystery B. Flagged rows start at index 5 (first flagged entry = missing 0).
  const sf::FloatRect replace = buttonRectFor(list.position(), list.size(), list.rowHeight(), true);
  const sf::FloatRect remove = buttonRectFor(list.position(), list.size(), list.rowHeight(), false);

  // Row 5 (Mystery A, flagged index 0): both buttons hit it.
  const sf::Vector2f replaceCenter{replace.left + (replace.width / 2.f),
                                   list.position().y + (5.5f * list.rowHeight())};
  const sf::Vector2f removeCenter{remove.left + (remove.width / 2.f),
                                  list.position().y + (5.5f * list.rowHeight())};
  EXPECT_EQ(list.replaceAt(replaceCenter), std::make_optional<std::size_t>(0));
  EXPECT_EQ(list.removeAt(removeCenter), std::make_optional<std::size_t>(0));

  // A resolved card row (row 1, Forest) has no buttons.
  EXPECT_FALSE(list.replaceAt({200.f, list.position().y + (1.5f * list.rowHeight())}).has_value());
  EXPECT_FALSE(list.removeAt({200.f, list.position().y + (1.5f * list.rowHeight())}).has_value());
}

TEST(PreviewList, ClickingReplaceQueuesTheFlaggedEntry) {
  PreviewList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 300.f});
  list.setRowHeight(44.f);
  list.setPreview(fixture());

  const sf::FloatRect replace = buttonRectFor(list.position(), list.size(), list.rowHeight(), true);
  const sf::Vector2f point{replace.left + (replace.width / 2.f),
                           list.position().y + (6.5f * list.rowHeight())}; // Mystery B
  EXPECT_TRUE(list.mousePressed(point));
  EXPECT_EQ(list.consumeReplace(), std::make_optional<std::size_t>(1));
  EXPECT_FALSE(list.consumeReplace().has_value()); // exactly once
  EXPECT_FALSE(list.consumeRemove().has_value());
}

TEST(PreviewList, ClickingRemoveQueuesTheFlaggedEntry) {
  PreviewList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 300.f});
  list.setRowHeight(44.f);
  list.setPreview(fixture());

  const sf::FloatRect remove = buttonRectFor(list.position(), list.size(), list.rowHeight(), false);
  const sf::Vector2f point{remove.left + (remove.width / 2.f),
                           list.position().y + (5.5f * list.rowHeight())}; // Mystery A
  EXPECT_TRUE(list.mousePressed(point));
  EXPECT_EQ(list.consumeRemove(), std::make_optional<std::size_t>(0));
}

TEST(PreviewList, ClickingAResolvedRowQueuesNothing) {
  PreviewList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 300.f});
  list.setRowHeight(44.f);
  list.setPreview(fixture());

  EXPECT_FALSE(list.mousePressed({200.f, list.position().y + (1.5f * list.rowHeight())}));
  EXPECT_FALSE(list.consumeReplace().has_value());
  EXPECT_FALSE(list.consumeRemove().has_value());
}

TEST(PreviewList, ScrollOffsetClampsToTheRowCount) {
  PreviewList list;
  list.setPreview(fixture());
  list.setScrollOffset(99);
  EXPECT_EQ(list.scrollOffset(), 7u);
}

TEST(PreviewList, SettingANewPreviewResetsScrollAndPendingActions) {
  PreviewList list;
  list.setPosition({0.f, 0.f});
  list.setSize({400.f, 300.f});
  list.setRowHeight(44.f);
  list.setPreview(fixture());
  list.setScrollOffset(2);
  list.mousePressed({5.f, 5.f});

  list.setPreview(fixture());
  EXPECT_EQ(list.scrollOffset(), 0u);
  EXPECT_FALSE(list.consumeReplace().has_value());
  EXPECT_FALSE(list.consumeRemove().has_value());
}

TEST(PreviewList, EmptyPreviewHasNoRows) {
  PreviewList list;
  list.setPreview(ImportPreview{});
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.rowCount(), 0u);
  EXPECT_FALSE(list.replaceAt({5.f, 5.f}).has_value());
  EXPECT_FALSE(list.removeAt({5.f, 5.f}).has_value());
}

} // namespace
} // namespace mtgcpp::core
