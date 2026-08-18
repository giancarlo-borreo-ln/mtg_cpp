// M5.1 DeckEditorScreen tests: search -> results -> add -> quantity/remove
// flow, the empty states, and the responsive two-pane layout. Everything is
// headless: the screen's widgets are pure math over rects plus the sf::Event
// adapters, so synthesized clicks and text events drive the full logic.

#include "core/card.h"
#include "core/import_preview.h"
#include "ui/layout.h"
#include "ui/screens/deck_editor_screen.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

namespace mtgcpp::core {
namespace {

// The chrome margin app.cpp uses for the content band (mirrored here so the
// test drives the same content rect the real App hands to the screen).
constexpr float kMargin = 40.f;

sf::Event mousePress(float x, float y) {
  sf::Event event{};
  event.type = sf::Event::MouseButtonPressed;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

sf::Event mouseRelease(float x, float y) {
  sf::Event event{};
  event.type = sf::Event::MouseButtonReleased;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

sf::Event textChar(char c) {
  sf::Event event{};
  event.type = sf::Event::TextEntered;
  event.text.unicode = static_cast<sf::Uint32>(static_cast<unsigned char>(c));
  return event;
}

sf::Event keyPress(sf::Keyboard::Key key) {
  sf::Event event{};
  event.type = sf::Event::KeyPressed;
  event.key.code = key;
  return event;
}

// Complete a click on a widget: press then release inside it, then drain the
// resulting action.
DeckEditorAction click(DeckEditorScreen &editor, sf::Vector2f point) {
  editor.routeEvent(mousePress(point.x, point.y));
  editor.routeEvent(mouseRelease(point.x, point.y));
  return editor.pollAction();
}

sf::Vector2f center(const sf::FloatRect &rect) {
  return {rect.left + (rect.width / 2.f), rect.top + (rect.height / 2.f)};
}

// The content band App hands to the screen for a window of `width` x 600.
sf::FloatRect contentFor(unsigned width) {
  const float margin = px(kMargin, {width, 600u});
  return {margin, px(196.f, {width, 600u}), static_cast<float>(width) - (2.f * margin),
          (600.f - px(56.f, {width, 600u}) - px(16.f, {width, 600u})) - px(196.f, {width, 600u})};
}

Card searchCard(std::string name, const std::string &set, const std::string &number) {
  Card c;
  c.name = std::move(name);
  c.set_code = set;
  c.set_name = "Test Set";
  c.collector_number = number;
  c.type_line = "Instant";
  return c;
}

Card deckCard(std::string name, int quantity) {
  Card c = searchCard(std::move(name), "sta", "1");
  c.quantity = quantity;
  return c;
}

// Focus the search field and type `text` into it via the event adapter.
void typeInto(DeckEditorScreen &editor, const std::string &text) {
  const sf::FloatRect input = editor.searchInput().bounds();
  editor.routeEvent(mousePress(center(input).x, center(input).y));
  for (const char c : text) {
    editor.routeEvent(textChar(c));
  }
}

// The control rect for `which` on the first visible deck row, mirroring the
// DeckBuilderList cluster math (right-aligned inside the row).
sf::FloatRect controlRectFor(sf::Vector2f pos, sf::Vector2f size, float rowHeight,
                             const std::string &which) {
  const float buttonHeight = 30.f;
  const float mid = pos.y + (rowHeight / 2.f);
  const float removeLeft = pos.x + size.x - 40.f - 10.f;
  const float plusLeft = removeLeft - 6.f - 36.f;
  const float minusLeft = plusLeft - 6.f - 36.f - 6.f - 36.f;
  if (which == "remove") {
    return {removeLeft, mid - (buttonHeight / 2.f), 40.f, buttonHeight};
  }
  if (which == "plus") {
    return {plusLeft, mid - (buttonHeight / 2.f), 36.f, buttonHeight};
  }
  return {minusLeft, mid - (buttonHeight / 2.f), 36.f, buttonHeight};
}

TEST(DeckEditorScreen, StartsEmptyWithZeroTotals) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);

  EXPECT_TRUE(editor.deckCards().empty());
  EXPECT_TRUE(editor.results().empty());
  const DeckTotals totals = editor.totals();
  EXPECT_EQ(totals.total_cards, 0);
  EXPECT_EQ(totals.unique_cards, 0);
  EXPECT_EQ(editor.pollAction(), DeckEditorAction::None);
}

TEST(DeckEditorScreen, LayoutPositionsBothPanesAndControls) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);

