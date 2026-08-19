// Menu theme: shared palette constants + the bundled OFL menu font (M4.1).
//
// The owner decision in §1b says menus stay clean and functional — at most a
// subtle Shandalar palette — while the ornate velvet/gold battlefield arrives
// with the table screen (M9.1). So this milestone ships only:
//   * a handful of shared colors, so screens never hard-code hex values, and
//   * the one bundled font every screen draws text with.
//
// SFML is a "bring your own assets" library: it has no built-in font, so we
// bundle an OFL-licensed TrueType font under assets/fonts/ (see OFL.txt). A
// missing font must degrade, never crash: the window, routing and Esc-to-quit
// still work, just without readable text.
#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>

#include <filesystem>
#include <string>

namespace mtgcpp::core {

// The menu palette. Each value is an sf::Color = four bytes (red, green, blue,
// alpha); sf::Color is just a plain data struct, safe to use anywhere and to
// compare for equality in tests.
struct MenuPalette {
  sf::Color background;  // the whole window before anything is drawn
  sf::Color panel;       // card/panel fill, slightly lighter than the window
  sf::Color parchment;   // warm off-white, primary text
  sf::Color gold;        // titles / accents
  sf::Color muted;       // secondary text / hints
  sf::Color hoverFill;   // parchment tinted toward gold (hover / selection)
  sf::Color pressedFill; // darker parchment (pressed)
  sf::Color ink;         // deep warm brown, readable text on parchment fills
  sf::Color danger;      // muted red, destructive actions (delete)
};

// The single shared menu palette. A function-local static is built on first
// call and lives for the process; returning by const ref means zero copies.
const MenuPalette &menuPalette();

// Resolve a bundled asset path relative to the working directory's `assets/`
// directory. The app is launched from the source tree or the build tree (CMake
// copies assets/ into the build tree at configure time), so a relative lookup
// finds the font in both. Packaging (M11.2) will resolve next to the
// executable instead of the working directory.
std::filesystem::path assetPath(const std::string &relativePath);

// Path of the bundled menu font: `assets/fonts/PT_Serif-Web-Regular.ttf`
// (ParaType PT Serif, SIL OFL — clean and legible on screen).
std::filesystem::path menuFontPath();

// Path of the bundled bold menu face used for titles and buttons.
std::filesystem::path boldMenuFontPath();

// Load the bundled menu font into `font`. Returns false when the file is
// missing or SFML cannot parse it; callers degrade gracefully instead of
// aborting. Loading straight into the caller's font avoids any copying (an
// sf::Font owns the parsed glyph data, and SFML 2.6 only copy-constructs it).
bool loadMenuFont(sf::Font &font);

// Same, for the bold face.
bool loadBoldMenuFont(sf::Font &font);

// ---------------------------------------------------------------------------
// Table theme (M9.1) — the Shandalar battlefield look.
// ---------------------------------------------------------------------------
// The battlefield is fully procedural: every texture is painted into an
// sf::Image once and uploaded to an sf::Texture, so the app ships zero raster
// assets (the "no binary asset bloat" rule). The painters are pure CPU work —
// no window, no OpenGL context — so they are unit-testable headless; the
// upload functions (buildXxxTexture) are the only context-dependent part.
//
// The look: a velvet/burgundy playmat with a vignette + parchment noise, gold
// filigree borders, beveled stone/parchment zone tiles, and an ornate card
// back — faithful to the 1990s Shandalar table (§1b).

// The battlefield palette: deep velvet for the mat, parchment for card/zone
// faces, gold for filigree, ink for text on parchment.
struct TablePalette {
  sf::Color velvet;       // playmat base (deep burgundy)
  sf::Color velvetEdge;   // playmat vignette edge (near-black burgundy)
  sf::Color parchment;    // card/zone face
  sf::Color stone;        // beveled zone tile fill
  sf::Color stoneLight;   // bevel top highlight
  sf::Color stoneDark;    // bevel bottom shadow
  sf::Color gold;         // filigree / borders
  sf::Color goldDim;      // muted gold (inner border)
  sf::Color ink;          // text on parchment
  sf::Color parchmentDim; // faint parchment noise speckles
  sf::Color shadow;       // card drop shadow
};

// The single shared table palette (function-local static, built on first use).
const TablePalette &tablePalette();

// --- Pure pixel painters (headless-testable) --------------------------------

// Paint the velvet playmat with a vignette + deterministic parchment noise
// over the WHOLE image. `seed` varies the noise pattern (tests pin 0).
void paintPlaymat(sf::Image &image, unsigned seed);

// Paint a beveled stone/parchment zone tile over the whole image.
void paintZoneTile(sf::Image &image, unsigned seed);

// Paint an ornate card back over the whole image.
void paintCardBack(sf::Image &image);

// Paint a parchment card front over the whole image (real cards pre-art-cache).
void paintCardFront(sf::Image &image);

// Paint a token placeholder card (parchment with a dashed gold frame).
void paintTokenCard(sf::Image &image);

// Paint a gold life ring into `image`: transparent everywhere except a gold
// ring annulus with a burgundy inner disc (M9.5 life totals).
void paintLifeRing(sf::Image &image);

// Paint the app icon (M11.2): a velvet field with a gold frame and a gold "MTG"
// monogram, so the window/taskbar icon is procedural like everything else.
void paintAppIcon(sf::Image &image);

// --- Texture uploads (need an OpenGL context; return false on failure) ------

// Upload an image to a texture (the only step that needs a GL context).
bool buildTexture(sf::Texture &out, const sf::Image &image);

// Convenience builders: paint then upload a texture of the given size.
bool buildPlaymatTexture(sf::Texture &out, sf::Vector2u size);
bool buildZoneTileTexture(sf::Texture &out, unsigned size);
bool buildCardBackTexture(sf::Texture &out, sf::Vector2u size);
bool buildCardFrontTexture(sf::Texture &out, sf::Vector2u size);
bool buildTokenTexture(sf::Texture &out, sf::Vector2u size);
bool buildLifeRingTexture(sf::Texture &out, unsigned diameter);

} // namespace mtgcpp::core
