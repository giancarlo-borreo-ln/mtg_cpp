// DeckEditorScreen implementation (M5.1/M5.2): build + import views.

#include "ui/screens/deck_editor_screen.h"

#include "core/arena.h"
#include "ui/layout.h"
#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace mtgcpp::core {

namespace {
// Layout constants, authored in design pixels and scaled by uiScale().
constexpr float kPad = 24.f;               // content padding around the panels
constexpr float kGap = 16.f;               // gap between panes and rows
constexpr float kButtonWidth = 88.f;       // toolbar / search-row buttons
constexpr float kRowHeight = 36.f;         // toolbar and search rows
constexpr float kPaneHeader = 24.f;        // height of a pane's section label
constexpr float kListRowHeight = 60.f;     // result / deck list rows
constexpr float kReplaceResultsRows = 3.f; // compact replace search results

// The editor is a two-pane split: search results keep this share of the width
// and the deck being built gets the rest.
constexpr float kResultsShare = 0.45f;

// Trim surrounding whitespace, matching the webapp's `text.trim()` (Arena
// exports usually end with a trailing newline).
std::string trimEdges(std::string_view value) {
  std::string result(value);
  const auto isSpace = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!result.empty() && isSpace(static_cast<unsigned char>(result.front()))) {
    result.erase(result.begin());
  }
  while (!result.empty() && isSpace(static_cast<unsigned char>(result.back()))) {
    result.pop_back();
  }
  return result;
}
} // namespace

DeckEditorScreen::DeckEditorScreen() {
  deckNameInput_.setPlaceholder("Deck name");
  searchInput_.setPlaceholder("Search cards - name or (SET) number");
  searchButton_.setLabel("Search");
  importButton_.setLabel("Import");
  exportButton_.setLabel("Export");
  saveButton_.setLabel("Save");
  backButton_.setLabel("Back");

  importArea_.setPlaceholder("Paste an Arena deck export here...");
  importCancelButton_.setLabel("Cancel");
  previewConfirmButton_.setLabel("Add to deck");
  previewCancelButton_.setLabel("Cancel import");
  replaceInput_.setPlaceholder("Search for the right card...");
  replaceSearchButton_.setLabel("Search");
  replaceDismissButton_.setLabel("Cancel");
}

void DeckEditorScreen::setDeckName(std::string name) { deckNameInput_.setText(std::move(name)); }

CursorKind DeckEditorScreen::cursorAt(sf::Vector2f point) const {
  // Editable text fields get the text caret.
  if (searchInput_.contains(point) || deckNameInput_.contains(point) ||
      importArea_.contains(point) || replaceInput_.contains(point)) {
    return CursorKind::Text;
  }
  // Everything clickable: build-view buttons + lists, import-view controls.
  // Widgets that are not laid out have zero-size bounds, so their contains()
  // is always false — no visibility bookkeeping needed.
  if (searchButton_.contains(point) || backButton_.contains(point) ||
      importButton_.contains(point) || saveButton_.contains(point) ||
      exportButton_.contains(point) || importCancelButton_.contains(point) ||
      previewConfirmButton_.contains(point) || previewCancelButton_.contains(point) ||
      replaceSearchButton_.contains(point) || replaceDismissButton_.contains(point) ||
      resultsList_.contains(point) || deckList_.contains(point) || previewList_.contains(point) ||
      replaceResultsList_.contains(point)) {
    return CursorKind::Hand;
  }
  return CursorKind::Arrow;
}

void DeckEditorScreen::setDeck(std::vector<Card> cards) { deckList_.setCards(std::move(cards)); }

void DeckEditorScreen::setResults(std::vector<Card> cards) {
  resultsList_.setCards(std::move(cards));
}

void DeckEditorScreen::setReplaceResults(std::vector<Card> cards) {
  replaceResults_ = std::move(cards);
  replaceResultsList_.setCards(replaceResults_);
  if (preview_.has_value()) {
    relayout(content_, scale_); // the replace strip grows with its results
  }
}