  // Toolbar + search row + two non-overlapping panes, all hittable.
  EXPECT_GT(editor.backButton().bounds().width, 0.f);
  EXPECT_GT(editor.searchButton().bounds().width, 0.f);
  EXPECT_GT(editor.searchInput().bounds().width, 0.f);
  EXPECT_GT(editor.resultsList().bounds().width, 0.f);
  EXPECT_GT(editor.deckList().bounds().width, 0.f);
  const sf::FloatRect results = editor.resultsList().bounds();
  const sf::FloatRect deck = editor.deckList().bounds();
  EXPECT_FALSE(results.intersects(deck));
  // The results pane sits to the left of the deck pane.
  EXPECT_LT(results.left + results.width, deck.left);
}

TEST(DeckEditorScreen, SubmittingASearchReportsTheTrimmedQuery) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);

  typeInto(editor, "  bolt  ");
  editor.routeEvent(keyPress(sf::Keyboard::Enter)); // Enter submits while focused
  EXPECT_EQ(editor.pollAction(), DeckEditorAction::Search);
  EXPECT_EQ(editor.searchQuery(), "bolt");
}

TEST(DeckEditorScreen, ClickingTheSearchButtonReportsTheQuery) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);

  typeInto(editor, "Ghalta");
  EXPECT_EQ(click(editor, center(editor.searchButton().bounds())), DeckEditorAction::Search);
  EXPECT_EQ(editor.searchQuery(), "Ghalta");
}

TEST(DeckEditorScreen, BlankSearchIsDropped) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);

  typeInto(editor, "   ");
  editor.routeEvent(keyPress(sf::Keyboard::Enter));
  EXPECT_EQ(editor.pollAction(), DeckEditorAction::None);
}

TEST(DeckEditorScreen, ClickingAResultQueuesAnAdd) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  editor.setResults(
      {searchCard("Lightning Bolt", "sta", "49"), searchCard("Bolt Hound", "m20", "136")});

  // Click the second result row (left edge of the results list).
  const sf::Vector2f list = editor.resultsList().position();
  const float rowHeight = editor.resultsList().rowHeight();
  EXPECT_EQ(click(editor, {list.x + 10.f, list.y + (1.5f * rowHeight)}), DeckEditorAction::AddCard);
  EXPECT_EQ(editor.resultIndex(), std::make_optional<std::size_t>(1));
}

TEST(DeckEditorScreen, ClickingAResultUnfocusesTheSearchField) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  editor.setResults({searchCard("Lightning Bolt", "sta", "49")});

  typeInto(editor, "bolt");
  EXPECT_TRUE(editor.searchInput().isFocused());

  const sf::Vector2f list = editor.resultsList().position();
  click(editor, {list.x + 10.f, list.y + (0.5f * editor.resultsList().rowHeight())});
  EXPECT_FALSE(editor.searchInput().isFocused());
}

TEST(DeckEditorScreen, PlusButtonQueuesASetQuantityIncrement) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  editor.setDeck({deckCard("Forest", 4)});

  const sf::FloatRect plus = controlRectFor(editor.deckList().position(), editor.deckList().size(),
                                            editor.deckList().rowHeight(), "plus");
  EXPECT_EQ(click(editor, center(plus)), DeckEditorAction::SetQuantity);
  EXPECT_EQ(editor.deckRow(), std::make_optional<std::size_t>(0));
  EXPECT_EQ(editor.quantity(), 5);
}

TEST(DeckEditorScreen, MinusButtonQueuesASetQuantityDecrement) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  editor.setDeck({deckCard("Forest", 4)});

  const sf::FloatRect minus = controlRectFor(editor.deckList().position(), editor.deckList().size(),
                                             editor.deckList().rowHeight(), "minus");
  EXPECT_EQ(click(editor, center(minus)), DeckEditorAction::SetQuantity);
  EXPECT_EQ(editor.deckRow(), std::make_optional<std::size_t>(0));
  EXPECT_EQ(editor.quantity(), 3);
}

