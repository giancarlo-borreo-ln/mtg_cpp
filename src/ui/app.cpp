// App shell implementation (M4.1/M4.3/M5.1): window loop, routing, Home + the
// Deck Editor.
//
// Layout: anchored to the window size and re-flowed by a single relayout() pass
// (see ui/layout.h) — top bar with the wordmark + version, a centered screen
// heading + description, the screen body, and a bottom hint bar. Gold rules
// frame the content area. All strings are ASCII-only.

#include "ui/app.h"

#include "core/arena.h"
#include "core/card.h"
#include "core/deck_parser.h"
#include "core/import_preview.h"
#include "core/importer.h"
#include "net/address.h"
#include "store/profile_store.h"
#include "ui/layout.h"
#include "ui/theme.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace mtgcpp::core {

namespace {

// The default window size before any resize is the App member windowSize_'s
// initial value (960x600, matching the original window).

// The port the host's embedded relay binds; peers join `hostIp:kLobbyPort`.
constexpr std::uint16_t kLobbyPort = 7500;

// Chrome layout, authored in design pixels (scaled by uiScale()).
constexpr float kTopBarHeight = 64.f;
constexpr float kBottomBarHeight = 56.f;
constexpr float kMargin = 40.f; // side margins for the content band
constexpr float kHeadingY = 106.f;
constexpr float kPlaceholderY = 160.f;
constexpr float kContentTop = 196.f;
constexpr float kContentBottomPad = 16.f;

// Uppercase a label for the display heading (all-ASCII input).
std::string toUpper(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return result;
}

// Horizontally center a text on the window at height `y`, accounting for the
// measured box's (usually non-zero) origin.
sf::Vector2f centeredAt(const sf::Text &text, float windowWidth, float y) {
  const sf::FloatRect bounds = text.getLocalBounds();
  return {((windowWidth - bounds.width) / 2.f) - bounds.left, y};
}

} // namespace

const char *screenName(Screen screen) {
  // Array lookup (not a switch) so an out-of-range value cannot fall through;
  // kNames is static constexpr storage, safe to return a pointer into.
  static constexpr std::array kNames{"Home", "Deck Editor", "Lobby", "Table"};
  const std::size_t index = static_cast<std::size_t>(screen);
  if (index < kNames.size()) {
    return kNames.at(index);
  }
  return "Unknown";
}

const char *screenPlaceholder(Screen screen) {
  // One-line, user-facing description per screen. ASCII only (see header note
  // about sf::String decoding through the C locale). Home's real text is
  // view-dependent and set by refreshHomeChrome().
  static constexpr std::array kTexts{
      "Pick a player to open your deck vault.",
      "Search the local card database and build a deck.",
      "Create a room and share its address, or join a friend's.",
      "The Shandalar battlefield - click a card, then command it.",
  };
  const std::size_t index = static_cast<std::size_t>(screen);
  if (index < kTexts.size()) {
    return kTexts.at(index);
  }
  return "Unknown screen";
}