void DeckEditorScreen::setPreview(std::optional<ImportPreview> preview) {
  preview_ = std::move(preview);
  pendingAction_ = DeckEditorAction::None;
  importError_.clear();
  if (preview_.has_value()) {
    view_ = View::Import;
    previewList_.setPreview(preview_.value());
    const bool clean = canConfirmImport(preview_.value());
    previewConfirmButton_.setEnabled(clean);
    previewConfirmButton_.setLabel(clean ? "Add to deck" : "Resolve flagged cards first");
  } else {
    view_ = View::Build;
  }
  relayout(content_, scale_);
}

void DeckEditorScreen::setImportError(std::string error) { importError_ = std::move(error); }

void DeckEditorScreen::setDatabaseSize(std::size_t size) { databaseSize_ = size; }

DeckTotals DeckEditorScreen::totals() const { return deckTotals(deckList_.cards()); }

void DeckEditorScreen::setAction(DeckEditorAction action) {
  // Keep the first action of a frame so rapid clicks cannot interleave two
  // mutations; the App drains it at most once per poll.
  if (pendingAction_ == DeckEditorAction::None) {
    pendingAction_ = action;
  }
}

void DeckEditorScreen::queueRowAction(DeckEditorAction action, std::size_t row,
                                      int targetQuantity) {
  deckRow_ = row;
  quantity_ = targetQuantity;
  setAction(action);
}

void DeckEditorScreen::relayout(const sf::FloatRect &content, float scale) {
  content_ = content;
  scale_ = scale;
  if (view_ == View::Build) {
    relayoutBuild(content, scale);
  } else {
    relayoutImport(content, scale);
  }
}

void DeckEditorScreen::relayoutBuild(const sf::FloatRect &content, float scale) {
  const sf::FloatRect inner = inset(content, kPad * scale);

  // Toolbar: Back on the left, Save on the right, the deck-name field flexing
  // between them; the card counts render in the gap (drawn, not a widget).
  const sf::FloatRect toolbar{inner.left, inner.top, inner.width, kRowHeight * scale};
  backButton_.setPosition({toolbar.left, toolbar.top});
  backButton_.setSize({kButtonWidth * scale, toolbar.height});
  const float nameWidth = toolbar.width - (2.f * kButtonWidth * scale) - (2.f * kGap * scale);
  deckNameInput_.setPosition({toolbar.left + (kButtonWidth * scale) + (kGap * scale), toolbar.top});
  deckNameInput_.setSize({nameWidth, toolbar.height});
  saveButton_.setPosition({toolbar.left + toolbar.width - (kButtonWidth * scale), toolbar.top});
  saveButton_.setSize({kButtonWidth * scale, toolbar.height});

  // Search row below the toolbar: the input flexes, the buttons are fixed.
  const sf::FloatRect searchRow{toolbar.left, toolbar.top + toolbar.height + (kGap * scale),
                                toolbar.width, kRowHeight * scale};
  const float buttonsWidth = (3.f * kButtonWidth * scale) + (4.f * kGap * scale);
  const float inputWidth = searchRow.width - buttonsWidth;
  searchInput_.setPosition({searchRow.left, searchRow.top});
  searchInput_.setSize({inputWidth, searchRow.height});
  searchButton_.setPosition({searchRow.left + inputWidth + (kGap * scale), searchRow.top});
  searchButton_.setSize({kButtonWidth * scale, searchRow.height});
  importButton_.setPosition(
      {searchRow.left + inputWidth + (2.f * kGap * scale) + (kButtonWidth * scale), searchRow.top});
  importButton_.setSize({kButtonWidth * scale, searchRow.height});
  exportButton_.setPosition(
      {searchRow.left + inputWidth + (3.f * kGap * scale) + (2.f * kButtonWidth * scale),
       searchRow.top});
  exportButton_.setSize({kButtonWidth * scale, searchRow.height});

  // The two panes share the remaining height: results on the left, deck right.
  const float paneTop = searchRow.top + searchRow.height + (kGap * scale);
  const float paneHeight = inner.top + inner.height - paneTop;
  const sf::FloatRect pane{inner.left, paneTop, inner.width, paneHeight};
  const float gap = kGap * scale;
  const float resultsWidth = pane.width * kResultsShare;
  const float deckWidth = pane.width - resultsWidth - gap;
  resultsPane_ = {pane.left, pane.top, resultsWidth, paneHeight};
  deckPane_ = {pane.left + resultsWidth + gap, pane.top, deckWidth, paneHeight};

  // Each list sits under a pane header label.
  const float listTop = pane.top + (kPaneHeader * scale);
  const float listHeight = pane.height - (kPaneHeader * scale);
  resultsList_.setPosition({resultsPane_.left, listTop});
  resultsList_.setSize({resultsPane_.width, listHeight});
  resultsList_.setScale(scale);
  resultsList_.setRowHeight(kListRowHeight * scale);
  deckList_.setPosition({deckPane_.left, listTop});
  deckList_.setSize({deckPane_.width, listHeight});
  deckList_.setScale(scale);
  deckList_.setRowHeight(kListRowHeight * scale);
}

