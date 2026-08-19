// The Deck Editor screen (M5.1/M5.2): search the local card database, build a
// deck, and review a pasted Arena import before committing it.
//
// Mirrors the webapp's deck-editor.component.html with the M4.3 responsive
// master-detail pattern. Two views:
//
//   * Build view — a toolbar (Back + live card counts), a search row (text
//     input + Search + Import buttons), and two panes side by side: the search
//     results list on the left, the deck being built on the right. Clicking a
//     result adds it; each deck row carries minus / quantity / plus / remove.
//   * Import view — reached via the Import button: a multi-line text area for
//     pasting an Arena deck export, then (once the App parses it and pushes a
//     preview) a review panel: resolved cards grouped by section, flagged
//     entries with Replace / Remove, a replace search strip for the active
//     flagged entry, and a guarded Confirm + Cancel footer.
//
// Search semantics come from the local CardDatabase (name substrings, plus
// `(SET)` and collector-number matching). The screen owns its widgets but does
// ZERO I/O and no state mutation beyond reporting: the App runs searches,
// parses imports, applies the pure core/import_preview.h operations and the
// pure core/deck_editor.h mutations, then pushes the fresh state back via
// setDeck/setResults/setPreview. Everything except draw() is pure and
// headless-testable; relayout() recomputes every rect from the content band.
#pragma once

#include "core/card.h"
#include "core/deck_editor.h"
#include "core/import_preview.h"
#include "ui/cursor.h"
#include "ui/widgets/button.h"
#include "ui/widgets/card_list.h"
#include "ui/widgets/deck_builder_list.h"
#include "ui/widgets/preview_list.h"
#include "ui/widgets/text_area.h"
#include "ui/widgets/text_input.h"

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

// What the user just did on the Deck Editor. The App reacts by searching the
// card database, parsing an import, applying preview/deck operations or saving
// the deck; the screen never does I/O.
enum class DeckEditorAction {
  None,
  Search,            // build view: searchQuery() holds the query to run
  AddCard,           // build view: resultIndex() holds the search result to add
  SetQuantity,       // build view: deckRow() + quantity() hold the row and its value
  RemoveCard,        // build view: deckRow() holds the deck row to drop
  Back,              // build view: leave the editor for Home
  ImportText,        // import view: importText() holds the pasted Arena text
  CancelImport,      // import view: discard the preview and return to build view
  ConfirmImport,     // import view: commit the preview's resolved cards
  SelectMissing,     // import view: missingIndex() holds the flagged entry chosen
  RemoveMissing,     // import view: missingIndex() holds the flagged entry to drop
  ReplaceMissing,    // import view: resultIndex() holds the replace search result
  ReplaceSearch,     // import view: replaceQuery() holds the replace search text
  DismissPreview,    // import view: clear the active replacement
  ExportToClipboard, // build view: copy the deck as Arena text
  SaveDeck,          // build view: persist the deck
};

class DeckEditorScreen {
public:
  DeckEditorScreen();

  // --- View state -----------------------------------------------------------
  // Replace the deck being built (the App pushes this after every mutation).
  void setDeck(std::vector<Card> cards);
  const std::vector<Card> &deckCards() const { return deckList_.cards(); }

  // Replace the search results (the App pushes these after a search).
  void setResults(std::vector<Card> cards);
  const std::vector<Card> &results() const { return resultsList_.cards(); }

  // Replace the replace-search results (import view's flagged replacement).
  void setReplaceResults(std::vector<Card> cards);
  const std::vector<Card> &replaceResults() const { return replaceResults_; }

  // The active import preview, or nullopt when none. Setting a preview shows
  // the import view's review panel; clearing it returns to the build view.
  void setPreview(std::optional<ImportPreview> preview);
  const std::optional<ImportPreview> &preview() const { return preview_; }

  // Import failure message (e.g. "no valid card lines"), shown under the text
  // area. Empty = no error.
  void setImportError(std::string error);
  const std::string &importError() const { return importError_; }

  // Number of cards in the loaded local database; 0 means "not loaded".
  void setDatabaseSize(std::size_t size);
  std::size_t databaseSize() const { return databaseSize_; }

  // The deck name being edited (the Save payload; "Untitled Deck" when blank).
  void setDeckName(std::string name);
  const std::string &deckName() const { return deckNameInput_.text(); }

  // Derived totals for the deck being built (for the counts label + tests).
  DeckTotals totals() const;

  // Which view is active.
  bool isImportView() const { return view_ == View::Import; }

  // --- Layout ---------------------------------------------------------------
  void relayout(const sf::FloatRect &content, float scale);

