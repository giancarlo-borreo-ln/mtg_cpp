// The SFML application shell (M4.1 + M4.3 + M5.1): window lifecycle, screen
// routing, the responsive Home screen and the Deck Editor.
//
// What this milestone establishes, and the SFML concepts it demonstrates:
//
//   * RenderWindow — the OS window plus an OpenGL context that everything is
//     drawn into. Created inside run(), NOT the constructor, so the App itself
//     can be built and routed in headless unit tests (no display needed).
//   * The event loop — every frame the app polls events, reacts, then renders:
//         while (window.isOpen())
//             for each event: handle it
//             poll screen actions
//             draw everything
//             window.display()          // swap the back buffer to the screen
//   * clear()/draw()/display() — erase the window, paint drawables, present.
//   * sf::Text — a drawable string laid out from an sf::Font. It stores a
//     pointer to the font, so the font must outlive the text (ours are members
//     of App, so they always do).
//   * Screen routing — an enum + a switchTo() that swaps the active screen.
//
// Resizable + responsive (see ui/layout.h): the window re-flows on every resize
// through a single relayout() pass, and the height-based UI scale keeps the
// menu readable from a small laptop window up to a large monitor. Layout is
// anchored to the window size, never to fixed constants.
//
// The Home screen (M4.3) is a real screen: profile picker -> deck vault backed
// by the DeckRepository, profile persisted via the profile store. The Deck
// Editor (M5.1) is a real screen too: search the local card database and build
// a deck (add/remove/quantity) with a live deck list. The App does all I/O and
// all deck mutation; the screens only report actions.
//
// Layout: a fixed 8px-based spacing grid, a top bar with the wordmark + version,
// a centered screen heading + description, the screen body, and a bottom hint
// bar. Gold rules frame the content area. All strings are ASCII-only.
//
// Try it: launch the app and press 1/2/3/4 to route, Esc to quit. On Home, pick
// a profile, then press "New Deck" to open the Deck Editor: type a card name,
// Search, and click results to build a deck. Resize the window and the panes
// re-flow.
#pragma once

#include "core/card.h"
#include "core/card_database.h"
#include "core/import_preview.h"
#include "net/client.h"
#include "net/server.h"
#include "net/transport.h"
#include "state/session.h"
#include "store/deck_repository.h"
#include "ui/art_cache.h"
#include "ui/cursor.h"
#include "ui/screens/deck_editor_screen.h"
#include "ui/screens/home_screen.h"
#include "ui/screens/lobby_screen.h"
#include "ui/screens/table_screen.h"

#include <SFML/Graphics.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mtgcpp::core {

// The four top-level screens, in menu order. Home and Deck Editor are real
// screens; Lobby/Table fill in over Sprints 8-9.
enum class Screen { Home, DeckEditor, Lobby, Table };

// Stable menu label for a screen (window title + on-screen heading). Unknown
// values fall back to "Unknown" so an out-of-range cast never reads out of
// bounds (the lookup is bounds-checked via .at()).
const char *screenName(Screen screen);

// One-line description shown under the screen heading.
const char *screenPlaceholder(Screen screen);

class App {
public:
  // Loads the bundled menu fonts, reads the saved player profile, loads the
  // deck list and lays out the chrome. `version` is shown in the window title
  // and the top bar. `decks` is the deck store the Home screen lists and
  // deletes from; `cards` is the local card database the Deck Editor searches
  // (owned by main); `dataDir` is where the profile is persisted. Does NOT
  // open a window — that happens in run().
  App(std::string version, DeckRepository &decks, const CardDatabase &cards,
      std::filesystem::path dataDir);

  // Open the window and run the event loop until it is closed (Esc or the OS
  // close button). Blocks for the whole session. Needs a display.
  void run();

  // Router: switch the active screen (updates heading + body text + layout).
  void switchTo(Screen screen);

  // Enable/disable the runtime art cache (M10.1). Off by default so the app can
  // run fully offline / without downloads; the real launcher turns it on. The
  // table keeps its procedural fallback while it is off.
  void setArtCacheEnabled(bool enabled);

  // The pointer the active screen wants over `point` (M10.3): Hand over
  // clickable widgets, Text over editable fields, Arrow otherwise. Pure, so
  // tests pin the mapping without a display.
  CursorKind cursorAt(sf::Vector2f point) const;

  Screen currentScreen() const { return screen_; }

  // True when the bundled regular font was found and parsed. Screens keep
  // working without it (nothing renders) but tests assert it loads.
  bool fontLoaded() const { return fontLoaded_; }

  // The current responsive UI scale (drives widget geometry + fonts).
  float scale() const { return scale_; }

  // Number of cards indexed in the local database handed to the Deck Editor
  // (0 = not loaded; the editor surfaces that as a hint).
  std::size_t cardDatabaseSize() const { return cards_.size(); }

  // --- Scripting seam (Sprint 12) ------------------------------------------
  // Push one synthetic event through the exact handler the window loop uses,
  // minus window side effects (no window close/resize calls). Returns false
  // when the event asked to quit (Esc / OS close). Lets an integration driver
  // simulate mouse presses / key presses headlessly.
  bool injectEvent(const sf::Event &event);
  // Run one action-poll pass (clicks resolve on release, screens report their
  // actions). The window loop calls this every frame between event polls.
  void pump();