void DeckEditorScreen::relayoutImport(const sf::FloatRect &content, float scale) {
  const sf::FloatRect inner = inset(content, kPad * scale);

  // Toolbar: Back button + the "Import from Arena" title (drawn).
  const sf::FloatRect toolbar{inner.left, inner.top, inner.width, kRowHeight * scale};
  backButton_.setPosition({toolbar.left, toolbar.top});
  backButton_.setSize({kButtonWidth * scale, toolbar.height});

  const float regionTop = toolbar.top + toolbar.height + (kGap * scale);
  const float regionHeight = inner.top + inner.height - regionTop;

  if (!preview_.has_value()) {
    // Paste area: the text area fills the space, the Import/Cancel row sits
    // at the bottom.
    const sf::FloatRect buttons{inner.left, regionTop + regionHeight - (kRowHeight * scale),
                                inner.width, kRowHeight * scale};
    const float half = (buttons.width - (kGap * scale)) / 2.f;
    importButton_.setPosition({buttons.left, buttons.top});
    importButton_.setSize({half, buttons.height});
    importCancelButton_.setPosition({buttons.left + half + (kGap * scale), buttons.top});
    importCancelButton_.setSize({half, buttons.height});

    const float areaHeight = regionHeight - buttons.height - (kGap * scale);
    importArea_.setPosition({inner.left, regionTop});
    importArea_.setSize({inner.width, areaHeight});
    return;
  }

  // Preview panel: footer row at the bottom, the replace strip (when a flagged
  // entry is active) at the top, and the preview list filling the rest.
  panelRect_ = {inner.left, regionTop, inner.width, regionHeight};
  const sf::FloatRect footer{panelRect_.left,
                             panelRect_.top + panelRect_.height - (kRowHeight * scale),
                             panelRect_.width, kRowHeight * scale};
  const float footerHalf = (footer.width - (kGap * scale)) / 2.f;
  previewConfirmButton_.setPosition({footer.left, footer.top});
  previewConfirmButton_.setSize({footerHalf, footer.height});
  previewCancelButton_.setPosition({footer.left + footerHalf + (kGap * scale), footer.top});
  previewCancelButton_.setSize({footerHalf, footer.height});
  footerRect_ = footer;

  float stripHeight = 0.f;
  if (preview_->activeMissing.has_value()) {
    // Replace strip: a "Flagged: name" line + search row, then the compact
    // results list (which is empty until a search runs).
    const float labelHeight = 20.f * scale;
    const float resultsHeight =
        replaceResults_.empty() ? 0.f : (kReplaceResultsRows * kListRowHeight * scale);
    stripHeight =
        labelHeight + (6.f * scale) + (kRowHeight * scale) + (8.f * scale) + resultsHeight;
    replaceStripRect_ = {panelRect_.left, panelRect_.top, panelRect_.width, stripHeight};

    // Label row: the flagged name on the left, the dismiss button on the right.
    const float labelTop = replaceStripRect_.top;
    replaceDismissButton_.setPosition(
        {replaceStripRect_.left + replaceStripRect_.width - (kButtonWidth * scale), labelTop});
    replaceDismissButton_.setSize({kButtonWidth * scale, labelHeight});

    // Search row under the label: the input flexes beside the Search button.
    const float searchTop = labelTop + labelHeight + (6.f * scale);
    const float searchWidth = replaceStripRect_.width - (kButtonWidth * scale) - (kGap * scale);
    replaceInput_.setPosition({replaceStripRect_.left, searchTop});
    replaceInput_.setSize({searchWidth, kRowHeight * scale});
    replaceSearchButton_.setPosition(
        {replaceStripRect_.left + searchWidth + (kGap * scale), searchTop});
    replaceSearchButton_.setSize({kButtonWidth * scale, kRowHeight * scale});

    // Compact results below the search row (empty until a search runs).
    replaceResultsRect_ = {replaceStripRect_.left, searchTop + (kRowHeight * scale) + (8.f * scale),
                           replaceStripRect_.width, resultsHeight};
    replaceResultsList_.setPosition({replaceResultsRect_.left, replaceResultsRect_.top});
    replaceResultsList_.setSize({replaceResultsRect_.width, replaceResultsRect_.height});
    replaceResultsList_.setScale(scale);
    replaceResultsList_.setRowHeight(kListRowHeight * scale);
  } else {
    replaceStripRect_ = {0.f, 0.f, 0.f, 0.f};
  }

  // The preview list takes everything between the strip and the footer.
  const float listTop = panelRect_.top + stripHeight + (stripHeight > 0.f ? (kGap * scale) : 0.f);
  const float listHeight = footer.top - (kGap * scale) - listTop;
  previewList_.setPosition({panelRect_.left, listTop});
  previewList_.setSize({panelRect_.width, listHeight});
  previewList_.setScale(scale);
  previewList_.setRowHeight(44.f * scale);
}