TEST(DeckEditorScreen, MinusDownToZeroStillQueuesASetQuantity) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  editor.setDeck({deckCard("Forest", 1)});

  const sf::FloatRect minus = controlRectFor(editor.deckList().position(), editor.deckList().size(),
                                             editor.deckList().rowHeight(), "minus");
  // 1 - 1 = 0: the App turns a zero quantity into a row removal.
  EXPECT_EQ(click(editor, center(minus)), DeckEditorAction::SetQuantity);
  EXPECT_EQ(editor.quantity(), 0);
}

TEST(DeckEditorScreen, RemoveButtonQueuesARemoval) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  editor.setDeck({deckCard("Forest", 2)});

  const sf::FloatRect remove =
      controlRectFor(editor.deckList().position(), editor.deckList().size(),
                     editor.deckList().rowHeight(), "remove");
  EXPECT_EQ(click(editor, center(remove)), DeckEditorAction::RemoveCard);
  EXPECT_EQ(editor.deckRow(), std::make_optional<std::size_t>(0));
}

TEST(DeckEditorScreen, BackButtonReportsBack) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  EXPECT_EQ(click(editor, center(editor.backButton().bounds())), DeckEditorAction::Back);
}

TEST(DeckEditorScreen, TotalsReflectThePushedDeck) {
  DeckEditorScreen editor;
  editor.setDeck({deckCard("Forest", 4), deckCard("Bolt", 2)});

  const DeckTotals totals = editor.totals();
  EXPECT_EQ(totals.total_cards, 6);
  EXPECT_EQ(totals.unique_cards, 2);
}

TEST(DeckEditorScreen, DatabaseSizeIsReportedAndStored) {
  DeckEditorScreen editor;
  EXPECT_EQ(editor.databaseSize(), 0u); // 0 = not loaded, shown as a hint
  editor.setDatabaseSize(116712u);
  EXPECT_EQ(editor.databaseSize(), 116712u);
}

TEST(DeckEditorScreen, PaneReflowsAcrossWindowWidths) {
  for (const unsigned width : {640u, 960u, 1280u, 1920u}) {
    DeckEditorScreen editor;
    editor.relayout(contentFor(width), 1.f);

    // Both lists stay hittable and non-overlapping at every width.
    const sf::FloatRect results = editor.resultsList().bounds();
    const sf::FloatRect deck = editor.deckList().bounds();
    EXPECT_GT(results.width, 0.f) << "width " << width;
    EXPECT_GT(deck.width, 0.f) << "width " << width;
    EXPECT_FALSE(results.intersects(deck)) << "width " << width;

    // And the first result stays clickable at every width.
    editor.setResults({searchCard("Lightning Bolt", "sta", "49")});
    EXPECT_EQ(click(editor, {results.left + 10.f, results.top + 10.f}), DeckEditorAction::AddCard)
        << "width " << width;
  }
}

// ---------------------------------------------------------------------------
// M5.2 import-preview flow
// ---------------------------------------------------------------------------

// Type `text` into the import text area (Enter becomes a newline).
void typeIntoArea(DeckEditorScreen &editor, const std::string &text) {
  const sf::FloatRect area = editor.importArea().bounds();
  editor.routeEvent(mousePress(center(area).x, center(area).y));
  for (const char c : text) {
    if (c == '\n') {
      editor.routeEvent(keyPress(sf::Keyboard::Enter));
    } else {
      editor.routeEvent(textChar(c));
    }
  }
}

// A preview with one resolved card and one flagged entry.
ImportPreview samplePreview() {
  ImportPreview preview;
  preview.cards = {searchCard("Forest", "war", "263")};
  preview.cards.at(0).quantity = 4;
  preview.missing = {MissingCard{"Mystery A", "ZZZ", "1", 2, ArenaSection::Mainboard}};
  return preview;
}

TEST(DeckEditorScreen, ImportButtonOpensTheImportView) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  EXPECT_FALSE(editor.isImportView());

  EXPECT_EQ(click(editor, center(editor.importButton().bounds())), DeckEditorAction::None);
  EXPECT_TRUE(editor.isImportView());
  EXPECT_GT(editor.importArea().bounds().height, 0.f);
}