  // The four screens, exposed so the integration driver can click their
  // widgets (read-only geometry via their own const accessors).
  const HomeScreen &home() const { return home_; }
  const DeckEditorScreen &deckEditor() const { return deckEditor_; }
  const LobbyScreen &lobby() const { return lobby_; }
  const TableScreen &table() const { return table_; }

private:
  std::string windowTitle() const;
  // The event-handling logic with NO window side effects. Returns true when the
  // event requested a quit. Shared by handleEvent (real window) and injectEvent
  // (scripting) so both paths stay identical.
  bool handleEventCore(const sf::Event &event);
  void handleEvent(sf::RenderWindow &window, const sf::Event &event);
  void pollActions();
  void draw(sf::RenderWindow &window);
  // Apply the pointer shape the active screen wants at `point` (M10.3), only
  // when it changes. Degrades to the default arrow if a cursor failed to load.
  void updateCursor(sf::RenderWindow &window, sf::Vector2f point);

  // Recompute the chrome and the active screen's widgets from windowSize_.
  // Called at startup and on every window resize (the single layout pass).
  void relayout();
  // Re-center the one-line description under the heading.
  void setPlaceholderText(const std::string &text);
  // Re-read the deck list into the Home screen (after create/delete).
  void refreshDecks();
  // Translate a HomeScreen action into I/O + navigation.
  void onHomeAction(HomeAction action);
  // Translate a DeckEditorScreen action into database searches + deck
  // mutations + navigation.
  void onDeckEditorAction(DeckEditorAction action);
  // Keep the Home placeholder in sync with its view (picker vs. welcome).
  void refreshHomeChrome();

  // --- Lobby (Sprint 8) -----------------------------------------------------
  // Start the embedded relay + a local session (host) or connect to a remote
  // address (guest). Thin glue: the Session/relay do the real work.
  void startLobbyAsHost();
  void startLobbyAsGuest(const std::string &address);
  void stopLobby();
  // Pump the relay + session once per frame and push the session state into
  // the lobby screen.
  void pumpLobby();
  void refreshLobbyChrome();
  void onLobbyAction(LobbyAction action);

  // --- Table (Sprint 9) -----------------------------------------------------
  // Push the live session board + reveal state into the table screen once per
  // frame, and translate its actions into session calls / navigation.
  void pumpTable();
  void onTableAction(TableAction action);

  DeckRepository &decks_;                    // the deck store (owned by main)
  const CardDatabase &cards_;                // the local card database (owned by main)
  std::filesystem::path dataDir_;            // where profile.json + the art cache live
  std::unique_ptr<core::ArtCache> artCache_; // runtime card art (M10.1)
  HomeScreen home_;
  DeckEditorScreen deckEditor_;
  LobbyScreen lobby_;
  TableScreen table_;
  // The deck being built in the editor (owned by App; screens only display).
  std::vector<Card> editorDeck_;
  // The id of the deck being edited, or nullopt when the editor holds a new
  // (not yet saved) deck.
  std::optional<std::string> editingDeckId_;
  // The last search results (kept so an AddCard action can index them).
  std::vector<Card> searchResults_;
  // The active import preview, or nullopt when none (owned by App; the screen
  // renders it via setPreview).
  std::optional<ImportPreview> preview_;

  // Lobby networking, owned by App (created on CreateRoom/JoinRoom, destroyed
  // on LeaveRoom). The host owns the relay; both sides own a client + session.
  std::unique_ptr<net::AsioTransport> lobbyTransport_;
  std::unique_ptr<net::Server> lobbyServer_;
  std::unique_ptr<net::Client> lobbyClient_;
  std::unique_ptr<state::Session> lobbySession_;
  // Edge flag: rises once when the lobby becomes ready to start, so the app
  // switches to the table exactly once.
  bool lobbyStarted_ = false;

  sf::Font font_;           // regular face, body text
  sf::Font boldFont_;       // bold face, titles/buttons
  bool fontLoaded_ = false; // false = degrade gracefully, never crash
  bool boldFontLoaded_ = false;
  Screen screen_ = Screen::Home;
  std::string version_;

  sf::Vector2u windowSize_{960u, 600u}; // current size; updated by run()/resize
  float scale_ = 1.f;                   // height-based responsive scale

  // The velvet playmat backdrop, rebuilt at the window size when the table is
  // shown (the table draws it full-window, replacing the menu background).
  sf::Texture playmat_;
  sf::Vector2u playmatSize_{0u, 0u};

  // Cursor states (M10.3): loaded once; the active screen's cursorAt drives
  // which one is applied each frame.
  sf::Cursor handCursor_;
  sf::Cursor textCursor_;
  bool handCursorLoaded_ = false;
  bool textCursorLoaded_ = false;
  CursorKind appliedCursor_ = CursorKind::Arrow;

  sf::Text wordmark_;    // app name, top-left
  sf::Text versionText_; // version, top-right
  sf::Text heading_;     // current screen name, centered under the top bar
  sf::Text placeholder_; // one-line description under the heading
  sf::Text hints_;       // keyboard legend, centered in the bottom bar
};

} // namespace mtgcpp::core