bool DeckEditorScreen::routeEvent(const sf::Event &event) {
  // A left-button press outside any text field unfocuses it, so typing stops
  // going into a box as soon as the user clicks a result or a button.
  if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    const sf::Vector2f point{static_cast<float>(event.mouseButton.x),
                             static_cast<float>(event.mouseButton.y)};
    if (!deckNameInput_.contains(point)) {
      deckNameInput_.setFocused(false);
    }
    if (!searchInput_.contains(point)) {
      searchInput_.setFocused(false);
    }
    if (!replaceInput_.contains(point)) {
      replaceInput_.setFocused(false);
    }
    if (!importArea_.contains(point)) {
      importArea_.setFocused(false);
    }
  }
  // Mouse moves update hover states and are passive (never consumed), so the
  // router still sees them.
  if (event.type == sf::Event::MouseMoved) {
    searchButton_.handleEvent(event);
    importButton_.handleEvent(event);
    exportButton_.handleEvent(event);
    saveButton_.handleEvent(event);
    backButton_.handleEvent(event);
    importCancelButton_.handleEvent(event);
    previewConfirmButton_.handleEvent(event);
    previewCancelButton_.handleEvent(event);
    replaceSearchButton_.handleEvent(event);
    replaceDismissButton_.handleEvent(event);
    return false;
  }

  if (view_ == View::Build) {
    // A focused text field owns the keyboard; let it consume before anything
    // else. Buttons, then the lists.
    if (deckNameInput_.handleEvent(event)) {
      return true;
    }
    if (searchInput_.handleEvent(event)) {
      return true;
    }
    if (searchButton_.handleEvent(event)) {
      return true;
    }
    if (importButton_.handleEvent(event)) {
      return true;
    }
    if (exportButton_.handleEvent(event)) {
      return true;
    }
    if (saveButton_.handleEvent(event)) {
      return true;
    }
    if (backButton_.handleEvent(event)) {
      return true;
    }
    if (deckList_.handleEvent(event)) {
      return true;
    }
    return resultsList_.handleEvent(event);
  }

  if (preview_.has_value()) {
    // Review panel: the replace search field owns the keyboard while active.
    if (replaceInput_.handleEvent(event)) {
      return true;
    }
    if (replaceSearchButton_.handleEvent(event)) {
      return true;
    }
    if (replaceDismissButton_.handleEvent(event)) {
      return true;
    }
    if (previewConfirmButton_.handleEvent(event)) {
      return true;
    }
    if (previewCancelButton_.handleEvent(event)) {
      return true;
    }
    if (previewList_.handleEvent(event)) {
      return true;
    }
    return replaceResultsList_.handleEvent(event);
  }

  // No preview yet: the text area, its buttons, and Back.
  if (importArea_.handleEvent(event)) {
    return true;
  }
  if (importButton_.handleEvent(event)) {
    return true;
  }
  if (importCancelButton_.handleEvent(event)) {
    return true;
  }
  return backButton_.handleEvent(event);
}