TEST(DeckEditorScreen, BackFromAnEmptyImportViewReturnsToBuild) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  click(editor, center(editor.importButton().bounds()));
  EXPECT_TRUE(editor.isImportView());

  EXPECT_EQ(click(editor, center(editor.backButton().bounds())), DeckEditorAction::None);
  EXPECT_FALSE(editor.isImportView());
}

TEST(DeckEditorScreen, ImportingPastedTextReportsTheTrimmedPayload) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  click(editor, center(editor.importButton().bounds()));

  typeIntoArea(editor, "Deck\n4 Forest (WAR) 263\n2 Bolt (STA) 49\n");
  EXPECT_EQ(click(editor, center(editor.importButton().bounds())), DeckEditorAction::ImportText);
  EXPECT_EQ(editor.importText(), "Deck\n4 Forest (WAR) 263\n2 Bolt (STA) 49");
}

TEST(DeckEditorScreen, ImportingBlankTextIsDropped) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  click(editor, center(editor.importButton().bounds()));

  typeIntoArea(editor, "   \n  ");
  EXPECT_EQ(click(editor, center(editor.importButton().bounds())), DeckEditorAction::None);
}

TEST(DeckEditorScreen, SettingAPreviewShowsTheReviewPanel) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);

  editor.setPreview(samplePreview());
  EXPECT_TRUE(editor.isImportView());
  EXPECT_TRUE(editor.preview().has_value());
  // Flagged cards remain, so confirm is disabled.
  EXPECT_FALSE(editor.previewConfirmButton().isEnabled());
  EXPECT_EQ(editor.previewConfirmButton().label(), "Resolve flagged cards first");
}

TEST(DeckEditorScreen, ACleanPreviewEnablesConfirm) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  ImportPreview preview = samplePreview();
  preview.missing.clear();

  editor.setPreview(preview);
  EXPECT_TRUE(editor.previewConfirmButton().isEnabled());
  EXPECT_EQ(editor.previewConfirmButton().label(), "Add to deck");
}

TEST(DeckEditorScreen, ConfirmingAReviewReportsConfirmImport) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  ImportPreview preview = samplePreview();
  preview.missing.clear();
  editor.setPreview(preview);

  EXPECT_EQ(click(editor, center(editor.previewConfirmButton().bounds())),
            DeckEditorAction::ConfirmImport);
}

TEST(DeckEditorScreen, CancellingTheReviewReportsCancelImport) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  editor.setPreview(samplePreview());

  EXPECT_EQ(click(editor, center(editor.previewCancelButton().bounds())),
            DeckEditorAction::CancelImport);
}

TEST(DeckEditorScreen, ClickingReplaceOnAFlaggedEntryReportsSelectMissing) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  editor.setPreview(samplePreview());

  // Rows: 0 "Deck", 1 Forest, 2 "Flagged", 3 Mystery A (flagged, index 0).
  const sf::Vector2f pos = editor.previewList().position();
  const float rowHeight = editor.previewList().rowHeight();
  const float rowTop = pos.y + (3.5f * rowHeight);
  // Replace button: right-aligned cluster (76 wide, 6 gap, 64 remove, 10 pad).
  const float replaceLeft = pos.x + editor.previewList().size().x - 10.f - 64.f - 6.f - 76.f;
  EXPECT_EQ(click(editor, {replaceLeft + 38.f, rowTop}), DeckEditorAction::SelectMissing);
  EXPECT_EQ(editor.missingIndex(), std::make_optional<std::size_t>(0));
}

TEST(DeckEditorScreen, ClickingRemoveOnAFlaggedEntryReportsRemoveMissing) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  editor.setPreview(samplePreview());

  const sf::Vector2f pos = editor.previewList().position();
  const float rowHeight = editor.previewList().rowHeight();
  const float rowTop = pos.y + (3.5f * rowHeight);
  const float removeLeft = pos.x + editor.previewList().size().x - 10.f - 64.f;
  EXPECT_EQ(click(editor, {removeLeft + 32.f, rowTop}), DeckEditorAction::RemoveMissing);
  EXPECT_EQ(editor.missingIndex(), std::make_optional<std::size_t>(0));
}

