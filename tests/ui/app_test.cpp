// M4.1/M4.3 UI shell tests: screen metadata, the App router, menu palette and
// the bundled font.
//
// The real window needs a display, so everything here is headless-safe:
//   * screen names / placeholders are pure string data,
//   * the App router (switchTo) touches only members, never the window,
//   * the palette is plain color data,
//   * loading an sf::Font uses FreeType, not OpenGL — no display required.
// The windowed render itself is verified by launching the app locally.

#include "core/card_database.h"
#include "store/deck_repository.h"
#include "ui/app.h"
#include "ui/theme.h"

#include <gtest/gtest.h>

#include <SFML/Graphics.hpp>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>

namespace mtgcpp::core {
namespace {

// A throwaway app-data dir removed on destruction, so the App never touches
// real user data (decks, profile) during tests.
class TempDir {
public:
  TempDir() {
    const std::string unique =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = std::filesystem::temp_directory_path() / ("mtgcpp_app_" + unique);
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

TEST(Screen, NamesEveryScreen) {
  EXPECT_STREQ(screenName(Screen::Home), "Home");
  EXPECT_STREQ(screenName(Screen::DeckEditor), "Deck Editor");
  EXPECT_STREQ(screenName(Screen::Lobby), "Lobby");
  EXPECT_STREQ(screenName(Screen::Table), "Table");
}

TEST(Screen, EveryScreenHasANonEmptyPlaceholder) {
  const std::array screens{Screen::Home, Screen::DeckEditor, Screen::Lobby, Screen::Table};
  for (const Screen screen : screens) {
    EXPECT_FALSE(std::string(screenPlaceholder(screen)).empty()) << screenName(screen);
  }
}

TEST(App, StartsOnHomeAndRoutesThroughEveryScreen) {
  TempDir dir;
  DeckRepository repository(dir.path());
  CardDatabase cards; // hermetic test: an empty local card database is fine
  App app("test", repository, cards, dir.path());

  EXPECT_EQ(app.currentScreen(), Screen::Home);
  EXPECT_TRUE(app.fontLoaded());

  app.switchTo(Screen::Table);
  EXPECT_EQ(app.currentScreen(), Screen::Table);

  app.switchTo(Screen::Lobby);
  EXPECT_EQ(app.currentScreen(), Screen::Lobby);

  app.switchTo(Screen::DeckEditor);
  EXPECT_EQ(app.currentScreen(), Screen::DeckEditor);

  app.switchTo(Screen::Home);
  EXPECT_EQ(app.currentScreen(), Screen::Home);
}

TEST(App, ReportsTheCardDatabaseSizeBackToTheEditor) {
  TempDir dir;
  DeckRepository repository(dir.path());
  CardDatabase cards;
  std::istringstream in(
      "{\"id\":\"i\",\"name\":\"Forest\",\"set\":\"war\",\"set_name\":\"War of the "
      "Spark\",\"collector_number\":\"263\",\"type_line\":\"Basic Land\",\"image_uris\":{\"png\":"
      "\"https://img/f.png\"}}\n");
  cards.load(in);

  App app("test", repository, cards, dir.path());
  EXPECT_EQ(app.cardDatabaseSize(), 1u);

  app.switchTo(Screen::DeckEditor);
  EXPECT_EQ(app.currentScreen(), Screen::DeckEditor);
}

// --- M12.1 scripting seam (injectEvent / pump) ------------------------------

sf::Event mouseClickAt(bool pressed, float x, float y) {
  sf::Event event;
  event.type = pressed ? sf::Event::MouseButtonPressed : sf::Event::MouseButtonReleased;
  event.mouseButton.button = sf::Mouse::Left;
  event.mouseButton.x = static_cast<int>(x);
  event.mouseButton.y = static_cast<int>(y);
  return event;
}

TEST(App, InjectEventClicksAWidgetAndPumpFiresItsAction) {
  // A synthetic mouse press/release through injectEvent + pump must drive the
  // exact same path the window loop uses: clicking a profile card fires the
  // SelectProfile action and persists the pick.
  TempDir dir;
  DeckRepository repository(dir.path());
  CardDatabase cards;
  App app("test", repository, cards, dir.path());

  EXPECT_TRUE(app.home().isProfilePicker());
  const sf::FloatRect button = app.home().profileButton(0).bounds();
  const float x = button.left + (button.width / 2.f);
  const float y = button.top + (button.height / 2.f);
  EXPECT_TRUE(app.injectEvent(mouseClickAt(true, x, y)));
  EXPECT_TRUE(app.injectEvent(mouseClickAt(false, x, y)));
  app.pump();

  EXPECT_TRUE(app.home().playerId().has_value());
  EXPECT_FALSE(app.home().isProfilePicker());
}

TEST(App, InjectEventRoutesThroughTheActiveScreen) {
  // The seam routes to whichever screen is active: the same click that selects
  // a profile on Home is a no-op once a screen that does not own that widget is
  // active (the Deck Editor's Back button does not exist on Home).
  TempDir dir;
  DeckRepository repository(dir.path());
  CardDatabase cards;
  App app("test", repository, cards, dir.path());

  app.switchTo(Screen::DeckEditor);
  const sf::FloatRect button = app.deckEditor().backButton().bounds();
  const float x = button.left + (button.width / 2.f);
  const float y = button.top + (button.height / 2.f);
  EXPECT_TRUE(app.injectEvent(mouseClickAt(true, x, y)));
  EXPECT_TRUE(app.injectEvent(mouseClickAt(false, x, y)));
  app.pump();
  // The Back button took us Home.
  EXPECT_EQ(app.currentScreen(), Screen::Home);
}

TEST(App, InjectEventReportsAQuitRequest) {
  // Closed and an unconsumed Esc ask the app to quit: injectEvent returns
  // false, so a driver knows when the window loop would have stopped.
  TempDir dir;
  DeckRepository repository(dir.path());
  CardDatabase cards;
  App app("test", repository, cards, dir.path());

  sf::Event closed;
  closed.type = sf::Event::Closed;
  EXPECT_FALSE(app.injectEvent(closed));

  App app2("test", repository, cards, dir.path());
  sf::Event esc;
  esc.type = sf::Event::KeyPressed;
  esc.key.code = sf::Keyboard::Escape;
  EXPECT_FALSE(app2.injectEvent(esc)); // nothing on Home consumes Esc -> quit
}

TEST(Assets, MenuFontFileExistsInTheBundledAssets) {
  EXPECT_TRUE(std::filesystem::exists(menuFontPath()));
}

TEST(Assets, BoldMenuFontFileExistsInTheBundledAssets) {
  EXPECT_TRUE(std::filesystem::exists(boldMenuFontPath()));
}

TEST(MenuFont, LoadsTheBundledOflFont) {
  sf::Font font;
  if (loadMenuFont(font)) {
    // A loaded FreeType font always reports a family; if this is empty the
    // file parsed as a font with no name table, which we do not ship.
    EXPECT_FALSE(font.getInfo().family.empty());
  } else {
    FAIL() << "expected the bundled menu font to load from " << menuFontPath();
  }
}

TEST(MenuFont, LoadsTheBundledBoldOflFont) {
  sf::Font font;
  if (loadBoldMenuFont(font)) {
    EXPECT_FALSE(font.getInfo().family.empty());
  } else {
    FAIL() << "expected the bundled bold menu font to load from " << boldMenuFontPath();
  }
}

TEST(MenuPalette, ColorsAreTheExpectedShandalarValues) {
  const MenuPalette &palette = menuPalette();

  EXPECT_EQ(palette.background, sf::Color(0x12, 0x0a, 0x0a));
  EXPECT_EQ(palette.panel, sf::Color(0x20, 0x14, 0x14));
  EXPECT_EQ(palette.parchment, sf::Color(0xf2, 0xe9, 0xd0));
  EXPECT_EQ(palette.gold, sf::Color(0xd8, 0xb4, 0x3f));
  EXPECT_EQ(palette.muted, sf::Color(0xa2, 0x8f, 0x74));
  EXPECT_EQ(palette.hoverFill, sf::Color(0xe3, 0xd3, 0xa8));
  EXPECT_EQ(palette.pressedFill, sf::Color(0xbf, 0xa8, 0x7c));
  EXPECT_EQ(palette.ink, sf::Color(0x5a, 0x42, 0x2a));
  EXPECT_EQ(palette.danger, sf::Color(0xd9, 0x6a, 0x58));
}

TEST(MenuPalette, TextColorsStayDistinctFromTheBackground) {
  const MenuPalette &palette = menuPalette();

  // Text and widget fills must never blend into the window fill, or menus
  // become unreadable.
  EXPECT_NE(palette.parchment, palette.background);
  EXPECT_NE(palette.gold, palette.background);
  EXPECT_NE(palette.muted, palette.background);
  EXPECT_NE(palette.panel, palette.background);
  EXPECT_NE(palette.hoverFill, palette.background);
  EXPECT_NE(palette.pressedFill, palette.background);
  EXPECT_NE(palette.ink, palette.background);
  EXPECT_NE(palette.danger, palette.background);
}

} // namespace
} // namespace mtgcpp::core