DeckEditorAction DeckEditorScreen::pollAction() {
  if (view_ == View::Build) {
    // Back, then search (button click or Enter), then deck-row controls, then
    // adding a search result — one action per frame, first queued wins.
    if (backButton_.consumeClicked()) {
      setAction(DeckEditorAction::Back);
    }
    if (searchInput_.consumeSubmitted()) {
      setAction(DeckEditorAction::Search);
    }
    if (searchButton_.consumeClicked()) {
      setAction(DeckEditorAction::Search);
    }
    if (exportButton_.consumeClicked()) {
      setAction(DeckEditorAction::ExportToClipboard);
    }
    if (saveButton_.consumeClicked()) {
      setAction(DeckEditorAction::SaveDeck);
    }
    // Entering the import view is a pure view change (no App involvement).
    if (importButton_.consumeClicked()) {
      view_ = View::Import;
      relayout(content_, scale_);
    }

    const std::optional<std::size_t> inc = deckList_.consumeIncrement();
    if (inc.has_value() && inc.value() < deckList_.cards().size()) {
      const int target = deckList_.cards().at(inc.value()).quantity + 1;
      queueRowAction(DeckEditorAction::SetQuantity, inc.value(), target);
    }
    const std::optional<std::size_t> dec = deckList_.consumeDecrement();
    if (dec.has_value() && dec.value() < deckList_.cards().size()) {
      const int target = deckList_.cards().at(dec.value()).quantity - 1;
      queueRowAction(DeckEditorAction::SetQuantity, dec.value(), target);
    }
    const std::optional<std::size_t> remove = deckList_.consumeRemove();
    if (remove.has_value()) {
      queueRowAction(DeckEditorAction::RemoveCard, remove.value(), 0);
    }
    const std::optional<std::size_t> add = resultsList_.consumeAdd();
    if (add.has_value() && add.value() < resultsList_.cards().size()) {
      resultIndex_ = add;
      setAction(DeckEditorAction::AddCard);
    }
  } else {
    // Import view: the toolbar Back returns to the build view, or cancels the
    // preview when one is active.
    if (backButton_.consumeClicked()) {
      if (preview_.has_value()) {
        setAction(DeckEditorAction::CancelImport);
      } else {
        view_ = View::Build;
        relayout(content_, scale_);
      }
    }
    if (!preview_.has_value()) {
      // Paste step: Import parses the text, Cancel leaves the view.
      if (importButton_.consumeClicked()) {
        importText_ = trimEdges(importArea_.text());
        if (!importText_.empty()) {
          setAction(DeckEditorAction::ImportText);
        }
      }
      if (importCancelButton_.consumeClicked()) {
        view_ = View::Build;
        relayout(content_, scale_);
      }
    } else {
      // Review step.
      if (previewConfirmButton_.consumeClicked()) {
        setAction(DeckEditorAction::ConfirmImport);
      }
      if (previewCancelButton_.consumeClicked()) {
        setAction(DeckEditorAction::CancelImport);
      }
      if (replaceSearchButton_.consumeClicked() || replaceInput_.consumeSubmitted()) {
        replaceQuery_ = trimEdges(replaceInput_.text());
        if (!replaceQuery_.empty()) {
          setAction(DeckEditorAction::ReplaceSearch);
        }
      }
      if (replaceDismissButton_.consumeClicked()) {
        setAction(DeckEditorAction::DismissPreview);
      }
      const std::optional<std::size_t> select = previewList_.consumeReplace();
      if (select.has_value() && select.value() < previewList_.preview().missing.size()) {
        missingIndex_ = select;
        setAction(DeckEditorAction::SelectMissing);
      }
      const std::optional<std::size_t> drop = previewList_.consumeRemove();
      if (drop.has_value() && drop.value() < previewList_.preview().missing.size()) {
        missingIndex_ = drop;
        setAction(DeckEditorAction::RemoveMissing);
      }
      const std::optional<std::size_t> replaced = replaceResultsList_.consumeAdd();
      if (replaced.has_value() && replaced.value() < replaceResultsList_.cards().size()) {
        resultIndex_ = replaced;
        setAction(DeckEditorAction::ReplaceMissing);
      }
    }
  }

  if (pendingAction_ != DeckEditorAction::None) {
    // Capture the query payloads the App needs before the action drains. A
    // blank query cancels the action (blank searches do nothing).
    if (pendingAction_ == DeckEditorAction::Search) {
      searchQuery_ = trimEdges(searchInput_.text());
      if (searchQuery_.empty()) {
        pendingAction_ = DeckEditorAction::None;
      }
    } else if (pendingAction_ == DeckEditorAction::ReplaceSearch) {
      replaceQuery_ = trimEdges(replaceInput_.text());
      if (replaceQuery_.empty()) {
        pendingAction_ = DeckEditorAction::None;
      }
    }
  }
  if (pendingAction_ != DeckEditorAction::None) {
    const DeckEditorAction action = pendingAction_;
    pendingAction_ = DeckEditorAction::None;
    return action;
  }
  return DeckEditorAction::None;
}