App::App(std::string version, DeckRepository &decks, const CardDatabase &cards,
         std::filesystem::path dataDir)
    : decks_(decks), cards_(cards), dataDir_(std::move(dataDir)), version_(std::move(version)) {

  // Fonts first: every sf::Text below needs one. A missing font is a warning,
  // never a crash — the window, routing and Esc-to-quit all still work, and
  // the empty font just renders nothing.
  if (loadMenuFont(font_)) {
    fontLoaded_ = true;
  } else {
    std::cerr << "mtg_cpp: warning: bundled menu font not found at " << menuFontPath() << '\n';
  }
  if (loadBoldMenuFont(boldFont_)) {
    boldFontLoaded_ = true;
  } else {
    std::cerr << "mtg_cpp: warning: bundled bold menu font not found at " << boldMenuFontPath()
              << '\n';
  }
  const sf::Font &display = boldFontLoaded_ ? boldFont_ : font_;

  // Top bar: the wordmark (uppercase, letter-spaced, gold) on the left and the
  // version on the right. setLetterSpacing gives the display face a refined,
  // engraved look. Position is recomputed in relayout() (it depends on size).
  wordmark_.setFont(display);
  wordmark_.setCharacterSize(28u);
  wordmark_.setLetterSpacing(3.f);
  wordmark_.setFillColor(menuPalette().gold);
  wordmark_.setString("MTGCPP");

  versionText_.setFont(font_);
  versionText_.setCharacterSize(14u);
  versionText_.setFillColor(menuPalette().muted);
  versionText_.setString("v" + version_);

  // Centered screen heading + one-line description. Positions are recomputed in
  // relayout()/setPlaceholderText (they depend on the current string).
  heading_.setFont(display);
  heading_.setCharacterSize(36u);
  heading_.setLetterSpacing(2.f);
  heading_.setFillColor(menuPalette().gold);

  placeholder_.setFont(font_);
  placeholder_.setCharacterSize(17u);
  placeholder_.setFillColor(menuPalette().parchment);

  // Bottom bar: the keyboard legend.
  hints_.setFont(font_);
  hints_.setCharacterSize(14u);
  hints_.setLetterSpacing(0.5f);
  hints_.setFillColor(menuPalette().muted);
  hints_.setString("1 Home | 2 Deck Editor | 3 Lobby | 4 Table | Esc Quit");

  // Restore the saved player (if any), then load their decks. Home starts on
  // the picker or the vault depending on what was stored.
  home_.setPlayerId(loadPlayerId(dataDir_));
  refreshDecks();
  switchTo(Screen::Home);
}

std::string App::windowTitle() const { return "mtg_cpp " + version_ + " - " + screenName(screen_); }

void App::switchTo(Screen screen) {
  screen_ = screen;
  // The Deck Editor shows the deck being built, the database status and any
  // active import preview; push all three on entry so the screen always
  // reflects the current state.
  if (screen_ == Screen::DeckEditor) {
    deckEditor_.setDeck(editorDeck_);
    deckEditor_.setDatabaseSize(cards_.size());
    deckEditor_.setPreview(preview_);
  }
  // The Table renders the live session board; seed it on entry so a resize or
  // a re-entry never shows a stale battlefield.
  if (screen_ == Screen::Table && lobbySession_ != nullptr) {
    pumpTable();
  }
  // Update the two texts that depend on the active screen; their widths change
  // with the string, so they are re-centered here and again in relayout().
  heading_.setString(toUpper(screenName(screen_)));
  placeholder_.setString(screenPlaceholder(screen_));
  refreshHomeChrome(); // Home overrides the generic placeholder per view
  if (screen_ == Screen::Lobby) {
    // The lobby's deck picker lists the current vault.
    lobby_.setDecks(decks_.listSummaries());
    refreshLobbyChrome();
  }
  relayout();
}

void App::setPlaceholderText(const std::string &text) {
  placeholder_.setString(text);
  const float width = static_cast<float>(windowSize_.x);
  placeholder_.setPosition(centeredAt(placeholder_, width, px(kPlaceholderY, windowSize_)));
}

void App::refreshHomeChrome() {
  if (screen_ != Screen::Home) {
    return;
  }
  const std::optional<std::string> &id = home_.playerId();
  if (!id.has_value()) {
    setPlaceholderText("Pick who you are - each player keeps their own decks.");
  } else {
    setPlaceholderText("Welcome, " + playerName(id.value()) + " - your saved decks live here.");
  }
}

void App::refreshLobbyChrome() {
  if (screen_ != Screen::Lobby) {
    return;
  }
  if (!lobby_.connected()) {
    setPlaceholderText("Create a room and share its address, or join a friend's.");
  } else if (lobby_.canStartTable()) {
    setPlaceholderText("Both players ready - taking you to the table.");
  } else {
    setPlaceholderText("Connected - pick your deck and wait for the opponent.");
  }
}

void App::refreshDecks() { home_.setDecks(decks_.listSummaries()); }