TEST(DeckEditorScreen, SelectingAFlaggedEntryShowsTheReplaceStrip) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  ImportPreview preview = samplePreview();
  preview.activeMissing = preview.missing.at(0);
  editor.setPreview(preview);

  // The replace strip (search input + buttons) is laid out and hittable.
  EXPECT_GT(editor.replaceInput().bounds().width, 0.f);
  EXPECT_GT(editor.replaceSearchButton().bounds().width, 0.f);
  EXPECT_GT(editor.replaceDismissButton().bounds().width, 0.f);
}

TEST(DeckEditorScreen, ReplaceSearchReportsTheQuery) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  ImportPreview preview = samplePreview();
  preview.activeMissing = preview.missing.at(0);
  editor.setPreview(preview);

  // Type into the replace search and submit it.
  const sf::FloatRect input = editor.replaceInput().bounds();
  editor.routeEvent(mousePress(center(input).x, center(input).y));
  for (const char c : std::string("bolt")) {
    editor.routeEvent(textChar(c));
  }
  editor.routeEvent(keyPress(sf::Keyboard::Enter));

  EXPECT_EQ(editor.pollAction(), DeckEditorAction::ReplaceSearch);
  EXPECT_EQ(editor.replaceQuery(), "bolt");
}

TEST(DeckEditorScreen, DismissingTheReplaceStripReportsDismissPreview) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  ImportPreview preview = samplePreview();
  preview.activeMissing = preview.missing.at(0);
  editor.setPreview(preview);

  EXPECT_EQ(click(editor, center(editor.replaceDismissButton().bounds())),
            DeckEditorAction::DismissPreview);
}

TEST(DeckEditorScreen, ClickingAResultInTheReplacePanelReportsReplaceMissing) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  ImportPreview preview = samplePreview();
  preview.activeMissing = preview.missing.at(0);
  editor.setPreview(preview);

  editor.setReplaceResults({searchCard("Lightning Bolt", "sta", "49")});
  const sf::Vector2f pos = editor.replaceResultsList().position();
  EXPECT_EQ(click(editor, {pos.x + 10.f, pos.y + 10.f}), DeckEditorAction::ReplaceMissing);
  EXPECT_EQ(editor.resultIndex(), std::make_optional<std::size_t>(0));
}

TEST(DeckEditorScreen, ClearingThePreviewReturnsToTheBuildView) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  editor.setPreview(samplePreview());
  EXPECT_TRUE(editor.isImportView());

  editor.setPreview(std::nullopt);
  EXPECT_FALSE(editor.isImportView());
  EXPECT_GT(editor.searchInput().bounds().width, 0.f);
}

// ---------------------------------------------------------------------------
// M5.3 export + persistence UI
// ---------------------------------------------------------------------------

TEST(DeckEditorScreen, DeckNameCanBeSetAndRead) {
  DeckEditorScreen editor;
  EXPECT_TRUE(editor.deckName().empty());
  editor.setDeckName("Ghalta Stompy");
  EXPECT_EQ(editor.deckName(), "Ghalta Stompy");
}

TEST(DeckEditorScreen, DeckNameFitsTheToolbar) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);
  EXPECT_GT(editor.deckNameInput().bounds().width, 0.f);
  EXPECT_GT(editor.saveButton().bounds().width, 0.f);
}

TEST(DeckEditorScreen, SaveAndExportButtonsReportTheirActions) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);

  EXPECT_EQ(click(editor, center(editor.exportButton().bounds())),
            DeckEditorAction::ExportToClipboard);
  EXPECT_EQ(click(editor, center(editor.saveButton().bounds())), DeckEditorAction::SaveDeck);
}

TEST(DeckEditorScreen, TypingIntoTheDeckNameFieldIsEditable) {
  DeckEditorScreen editor;
  editor.relayout(contentFor(960u), 1.f);

  const sf::FloatRect input = editor.deckNameInput().bounds();
  editor.routeEvent(mousePress(center(input).x, center(input).y));
  for (const char c : std::string("Ghalta")) {
    editor.routeEvent(textChar(c));
  }
  EXPECT_EQ(editor.deckName(), "Ghalta");
}

} // namespace
} // namespace mtgcpp::core