void DeckEditorScreen::drawPaneHeader(sf::RenderTarget &target, const sf::Font &font,
                                      const std::string &label, const sf::FloatRect &pane) const {
  sf::Text header(label, font, static_cast<unsigned>(13.f * scale_));
  header.setLetterSpacing(1.f);
  header.setFillColor(menuPalette().gold);
  header.setPosition({pane.left, pane.top});
  target.draw(header);
}

void DeckEditorScreen::draw(sf::RenderTarget &target, const sf::Font &font,
                            const sf::Font &boldFont) const {
  if (view_ == View::Build) {
    drawBuild(target, font, boldFont);
    return;
  }
  drawImport(target, font, boldFont);
}

void DeckEditorScreen::drawBuild(sf::RenderTarget &target, const sf::Font &font,
                                 const sf::Font &boldFont) const {
  // Toolbar counts: "N cards - M unique", right-aligned just before Save.
  const DeckTotals totals = this->totals();
  const std::string counts = std::to_string(totals.total_cards) + " cards - " +
                             std::to_string(totals.unique_cards) + " unique";
  sf::Text countsText(counts, font, static_cast<unsigned>(14.f * scale_));
  countsText.setFillColor(menuPalette().muted);
  const sf::FloatRect countsBounds = countsText.getLocalBounds();
  const sf::Vector2f savePos = saveButton_.position();
  const sf::Vector2f saveSize = saveButton_.size();
  countsText.setPosition(
      {savePos.x - countsBounds.width - (16.f * scale_),
       savePos.y + ((saveSize.y - countsBounds.height) / 2.f) - countsBounds.top});
  target.draw(countsText);

  backButton_.draw(target, boldFont, scale_);
  deckNameInput_.draw(target, font);
  saveButton_.draw(target, boldFont, scale_);
  searchInput_.draw(target, font);
  searchButton_.draw(target, boldFont, scale_);
  importButton_.draw(target, boldFont, scale_);
  exportButton_.draw(target, boldFont, scale_);

  // The database status hint sits under the search row when the DB is missing.
  if (databaseSize_ == 0) {
    sf::Text warn("Card database not loaded - search finds nothing.", font,
                  static_cast<unsigned>(13.f * scale_));
    warn.setFillColor(menuPalette().danger);
    warn.setPosition({searchInput_.position().x,
                      searchInput_.position().y + searchInput_.size().y + (6.f * scale_)});
    target.draw(warn);
  }

  drawPaneHeader(target, font, "Search results", resultsPane_);
  drawPaneHeader(target, font, "Your deck", deckPane_);

  if (resultsList_.empty()) {
    // The empty search pane reads as an explanatory slot.
    drawEmptyPane(target, font, resultsList_.position(), resultsList_.size(),
                  "No cards yet - search the database to find some.");
  } else {
    resultsList_.draw(target, font, boldFont);
  }

  if (deckList_.empty()) {
    drawEmptyPane(target, font, deckList_.position(), deckList_.size(),
                  "Deck is empty - click a search result to add cards.");
  } else {
    deckList_.draw(target, font, boldFont);
  }
}