void App::relayout() {
  scale_ = uiScale(windowSize_);
  const float width = static_cast<float>(windowSize_.x);
  const float height = static_cast<float>(windowSize_.y);
  const float topBarHeight = px(kTopBarHeight, windowSize_);
  const float bottomBarHeight = px(kBottomBarHeight, windowSize_);
  const float bottomY = height - bottomBarHeight;

  // Top bar contents, anchored left/right with the shared margin.
  wordmark_.setPosition(
      {px(kMargin, windowSize_), (topBarHeight - wordmark_.getLocalBounds().height) / 2.f});
  versionText_.setPosition({width - px(kMargin, windowSize_) - versionText_.getLocalBounds().width,
                            (topBarHeight - versionText_.getLocalBounds().height) / 2.f});

  heading_.setPosition(centeredAt(heading_, width, px(kHeadingY, windowSize_)));
  placeholder_.setPosition(centeredAt(placeholder_, width, px(kPlaceholderY, windowSize_)));

  // The content band between the chrome bars is handed to the active screen.
  const float contentTop = px(kContentTop, windowSize_);
  const float contentBottom = bottomY - px(kContentBottomPad, windowSize_);
  const sf::FloatRect content{px(kMargin, windowSize_), contentTop,
                              width - (2.f * px(kMargin, windowSize_)), contentBottom - contentTop};
  if (screen_ == Screen::Table) {
    // The table is immersive: it fills the WHOLE window (the velvet mat is the
    // backdrop), so it gets the full rect instead of the menu content band.
    const float margin = px(8.f, windowSize_);
    table_.relayout({margin, margin, width - (2.f * margin), height - (2.f * margin)}, scale_);
  } else if (screen_ == Screen::Home) {
    home_.relayout(content, scale_);
  } else if (screen_ == Screen::DeckEditor) {
    deckEditor_.relayout(content, scale_);
  } else if (screen_ == Screen::Lobby) {
    lobby_.relayout(content, scale_);
  }

  // Bottom bar: the keyboard legend, centered.
  hints_.setPosition(centeredAt(hints_, width, bottomY + px(18.f, windowSize_)));
}

bool App::handleEventCore(const sf::Event &event) {
  // Closed asks to quit (the window wrapper performs the actual close).
  if (event.type == sf::Event::Closed) {
    return true;
  }

  // The Table screen gets every event first: it must close its context menu /
  // clear a selection on Esc and swallow its own keys (T/C/K/F/S/M/life
  // digits) before the global routing sees them. When it does not consume the
  // event (no menu, no selection), Esc falls through and quits as usual.
  if (screen_ == Screen::Table && table_.routeEvent(event)) {
    return false;
  }

  // Resize: clamp to the minimum layout size and re-flow everything (the real
  // window wrapper additionally calls window.setSize). SFML's default view
  // follows the window, so drawing in window coordinates just works.
  if (event.type == sf::Event::Resized) {
    windowSize_.x = std::max(event.size.width, kMinWindowWidth);
    windowSize_.y = std::max(event.size.height, kMinWindowHeight);
    relayout();
    return false;
  }

  if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
    return true;
  }

  // On Home / the Deck Editor / the Lobby, the screen's widgets get first pick
  // of every event (clicks and key events they care about). If a widget
  // consumed it, we must not also route a screen switch.
  if (screen_ == Screen::Home && home_.routeEvent(event)) {
    return false;
  }
  if (screen_ == Screen::DeckEditor && deckEditor_.routeEvent(event)) {
    return false;
  }
  if (screen_ == Screen::Lobby && lobby_.routeEvent(event)) {
    return false;
  }

  if (event.type != sf::Event::KeyPressed) {
    return false;
  }

  switch (event.key.code) {
  case sf::Keyboard::Num1:
    switchTo(Screen::Home);
    break;
  case sf::Keyboard::Num2:
    switchTo(Screen::DeckEditor);
    break;
  case sf::Keyboard::Num3:
    switchTo(Screen::Lobby);
    break;
  case sf::Keyboard::Num4:
    switchTo(Screen::Table);
    break;
  default:
    return false; // every other key is ignored
  }
  return false;
}

void App::handleEvent(sf::RenderWindow &window, const sf::Event &event) {
  if (event.type == sf::Event::Closed) {
    window.close();
    return;
  }
  if (event.type == sf::Event::Resized) {
    if (handleEventCore(event)) {
      window.close();
      return;
    }
    // The core already re-flowed to the clamped size; tell the OS window.
    window.setSize(windowSize_);
    return;
  }
  if (handleEventCore(event)) {
    window.close();
  }
}

bool App::injectEvent(const sf::Event &event) { return !handleEventCore(event); }