  // --- Interaction ----------------------------------------------------------
  // Forward an SFML event to the widgets. Returns true when a widget consumed
  // it (the app then must not route a screen switch).
  bool routeEvent(const sf::Event &event);
  // Drain one completed action (clicks resolve on release), or None. Call once
  // per frame; the app reacts to anything != None.
  DeckEditorAction pollAction();

  void draw(sf::RenderTarget &target, const sf::Font &font, const sf::Font &boldFont) const;

  // The pointer over `point`: Text over the search / deck-name / replace /
  // import text fields, Hand over buttons and list rows.
  CursorKind cursorAt(sf::Vector2f point) const;

  // --- Action payloads (read by the App before the next poll) ---------------
  const std::string &searchQuery() const { return searchQuery_; }
  const std::string &importText() const { return importText_; }
  const std::string &replaceQuery() const { return replaceQuery_; }
  std::optional<std::size_t> resultIndex() const { return resultIndex_; }
  std::optional<std::size_t> deckRow() const { return deckRow_; }
  std::optional<std::size_t> missingIndex() const { return missingIndex_; }
  int quantity() const { return quantity_; }

  // Exposed for tests.
  const TextInput &searchInput() const { return searchInput_; }
  const TextInput &deckNameInput() const { return deckNameInput_; }
  const Button &searchButton() const { return searchButton_; }
  const Button &backButton() const { return backButton_; }
  const Button &importButton() const { return importButton_; }
  const Button &saveButton() const { return saveButton_; }
  const Button &exportButton() const { return exportButton_; }
  const CardList &resultsList() const { return resultsList_; }
  const DeckBuilderList &deckList() const { return deckList_; }
  const TextArea &importArea() const { return importArea_; }
  const PreviewList &previewList() const { return previewList_; }
  const Button &previewConfirmButton() const { return previewConfirmButton_; }
  const Button &previewCancelButton() const { return previewCancelButton_; }
  const Button &replaceSearchButton() const { return replaceSearchButton_; }
  const Button &replaceDismissButton() const { return replaceDismissButton_; }
  const TextInput &replaceInput() const { return replaceInput_; }
  const CardList &replaceResultsList() const { return replaceResultsList_; }

private:
  enum class View { Build, Import };

  void setAction(DeckEditorAction action);
  // Queue a deck-row action (SetQuantity / RemoveCard) with its payload.
  void queueRowAction(DeckEditorAction action, std::size_t row, int targetQuantity);
  void relayoutBuild(const sf::FloatRect &content, float scale);
  void relayoutImport(const sf::FloatRect &content, float scale);
  void drawPaneHeader(sf::RenderTarget &target, const sf::Font &font, const std::string &label,
                      const sf::FloatRect &pane) const;
  void drawBuild(sf::RenderTarget &target, const sf::Font &font, const sf::Font &boldFont) const;
  void drawImport(sf::RenderTarget &target, const sf::Font &font, const sf::Font &boldFont) const;
  // An explanatory "empty" slot at `pos`/`size` (used by every list's empty
  // state so the three list widgets share one drawing path).
  void drawEmptyPane(sf::RenderTarget &target, const sf::Font &font, sf::Vector2f pos,
                     sf::Vector2f size, const std::string &hint) const;

  View view_ = View::Build;
  DeckEditorAction pendingAction_ = DeckEditorAction::None;
  std::string searchQuery_;
  std::string importText_;
  std::string replaceQuery_;
  std::string importError_;
  std::optional<std::size_t> resultIndex_;
  std::optional<std::size_t> deckRow_;
  std::optional<std::size_t> missingIndex_;
  int quantity_ = 0;
  std::size_t databaseSize_ = 0;
  float scale_ = 1.f;
  sf::FloatRect content_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect resultsPane_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect deckPane_{0.f, 0.f, 0.f, 0.f};
  std::optional<ImportPreview> preview_;
  std::vector<Card> replaceResults_;
  sf::FloatRect panelRect_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect replaceStripRect_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect replaceResultsRect_{0.f, 0.f, 0.f, 0.f};
  sf::FloatRect footerRect_{0.f, 0.f, 0.f, 0.f};

  // Build view.
  TextInput searchInput_;
  TextInput deckNameInput_;
  Button searchButton_;
  Button backButton_;
  Button importButton_;
  Button saveButton_;
  Button exportButton_;
  CardList resultsList_;
  DeckBuilderList deckList_;

  // Import view.
  TextArea importArea_;
  Button importCancelButton_;
  PreviewList previewList_;
  Button previewConfirmButton_;
  Button previewCancelButton_;
  TextInput replaceInput_;
  Button replaceSearchButton_;
  Button replaceDismissButton_;
  CardList replaceResultsList_;
};

} // namespace mtgcpp::core