void DeckEditorScreen::drawImport(sf::RenderTarget &target, const sf::Font &font,
                                  const sf::Font &boldFont) const {
  // Toolbar: Back + the view title.
  backButton_.draw(target, boldFont, scale_);
  const sf::Vector2f backPos = backButton_.position();
  const sf::Vector2f backSize = backButton_.size();
  sf::Text title("Import from Arena", boldFont, static_cast<unsigned>(15.f * scale_));
  title.setFillColor(menuPalette().gold);
  title.setPosition({backPos.x + backSize.x + (16.f * scale_), backPos.y});
  target.draw(title);

  if (!preview_.has_value()) {
    // Paste step.
    importArea_.draw(target, font);
    importButton_.draw(target, boldFont, scale_);
    importCancelButton_.draw(target, boldFont, scale_);
    if (!importError_.empty()) {
      sf::Text error(importError_, font, static_cast<unsigned>(13.f * scale_));
      error.setFillColor(menuPalette().danger);
      error.setPosition({importArea_.position().x,
                         importArea_.position().y + importArea_.size().y - (18.f * scale_)});
      target.draw(error);
    }
    return;
  }

  // Review panel: a framed slot holding the replace strip (when active), the
  // preview list and the footer.
  sf::RectangleShape panel({panelRect_.width, panelRect_.height});
  panel.setPosition({panelRect_.left, panelRect_.top});
  panel.setFillColor(menuPalette().background);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(menuPalette().gold);
  target.draw(panel);

  if (preview_->activeMissing.has_value()) {
    const MissingCard &active = preview_->activeMissing.value();
    sf::Text flagged("Flagged: " + active.name, font, static_cast<unsigned>(13.f * scale_));
    flagged.setFillColor(menuPalette().danger);
    flagged.setPosition({replaceStripRect_.left, replaceStripRect_.top});
    target.draw(flagged);
    replaceDismissButton_.draw(target, boldFont, scale_);
    replaceInput_.draw(target, font);
    replaceSearchButton_.draw(target, boldFont, scale_);
    if (!replaceResults_.empty()) {
      replaceResultsList_.draw(target, font, boldFont);
    } else {
      drawEmptyPane(target, font, replaceResultsList_.position(), replaceResultsList_.size(),
                    "No results yet - search to find a replacement.");
    }
  }

  const std::size_t resolved = previewList_.preview().cards.size();
  const std::size_t flaggedCount = previewList_.preview().missing.size();
  const std::string counts =
      std::to_string(resolved) + " resolved - " + std::to_string(flaggedCount) + " flagged";
  sf::Text countsText(counts, font, static_cast<unsigned>(13.f * scale_));
  countsText.setFillColor(menuPalette().muted);
  countsText.setPosition({footerRect_.left, footerRect_.top - (20.f * scale_)});
  target.draw(countsText);

  previewConfirmButton_.draw(target, boldFont, scale_);
  previewCancelButton_.draw(target, boldFont, scale_);

  if (previewList_.empty()) {
    drawEmptyPane(target, font, previewList_.position(), previewList_.size(),
                  "No cards in this import.");
  } else {
    previewList_.draw(target, font, boldFont);
  }
}

void DeckEditorScreen::drawEmptyPane(sf::RenderTarget &target, const sf::Font &font,
                                     sf::Vector2f pos, sf::Vector2f size,
                                     const std::string &hint) const {
  sf::RectangleShape panel(size);
  panel.setPosition(pos);
  panel.setFillColor(menuPalette().background);
  panel.setOutlineThickness(2.f);
  panel.setOutlineColor(menuPalette().gold);
  target.draw(panel);
  sf::Text empty(hint, font, static_cast<unsigned>(13.f * scale_));
  empty.setFillColor(menuPalette().muted);
  const sf::FloatRect bounds = empty.getLocalBounds();
  empty.setPosition({pos.x + ((size.x - bounds.width) / 2.f) - bounds.left,
                     pos.y + ((size.y - bounds.height) / 2.f) - bounds.top});
  target.draw(empty);
}

} // namespace mtgcpp::core