void App::pump() { pollActions(); }

void App::pollActions() {
  // A click resolves on mouse release; poll once per frame and react. Each
  // screen reports at most one action per poll.
  if (screen_ == Screen::Home) {
    const HomeAction action = home_.pollAction();
    if (action != HomeAction::None) {
      onHomeAction(action);
    }
  } else if (screen_ == Screen::DeckEditor) {
    const DeckEditorAction action = deckEditor_.pollAction();
    if (action != DeckEditorAction::None) {
      onDeckEditorAction(action);
    }
  } else if (screen_ == Screen::Lobby) {
    // Keep the live session state in front of the screen, then react.
    pumpLobby();
    const LobbyAction action = lobby_.pollAction();
    if (action != LobbyAction::None) {
      onLobbyAction(action);
    }
    // Ready-to-start transition: switch to the table once, on the rising edge.
    if (lobby_.canStartTable() && !lobbyStarted_) {
      lobbyStarted_ = true;
      switchTo(Screen::Table);
    }
    if (!lobby_.canStartTable()) {
      lobbyStarted_ = false;
    }
  } else if (screen_ == Screen::Table) {
    pumpTable();
    const TableAction action = table_.pollAction();
    if (action != TableAction::None) {
      onTableAction(action);
    }
  }
}

void App::onHomeAction(HomeAction action) {
  switch (action) {
  case HomeAction::None:
    break;
  case HomeAction::SelectProfile: {
    // Persist the pick, then show that player's decks (vault view).
    const std::optional<std::string> id = home_.playerId();
    if (id.has_value()) {
      savePlayerId(dataDir_, id.value());
      refreshDecks();
    }
    relayout(); // the view flipped from picker to vault
    refreshHomeChrome();
    break;
  }
  case HomeAction::DeleteDeck: {
    // Read the deck index BEFORE reloading (the index is stale afterwards).
    const std::optional<std::size_t> index = home_.deleteIndex();
    if (index.has_value() && index.value() < home_.decks().size()) {
      decks_.remove(home_.decks().at(index.value()).id);
      refreshDecks();
    }
    break;
  }
  case HomeAction::SwitchPlayer:
    clearPlayerId(dataDir_);
    home_.setPlayerId(std::nullopt);
    relayout(); // the view flipped from vault to picker
    refreshHomeChrome();
    break;
  case HomeAction::NewDeck:
    // A brand-new deck starts empty; the editor keeps any in-progress deck
    // when reached via the keyboard router.
    editorDeck_.clear();
    editingDeckId_.reset();
    preview_.reset();
    deckEditor_.setDeckName("");
    deckEditor_.setDeck(editorDeck_);
    deckEditor_.setPreview(std::nullopt);
    switchTo(Screen::DeckEditor);
    break;
  case HomeAction::EditDeck: {
    // Open the highlighted deck in the editor for editing (webapp: a deck row
    // links to /editor/{id}). The editor deck is replaced with the stored
    // cards and the deck name is filled in.
    const DeckSummary *summary = home_.highlightedDeck();
    if (summary != nullptr) {
      const std::variant<Deck, DeckReadError> loaded = decks_.read(summary->id);
      if (std::holds_alternative<Deck>(loaded)) {
        const Deck &deck = std::get<Deck>(loaded);
        editorDeck_ = deck.cards;
        editingDeckId_ = deck.id;
        preview_.reset();
        deckEditor_.setDeckName(deck.name);
        deckEditor_.setDeck(editorDeck_);
        deckEditor_.setPreview(std::nullopt);
        switchTo(Screen::DeckEditor);
      }
    }
    break;
  }
  case HomeAction::PlayOnline:
    switchTo(Screen::Lobby);
    break;
  }
}

