// Integration driver (Sprint 12, M12.2): exercises the whole app by simulating
// mouse presses (and keyboard input) through the same event path the window
// loop uses — no display needed, because App routing + pollActions are already
// headless-safe (the window is created only in run(), which this driver never
// calls).
//
// The driver builds the real App with a temp data dir + a tiny in-memory card
// database, then scripts the full user journey:
//
//   Home profile picker -> deck vault
//   Deck Editor: search -> add -> save, and import-preview -> confirm
//   Lobby: create room -> choose a deck, then a real loopback guest joins
//   Table: select -> Tap / To Stack, and life editing — all asserted to land on
//     the opponent's byte-identical mirror over the wire.
//
// Every step prints PASS/FAIL; the process exits non-zero on any failure.
// Driven by /tmp/todocpp/integration.sh.

#include "core/card.h"
#include "core/card_database.h"
#include "net/client.h"
#include "state/session.h"
#include "store/deck_repository.h"
#include "ui/app.h"
#include "ui/layout.h"

#include <SFML/Graphics.hpp>
#include <SFML/Window/Event.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

// A throwaway app-data dir, removed on exit.
class TempDir {
public:
  TempDir() {
    const std::string unique =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = std::filesystem::temp_directory_path() / ("mtgcpp_integration_" + unique);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }

  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;
  TempDir(TempDir &&) = delete;
  TempDir &operator=(TempDir &&) = delete;

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

// The failure count, held behind a function so there is no mutable global.
int &failureCount() {
  static int failures = 0;
  return failures;
}

void check(bool ok, const char *what) {
  std::cout << (ok ? "PASS " : "FAIL ") << what << "\n" << std::flush;
  if (!ok) {
    ++failureCount();
  }
}

// --- Event builders ---------------------------------------------------------

sf::Vector2f center(const sf::FloatRect &rect) {
  return {rect.left + (rect.width / 2.f), rect.top + (rect.height / 2.f)};
}

sf::Event mouse(bool pressed, sf::Mouse::Button button, sf::Vector2f p) {
  sf::Event event;
  event.type = pressed ? sf::Event::MouseButtonPressed : sf::Event::MouseButtonReleased;
  event.mouseButton.button = button;
  event.mouseButton.x = static_cast<int>(p.x);
  event.mouseButton.y = static_cast<int>(p.y);
  return event;
}

sf::Event keyDown(sf::Keyboard::Key code) {
  sf::Event event;
  event.type = sf::Event::KeyPressed;
  event.key.code = code;
  return event;
}

sf::Event textChar(char c) {
  sf::Event event;
  event.type = sf::Event::TextEntered;
  event.text.unicode = static_cast<sf::Uint32>(static_cast<unsigned char>(c));
  return event;
}

// --- Input helpers (the "simulated mouse press" primitives) -----------------

// A full left-click: press inside, release inside, then one action-poll pass.
void click(mtgcpp::core::App &app, sf::Vector2f p) {
  app.injectEvent(mouse(true, sf::Mouse::Left, p));
  app.injectEvent(mouse(false, sf::Mouse::Left, p));
  app.pump();
}

// Type printable ASCII into the focused text field/area (one TextEntered per
// character), then one poll pass.
void type(mtgcpp::core::App &app, const std::string &text) {
  for (const char c : text) {
    app.injectEvent(textChar(c));
  }
  app.pump();
}

// Press a keyboard key, then one poll pass.
void pressKey(mtgcpp::core::App &app, sf::Keyboard::Key code) {
  app.injectEvent(keyDown(code));
  app.pump();
}

// --- Fixtures ---------------------------------------------------------------

mtgcpp::core::Card makeCard(const std::string &name, const std::string &type_line,
                            const std::string &number, const std::string &mana_cost, int qty) {
  mtgcpp::core::Card card;
  card.name = name;
  card.set_code = "war";
  card.set_name = "War of the Spark";
  card.collector_number = number;
  card.type_line = type_line;
  card.mana_cost = mana_cost;
  card.quantity = qty;
  card.image_uris["png"] = "https://img/" + name + ".png";
  return card;
}

// A tiny local card database, loaded from an inline JSONL stream (the same
// shape the real Scryfall bulk file uses, so the loader exercises the real
// parse path).
void loadFixture(mtgcpp::core::CardDatabase &cards) {
  std::istringstream in(
      "{\"id\":\"f1\",\"name\":\"Forest\",\"set\":\"war\",\"set_name\":\"War of the "
      "Spark\",\"collector_number\":\"263\",\"type_line\":\"Basic Land\","
      "\"image_uris\":{\"png\":\"https://img/forest.png\"}}\n"
      "{\"id\":\"f2\",\"name\":\"Mountain\",\"set\":\"war\",\"set_name\":\"War of the "
      "Spark\",\"collector_number\":\"261\",\"type_line\":\"Basic Land\","
      "\"image_uris\":{\"png\":\"https://img/mountain.png\"}}\n"
      "{\"id\":\"f3\",\"name\":\"Grizzly Bears\",\"set\":\"war\",\"set_name\":\"War of the "
      "Spark\",\"collector_number\":\"176\",\"type_line\":\"Creature - Bear\","
      "\"mana_cost\":\"{1}{G}\",\"image_uris\":{\"png\":\"https://img/bears.png\"}}\n"
      "{\"id\":\"f4\",\"name\":\"Lightning Bolt\",\"set\":\"war\",\"set_name\":\"War of the "
      "Spark\",\"collector_number\":\"153\",\"type_line\":\"Instant\","
      "\"mana_cost\":\"{R}\",\"image_uris\":{\"png\":\"https://img/bolt.png\"}}\n");
  cards.load(in);
}

mtgcpp::core::Deck seedDeck() {
  mtgcpp::core::Deck deck;
  deck.name = "Warriors";
  deck.format = "Other";
  deck.cards = {makeCard("Forest", "Basic Land", "263", "", 3),
                makeCard("Mountain", "Basic Land", "261", "", 3),
                makeCard("Grizzly Bears", "Creature - Bear", "176", "{1}{G}", 1),
                makeCard("Lightning Bolt", "Instant", "153", "{R}", 1)};
  return deck;
}

// Wait for `pred` by pumping the app (host side: relay + session) AND the guest
// session, the way a real frame loop would.
template <typename Pred>
bool waitUntil(Pred pred, mtgcpp::core::App &app, mtgcpp::state::Session &guest) {
  const auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < 10s) {
    app.pump();
    guest.drain();
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return false;
}

// Wait for `pred` pumping only the app (used before a guest exists).
template <typename Pred> bool waitApp(mtgcpp::core::App &app, Pred pred) {
  const auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < 10s) {
    app.pump();
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return false;
}

// --- Flows ------------------------------------------------------------------

void flowProfile(mtgcpp::core::App &app) {
  // Home starts on the profile picker; clicking a profile card picks it.
  click(app, center(app.home().profileButton(0).bounds()));
  check(app.home().playerId().has_value(), "home: profile picker -> player selected");
}

void flowEditorBuildAndSave(mtgcpp::core::App &app) {
  click(app, center(app.home().newDeckButton().bounds()));
  check(app.currentScreen() == mtgcpp::core::Screen::DeckEditor, "home: new deck opens the editor");

  // Type a search, click Search, then click the first result to add it.
  click(app, center(app.deckEditor().searchInput().bounds()));
  type(app, "Forest");
  click(app, center(app.deckEditor().searchButton().bounds()));
  check(!app.deckEditor().results().empty(), "editor: search finds cards");
  click(app, {app.deckEditor().resultsList().position().x + 30.f,
              app.deckEditor().resultsList().position().y +
                  (app.deckEditor().resultsList().rowHeight() / 2.f)});
  check(app.deckEditor().deckCards().size() == 1, "editor: clicking a result adds it to the deck");

  // Name the deck and save -> back on Home with the new deck listed.
  click(app, center(app.deckEditor().deckNameInput().bounds()));
  type(app, "My Deck");
  click(app, center(app.deckEditor().saveButton().bounds()));
  check(app.currentScreen() == mtgcpp::core::Screen::Home, "editor: save returns home");
  check(app.home().hasDecks(), "home: deck vault lists the saved deck");
}

void flowEditorImport(mtgcpp::core::App &app) {
  click(app, center(app.home().newDeckButton().bounds()));
  click(app, center(app.deckEditor().importButton().bounds()));
  check(app.deckEditor().isImportView(), "editor: import button opens the import view");

  // Paste an Arena export (Enter inserts a newline), then click Import.
  click(app, center(app.deckEditor().importArea().bounds()));
  type(app, "2 Grizzly Bears");
  app.injectEvent(keyDown(sf::Keyboard::Enter));
  type(app, "1 Lightning Bolt");
  click(app, center(app.deckEditor().importButton().bounds()));
  check(app.deckEditor().preview().has_value(), "editor: import parses into a preview");

  // Confirm the preview -> back in the build view with the imported cards.
  click(app, center(app.deckEditor().previewConfirmButton().bounds()));
  check(!app.deckEditor().isImportView(), "editor: confirming returns to the build view");
  check(app.deckEditor().totals().total_cards == 3, "editor: imported cards land in the deck");

  click(app, center(app.deckEditor().backButton().bounds()));
  check(app.currentScreen() == mtgcpp::core::Screen::Home, "editor: back returns home");
}

// Create a room on the host, choose a deck, drive a real loopback guest to a
// ready table, then play a few battlefield actions and assert they mirror.
void flowLobbyToTable(mtgcpp::core::App &app, const mtgcpp::core::Deck &deck) {
  using mtgcpp::core::PlayerSeat;
  const std::size_t host = static_cast<std::size_t>(PlayerSeat::Host);

  click(app, center(app.home().playButton().bounds()));
  check(app.currentScreen() == mtgcpp::core::Screen::Lobby, "lobby: play online opens the lobby");

  click(app, center(app.lobby().createButton().bounds()));
  const bool relayUp = waitApp(app, [&app] { return app.lobby().connected(); });
  check(relayUp, "lobby: create room starts the embedded relay");

  // Choose the seeded Warriors deck from the vault (the vault is newest-first,
  // so find its row by name and click that deck button).
  check(!app.lobby().decks().empty(), "lobby: deck picker lists the vault");
  std::size_t warriorsRow = 0;
  bool found = false;
  for (std::size_t i = 0; i < app.lobby().decks().size(); ++i) {
    if (app.lobby().decks().at(i).name == deck.name) {
      warriorsRow = i;
      found = true;
      break;
    }
  }
  check(found, "lobby: the seeded deck is in the vault");
  click(app, center(app.lobby().deckButtons().at(warriorsRow).bounds()));
  const bool announced = waitApp(app, [&app] { return app.lobby().myDeckName().has_value(); });
  check(announced, "lobby: choosing a deck announces it");

  // The guest joins over loopback, then chooses its deck.
  mtgcpp::net::Client guestClient;
  if (!guestClient.connect("127.0.0.1", "7500")) {
    check(false, "lobby: guest connects to the host relay");
    return;
  }
  mtgcpp::state::Session guest(guestClient);
  const bool joined = waitUntil([&guest] { return guest.role().has_value(); }, app, guest);
  check(joined, "lobby: guest learns its seat");
  guest.chooseDeck(deck);

  // Once both decks are known and the room is ready, the app auto-switches.
  const bool atTable =
      waitUntil([&app] { return app.currentScreen() == mtgcpp::core::Screen::Table; }, app, guest);
  check(atTable, "lobby: ready table transition fires");
  if (!atTable) {
    return;
  }

  // The table laid out the full window (960x600, margin 8) — recompute the
  // host's arched hand strip the same way the screen did.
  const sf::Vector2u win{960u, 600u};
  const float margin = mtgcpp::core::px(8.f, win);
  const sf::FloatRect content{margin, margin, static_cast<float>(win.x) - (2.f * margin),
                              static_cast<float>(win.y) - (2.f * margin)};
  const float gap = 6.f;
  const sf::FloatRect battlefield{content.left, content.top + 36.f + gap, content.width,
                                  content.height - 36.f - gap};
  const mtgcpp::core::TableBands bands = mtgcpp::core::tableBands(battlefield, gap, 0.44f);

  const std::size_t handSize = app.table().board().seats.at(host).hand.size();
  check(handSize >= 2, "table: host has a hand to interact with");
  const float amplitude = bands.myHand.height * 0.12f;
  const std::vector<sf::FloatRect> hand =
      mtgcpp::core::handCardRects(bands.myHand, handSize, amplitude, false);

  // Select the first card and tap it with the keyboard verb.
  click(app, center(hand.at(0)));
  check(app.table().selection().has_value(), "table: clicking a hand card selects it");
  pressKey(app, sf::Keyboard::T);
  check(waitUntil([&app] { return app.table().board().seats.at(host).hand.at(0).tapped; }, app,
                  guest),
        "table: T taps the selected card");
  check(waitUntil([&guest] { return guest.board().seats.at(host).hand.at(0).tapped; }, app, guest),
        "table: the tap mirrors to the guest");

  // Move the second card to the Stack; the only overlap fans out.
  const std::size_t before = app.table().board().stack.size();
  click(app, center(hand.at(1)));
  pressKey(app, sf::Keyboard::S);
  check(waitUntil([&app, before] { return app.table().board().stack.size() == before + 1; }, app,
                  guest),
        "table: S moves the card to the stack");
  check(
      waitUntil([&guest, before] { return guest.board().stack.size() == before + 1; }, app, guest),
      "table: the stack move mirrors to the guest");

  // Life: click the host ring, type 18, Enter.
  click(app, center(app.table().lifeRings().at(host)));
  check(app.table().lifeEditingSeat() == PlayerSeat::Host,
        "table: clicking the ring starts editing");
  app.injectEvent(keyDown(sf::Keyboard::Num1));
  app.injectEvent(keyDown(sf::Keyboard::Num8));
  app.injectEvent(keyDown(sf::Keyboard::Enter));
  app.pump();
  check(waitUntil([&app] { return app.table().board().life.at(host) == 18; }, app, guest),
        "table: life edits to 18");
  check(waitUntil([&guest] { return guest.board().life.at(host) == 18; }, app, guest),
        "table: the life edit mirrors to the guest");
}

} // namespace

int main() {
  try {
    TempDir dir;
    mtgcpp::core::DeckRepository repository(dir.path());
    mtgcpp::core::CardDatabase cards;
    loadFixture(cards);
    const mtgcpp::core::Deck warriors = repository.create(seedDeck());

    mtgcpp::core::App app("integration", repository, cards, dir.path());

    flowProfile(app);
    flowEditorBuildAndSave(app);
    flowEditorImport(app);
    flowLobbyToTable(app, warriors);
  } catch (const std::exception &exc) {
    check(false, "unexpected exception");
    std::cout << "  what: " << exc.what() << "\n";
  }

  if (failureCount() == 0) {
    std::cout << "\nINTEGRATION OK: all flows green\n";
    return 0;
  }
  std::cout << "\nINTEGRATION FAILED: " << failureCount() << " check(s) failed\n";
  return 1;
}