void App::onDeckEditorAction(DeckEditorAction action) {
  switch (action) {
  case DeckEditorAction::None:
    break;
  case DeckEditorAction::Search: {
    // Search the local database and push the results back to the screen. The
    // screen already trims blank queries, so reaching here means a real search.
    searchResults_ = cards_.search(deckEditor_.searchQuery());
    deckEditor_.setResults(searchResults_);
    break;
  }
  case DeckEditorAction::AddCard: {
    // Map the clicked result back to a card (the same list the screen shows),
    // then add one copy with the pure editor operation.
    const std::optional<std::size_t> index = deckEditor_.resultIndex();
    if (index.has_value() && index.value() < searchResults_.size()) {
      editorDeck_ = addCardToDeck(editorDeck_, searchResults_.at(index.value()));
      deckEditor_.setDeck(editorDeck_);
    }
    break;
  }
  case DeckEditorAction::SetQuantity: {
    const std::optional<std::size_t> row = deckEditor_.deckRow();
    if (row.has_value() && row.value() < editorDeck_.size()) {
      const std::string cardId = cardKey(editorDeck_.at(row.value()));
      editorDeck_ = setCardQuantity(editorDeck_, cardId, deckEditor_.quantity());
      deckEditor_.setDeck(editorDeck_);
    }
    break;
  }
  case DeckEditorAction::RemoveCard: {
    const std::optional<std::size_t> row = deckEditor_.deckRow();
    if (row.has_value() && row.value() < editorDeck_.size()) {
      const std::string cardId = cardKey(editorDeck_.at(row.value()));
      editorDeck_ = removeCardFromDeck(editorDeck_, cardId);
      deckEditor_.setDeck(editorDeck_);
    }
    break;
  }
  case DeckEditorAction::ImportText: {
    // Parse the pasted Arena text against the local database and open the
    // review panel. A parse failure (no valid card lines) surfaces as an
    // import error under the text area instead of crashing.
    try {
      const DeckParseResult result = importDeck(deckEditor_.importText(), cards_);
      preview_ = makeImportPreview(result);
      deckEditor_.setImportError("");
    } catch (const DeckParseError &exc) {
      preview_.reset();
      deckEditor_.setImportError(exc.what());
    }
    deckEditor_.setPreview(preview_);
    break;
  }
  case DeckEditorAction::CancelImport:
    preview_.reset();
    deckEditor_.setPreview(std::nullopt); // also returns the screen to build view
    break;
  case DeckEditorAction::ConfirmImport: {
    if (preview_.has_value()) {
      const std::optional<std::vector<Card>> committed = confirmImport(preview_.value());
      if (committed.has_value()) {
        editorDeck_ = committed.value();
        deckEditor_.setDeck(editorDeck_);
        preview_.reset();
        deckEditor_.setPreview(std::nullopt); // back to the build view
      }
    }
    break;
  }
  case DeckEditorAction::SelectMissing: {
    if (preview_.has_value()) {
      const std::optional<std::size_t> index = deckEditor_.missingIndex();
      if (index.has_value() && index.value() < preview_->missing.size()) {
        preview_ = selectMissing(preview_.value(), preview_->missing.at(index.value()));
        deckEditor_.setPreview(preview_);
      }
    }
    break;
  }
  case DeckEditorAction::RemoveMissing: {
    if (preview_.has_value()) {
      const std::optional<std::size_t> index = deckEditor_.missingIndex();
      if (index.has_value() && index.value() < preview_->missing.size()) {
        const std::string key = missingCardKey(preview_->missing.at(index.value()));
        preview_ = removeMissing(preview_.value(), key);
        deckEditor_.setPreview(preview_);
      }
    }
    break;
  }
  case DeckEditorAction::ReplaceMissing: {
    if (preview_.has_value() && preview_->activeMissing.has_value()) {
      const std::optional<std::size_t> index = deckEditor_.resultIndex();
      if (index.has_value() && index.value() < searchResults_.size()) {
        const std::string key = missingCardKey(preview_->activeMissing.value());
        preview_ = replaceMissing(preview_.value(), key, searchResults_.at(index.value()));
        deckEditor_.setPreview(preview_);
      }
    }
    break;
  }
  case DeckEditorAction::ReplaceSearch: {
    // Same search path as the build view; the results feed the replace panel.
    searchResults_ = cards_.search(deckEditor_.replaceQuery());
    deckEditor_.setReplaceResults(searchResults_);
    break;
  }
  case DeckEditorAction::DismissPreview: {
    if (preview_.has_value()) {
      preview_ = dismissMissing(preview_.value());
      deckEditor_.setPreview(preview_);
    }
    break;
  }
  case DeckEditorAction::Back:
    switchTo(Screen::Home);
    break;
  case DeckEditorAction::ExportToClipboard: {
    // Copy the deck as Arena text to the OS clipboard (SFML 2.6's sf::Clipboard;
    // only the running app touches the clipboard — tests never poll actions).
    const std::string text = toArenaText(editorDeck_);
    if (!text.empty()) {
      sf::Clipboard::setString(text);
    }
    break;
  }
  case DeckEditorAction::SaveDeck: {
    // Persist the deck: create a new one when the editor holds a new deck,
    // otherwise update the deck being edited. On success return to Home with
    // the vault refreshed (the webapp navigates home on create/update success).
    std::string name = deckEditor_.deckName();
    if (name.empty()) {
      name = "Untitled Deck";
    }
    const DeckTotals totals = deckTotals(editorDeck_);
    if (editingDeckId_.has_value()) {
      const std::variant<Deck, DeckReadError> existing = decks_.read(editingDeckId_.value());
      if (std::holds_alternative<Deck>(existing)) {
        Deck deck = std::get<Deck>(existing);
        deck.name = name;
        deck.cards = editorDeck_;
        deck.total_cards = totals.total_cards;
        deck.unique_cards = totals.unique_cards;
        decks_.update(deck);
      }
    } else {
      Deck deck;
      deck.name = name;
      deck.format = "Other";
      deck.cards = editorDeck_;
      deck.total_cards = totals.total_cards;
      deck.unique_cards = totals.unique_cards;
      // The created deck keeps its new id, so a later Save edits it instead of
      // spawning yet another copy (webapp: createDeckSuccess stores the deck).
      const Deck created = decks_.create(deck);
      editingDeckId_ = created.id;
    }
    refreshDecks();
    switchTo(Screen::Home);
    break;
  }
  }
}

void App::startLobbyAsHost() {
  // Clean any previous session, then stand up the relay and join it locally.
  // stopLobby() also cleared the lobby's deck list, so re-fill the picker once
  // the room is up (a connected lobby must always list the vault).
  stopLobby();
  lobby_.setDecks(decks_.listSummaries());
  lobby_.setConnected(false);
  lobby_.setConnecting(true);
  try {
    lobbyTransport_ = std::make_unique<net::AsioTransport>(kLobbyPort);
    lobbyTransport_->start();
    lobbyServer_ = std::make_unique<net::Server>(*lobbyTransport_);
    lobbyServer_->start();
    const std::string port = std::to_string(lobbyTransport_->localPort());
    lobby_.setShareAddress(net::localIpAddress() + ":" + port);
    lobbyClient_ = std::make_unique<net::Client>();
    if (!lobbyClient_->connect("127.0.0.1", port)) {
      lobby_.setError("Could not connect to the local relay");
      stopLobby();
      return;
    }
    lobbySession_ = std::make_unique<state::Session>(*lobbyClient_);
  } catch (const std::exception &exc) {
    lobby_.setError(exc.what());
    stopLobby();
  }
  refreshLobbyChrome();
}

void App::startLobbyAsGuest(const std::string &address) {
  // Split `IP:PORT` at the last colon; both halves must be non-empty.
  // stopLobby() cleared the deck list; re-fill it for the connected picker.
  stopLobby();
  lobby_.setDecks(decks_.listSummaries());
  lobby_.setConnected(false);
  lobby_.setConnecting(true);
  const std::size_t colon = address.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= address.size()) {
    lobby_.setError("Enter the address as IP:PORT, e.g. 192.168.1.5:7500");
    lobby_.setConnecting(false);
    refreshLobbyChrome();
    return;
  }
  const std::string host = address.substr(0, colon);
  const std::string port = address.substr(colon + 1);
  lobbyClient_ = std::make_unique<net::Client>();
  if (!lobbyClient_->connect(host, port)) {
    lobby_.setError("Could not connect to " + address);
    lobby_.setConnecting(false);
    lobbyClient_.reset();
    refreshLobbyChrome();
    return;
  }
  lobbySession_ = std::make_unique<state::Session>(*lobbyClient_);
  refreshLobbyChrome();
}

void App::stopLobby() {
  if (lobbySession_ != nullptr) {
    lobbySession_->leave();
  }
  lobbySession_.reset();
  lobbyClient_.reset();
  lobbyServer_.reset();
  lobbyTransport_.reset();
  lobby_.reset();
  lobbyStarted_ = false;
  refreshLobbyChrome();
}

void App::pumpLobby() {
  if (lobbySession_ == nullptr) {
    return;
  }
  if (lobbyServer_ != nullptr) {
    lobbyServer_->runOnce();
  }
  lobbySession_->drain();

  // Push the live session state into the screen.
  const state::Session &session = *lobbySession_;
  lobby_.setConnected(session.connected());
  lobby_.setConnecting(false);
  lobby_.setRole(session.role());
  lobby_.setPlayerId(session.playerId());
  lobby_.setPlayers(session.players());
  LobbyStatus status = LobbyStatus::Idle;
  switch (session.status()) {
  case state::Session::RoomStatus::Idle:
    status = LobbyStatus::Idle;
    break;
  case state::Session::RoomStatus::Waiting:
    status = LobbyStatus::Waiting;
    break;
  case state::Session::RoomStatus::Ready:
    status = LobbyStatus::Ready;
    break;
  }
  lobby_.setStatus(status);
  const state::BoardState &board = session.board();
  if (board.my_deck.has_value()) {
    lobby_.setMyDeckName(board.my_deck.value().name);
  } else {
    lobby_.setMyDeckName(std::nullopt);
  }
  if (board.their_deck.has_value()) {
    lobby_.setTheirDeckName(board.their_deck.value().name);
  } else {
    lobby_.setTheirDeckName(std::nullopt);
  }
  lobby_.setError(session.lastError());
  refreshLobbyChrome();
  // The lobby's view flips as the connection/status changes (connect panel ->
  // room panel -> deck picker -> picked-deck status). relayout() is normally
  // driven by switchTo/resize, so re-run it here whenever the lobby is live —
  // otherwise the flipped view keeps the previous panel's widget rects (the
  // deck picker would never appear after Create Room).
  if (screen_ == Screen::Lobby) {
    relayout();
  }
}

void App::onLobbyAction(LobbyAction action) {
  switch (action) {
  case LobbyAction::None:
    break;
  case LobbyAction::CreateRoom:
    startLobbyAsHost();
    break;
  case LobbyAction::JoinRoom:
    startLobbyAsGuest(lobby_.joinAddress());
    break;
  case LobbyAction::ChooseDeck: {
    // Load the picked deck from the repository and bring it to the table.
    const std::optional<std::size_t> index = lobby_.deckIndex();
    if (index.has_value() && index.value() < lobby_.decks().size() && lobbySession_ != nullptr) {
      const std::variant<Deck, DeckReadError> loaded =
          decks_.read(lobby_.decks().at(index.value()).id);
      if (std::holds_alternative<Deck>(loaded)) {
        lobbySession_->chooseDeck(std::get<Deck>(loaded));
      }
    }
    break;
  }
  case LobbyAction::LeaveRoom:
    stopLobby();
    break;
  }
}

void App::pumpTable() {
  if (lobbySession_ == nullptr) {
    return;
  }
  if (lobbyServer_ != nullptr) {
    lobbyServer_->runOnce();
  }
  lobbySession_->drain();

  // Push the live session state into the table screen.
  const state::Session &session = *lobbySession_;
  table_.setBoard(session.board());
  table_.setRole(session.role());
  table_.setPlayerId(session.playerId());
  table_.setRevealRequest(session.reveal().pending_request_from);
  table_.setRevealResult(session.reveal().reveal_accepted, session.reveal().revealed_hand);
  table_.setError(session.lastError());
}

void App::onTableAction(TableAction action) {
  switch (action) {
  case TableAction::None:
    break;
  case TableAction::CardCommand:
    // The screen built the full action (tap/counter/token/flip/move/life);
    // applying it locally also syncs it to the opponent.
    if (lobbySession_ != nullptr) {
      lobbySession_->applyLocalAction(table_.action());
    }
    break;
  case TableAction::RequestReveal:
    if (lobbySession_ != nullptr) {
      lobbySession_->requestHandReveal();
    }
    break;
  case TableAction::AcceptReveal:
    if (lobbySession_ != nullptr) {
      lobbySession_->acceptHandReveal(table_.revealCards());
    }
    break;
  case TableAction::DenyReveal:
    if (lobbySession_ != nullptr) {
      lobbySession_->denyHandReveal();
    }
    break;
  case TableAction::DismissReveal:
    if (lobbySession_ != nullptr) {
      lobbySession_->dismissRevealPrompt();
    }
    break;
  case TableAction::ClearRevealed:
    if (lobbySession_ != nullptr) {
      lobbySession_->clearRevealed();
    }
    break;
  case TableAction::Leave:
    stopLobby();
    switchTo(Screen::Lobby);
    break;
  }
}

void App::draw(sf::RenderWindow &window) {
  // The universal SFML render pattern: clear, draw every drawable, then
  // display() — the caller swaps buffers so the user sees one complete frame.
  // The Table is immersive: the velvet playmat IS the background, drawn
  // full-window and rebuilt at the current size, and the menu chrome is
  // skipped (the table draws its own toolbar).
  window.clear(menuPalette().background);

  if (screen_ == Screen::Table) {
    if (playmatSize_ != windowSize_) {
      playmatSize_ = windowSize_;
      buildPlaymatTexture(playmat_, windowSize_);
    }
    if (playmat_.getSize().x != 0u) {
      sf::Sprite mat(playmat_);
      mat.setScale({static_cast<float>(windowSize_.x) / static_cast<float>(playmat_.getSize().x),
                    static_cast<float>(windowSize_.y) / static_cast<float>(playmat_.getSize().y)});
      window.draw(mat);
    }
    table_.draw(window, font_, boldFont_);
    return;
  }

  const float width = static_cast<float>(windowSize_.x);
  const float topBarHeight = px(kTopBarHeight, windowSize_);
  const float bottomBarHeight = px(kBottomBarHeight, windowSize_);
  const float bottomY = static_cast<float>(windowSize_.y) - bottomBarHeight;

  // Top bar (panel fill) with a gold rule beneath it frames the content band.
  sf::RectangleShape topBar({width, topBarHeight});
  topBar.setFillColor(menuPalette().panel);
  window.draw(topBar);
  sf::RectangleShape topRule({width, std::max(1.f, 2.f * scale_)});
  topRule.setPosition({0.f, topBarHeight});
  topRule.setFillColor(menuPalette().gold);
  window.draw(topRule);

  window.draw(wordmark_);
  window.draw(versionText_);

  window.draw(heading_);
  window.draw(placeholder_);
  if (screen_ == Screen::Home) {
    home_.draw(window, font_, boldFont_);
  } else if (screen_ == Screen::DeckEditor) {
    deckEditor_.draw(window, font_, boldFont_);
  } else if (screen_ == Screen::Lobby) {
    lobby_.draw(window, font_, boldFont_);
  }

  // Bottom bar: gold rule on top, panel fill below, hints centered.
  sf::RectangleShape bottomRule({width, std::max(1.f, 2.f * scale_)});
  bottomRule.setPosition({0.f, bottomY});
  bottomRule.setFillColor(menuPalette().gold);
  window.draw(bottomRule);
  sf::RectangleShape bottomBar({width, bottomBarHeight});
  bottomBar.setPosition({0.f, bottomY});
  bottomBar.setFillColor(menuPalette().panel);
  window.draw(bottomBar);
  window.draw(hints_);
}

void App::run() {
  // The window is created HERE, not in the constructor: run() is the only
  // place that needs a display, so App construction + routing stay testable
  // on headless CI. VideoMode braces keep the unsigned dimensions explicit.
  // setKeyRepeatEnabled(false) means a held key fires one KeyPressed (we
  // react to presses, not to the OS auto-repeat).
  sf::RenderWindow window(sf::VideoMode({windowSize_.x, windowSize_.y}), windowTitle());
  window.setKeyRepeatEnabled(false);

  // Event loop: poll every pending event, react, then render exactly one frame.
  while (window.isOpen()) {
    for (sf::Event event; window.pollEvent(event);) {
      handleEvent(window, event);
    }
    pollActions(); // clicks resolve on release; act on them before drawing
    draw(window);
    window.display();
  }
}

} // namespace mtgcpp::core
