// Menu + table theme implementation (M4.1 + M9.1): palette constants, font
// loading, and the procedural Shandalar battlefield textures. The painters are
// pure per-pixel CPU work over an sf::Image (headless-testable); the
// buildXxxTexture helpers upload that image to a GPU texture (needs a context).

#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <system_error>

namespace mtgcpp::core {

namespace {

// Directory of the running executable, so bundled assets can be found no
// matter what the working directory is (e.g. double-clicking the binary).
std::filesystem::path executableDir() {
#ifdef _WIN32
  std::wstring buffer(32768, L'\0');
  const DWORD length =
      GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length > 0 && length < buffer.size()) {
    return std::filesystem::path(buffer).parent_path();
  }
  return {};
#else
  std::error_code ec;
  const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    return self.parent_path();
  }
  return {};
#endif
}

} // namespace

const MenuPalette &menuPalette() {
  // Function-local static: constructed on the first call (so construction can
  // never run during static initialization), alive for the whole process.
  // Values are the burgundy-black / parchment / gold family of the original
  // dark-red window, lightened for readability on menus.
  static const MenuPalette kPalette{
      .background = {0x12, 0x0a, 0x0a},  // deep burgundy-black
      .panel = {0x20, 0x14, 0x14},       // card/panel fill, one step lighter
      .parchment = {0xf2, 0xe9, 0xd0},   // warm off-white text
      .gold = {0xd8, 0xb4, 0x3f},        // titles / accents (bright enough for dark bg)
      .muted = {0xa2, 0x8f, 0x74},       // hints / secondary text (legible on dark)
      .hoverFill = {0xe3, 0xd3, 0xa8},   // gold-tinted parchment, hover/selected
      .pressedFill = {0xbf, 0xa8, 0x7c}, // darker parchment, pressed
      .ink = {0x5a, 0x42, 0x2a},         // deep warm brown, text on parchment
      .danger = {0xd9, 0x6a, 0x58},      // muted red, destructive actions
  };
  return kPalette;
}

std::filesystem::path assetPath(const std::string &relativePath) {
  // Prefer assets sitting next to the executable (works from any directory),
  // then fall back to <cwd>/assets (the source tree or the build tree, where
  // CMake copies the assets at configure time).
  std::filesystem::path nextToExe = executableDir() / "assets" / relativePath;
  if (std::filesystem::exists(nextToExe)) {
    return nextToExe;
  }
  return std::filesystem::current_path() / "assets" / relativePath;
}

std::filesystem::path menuFontPath() { return assetPath("fonts/PT_Serif-Web-Regular.ttf"); }

std::filesystem::path boldMenuFontPath() { return assetPath("fonts/PT_Serif-Web-Bold.ttf"); }

bool loadMenuFont(sf::Font &font) {
  // sf::Font::loadFromFile reads the TTF and parses it immediately; it does
  // not need a window or OpenGL context, so this is testable headless.
  return font.loadFromFile(menuFontPath().string());
}

bool loadBoldMenuFont(sf::Font &font) { return font.loadFromFile(boldMenuFontPath().string()); }

// ---------------------------------------------------------------------------
// Table theme (M9.1)
// ---------------------------------------------------------------------------

namespace {

// Linear interpolation between two colors (t in [0, 1]).
sf::Color mixColor(const sf::Color &a, const sf::Color &b, float t) {
  return {static_cast<sf::Uint8>(static_cast<float>(a.r) +
                                 ((static_cast<float>(b.r) - static_cast<float>(a.r)) * t)),
          static_cast<sf::Uint8>(static_cast<float>(a.g) +
                                 ((static_cast<float>(b.g) - static_cast<float>(a.g)) * t)),
          static_cast<sf::Uint8>(static_cast<float>(a.b) +
                                 ((static_cast<float>(b.b) - static_cast<float>(a.b)) * t)),
          static_cast<sf::Uint8>(static_cast<float>(a.a) +
                                 ((static_cast<float>(b.a) - static_cast<float>(a.a)) * t))};
}

// A deterministic per-pixel hash so the procedural noise is reproducible in
// tests (fixed seed) and stable across frames (no RNG state). Pairs well with
// the vig/integer operations below; cheap enough for a full-screen playmat.
unsigned hashMix(unsigned x, unsigned y, unsigned seed) {
  unsigned h = (x * 73856093u) ^ (y * 19349663u) ^ (seed * 83492791u);
  h = (h ^ (h >> 13)) * 1274126177u;
  h = h ^ (h >> 16);
  return h;
}

// Clamp a float to [0, 1] for color mixing.
float clamp01(float value) { return std::min(std::max(value, 0.f), 1.f); }

} // namespace

const TablePalette &tablePalette() {
  // The Shandalar battlefield family: deep velvet mat, parchment faces, gold
  // filigree. Built once on first use and shared by every screen.
  static const TablePalette kPalette{
      .velvet = {0x2a, 0x12, 0x14},       // deep burgundy velvet
      .velvetEdge = {0x0c, 0x05, 0x06},   // vignette edge (near-black)
      .parchment = {0xdc, 0xc9, 0xa0},    // warm parchment face
      .stone = {0x3d, 0x2c, 0x1e},        // beveled stone tile
      .stoneLight = {0x5a, 0x42, 0x2c},   // bevel top highlight
      .stoneDark = {0x22, 0x16, 0x0f},    // bevel bottom shadow
      .gold = {0xd8, 0xb4, 0x3f},         // filigree / borders
      .goldDim = {0x9a, 0x7e, 0x2a},      // muted gold (inner border)
      .ink = {0x3a, 0x2a, 0x18},          // text on parchment
      .parchmentDim = {0xbf, 0xab, 0x82}, // faint noise speckles
      .shadow = {0x00, 0x00, 0x00},       // card drop shadow
  };
  return kPalette;
}

void paintPlaymat(sf::Image &image, unsigned seed) {
  // The whole window's backdrop: velvet with a radial vignette (darker corners,
  // lighter center) and a fine deterministic parchment noise. Every channel is
  // computed from the center distance so the mat reads as a lit surface.
  const TablePalette &palette = tablePalette();
  const sf::Vector2u size = image.getSize();
  const float cx = static_cast<float>(size.x) / 2.f;
  const float cy = static_cast<float>(size.y) / 2.f;
  const float maxDist = std::sqrt((cx * cx) + (cy * cy)); // corner distance
  for (unsigned y = 0; y < size.y; ++y) {
    for (unsigned x = 0; x < size.x; ++x) {
      const float dx = (static_cast<float>(x) - cx) / maxDist;
      const float dy = (static_cast<float>(y) - cy) / maxDist;
      const float dist = std::sqrt((dx * dx) + (dy * dy)); // 0 center, ~1 corner
      // Vignette: bright at the center, dim toward the corners.
      const float light = 0.45f + ((1.f - clamp01(dist)) * 0.55f);
      const sf::Color base = mixColor(palette.velvet, palette.velvetEdge, clamp01(dist));
      // Deterministic noise: a few percent of random luminance variation.
      const float noise =
          ((static_cast<float>(hashMix(x, y, seed) & 0xffu) / 255.f) - 0.5f) * 0.05f;
      const float lum = clamp01(light + noise);
      image.setPixel(x, y,
                     {static_cast<sf::Uint8>(static_cast<float>(base.r) * lum),
                      static_cast<sf::Uint8>(static_cast<float>(base.g) * lum),
                      static_cast<sf::Uint8>(static_cast<float>(base.b) * lum), 255u});
    }
  }
}

void paintZoneTile(sf::Image &image, unsigned seed) {
  // A beveled stone/parchment zone tile: stone fill with a subtle deterministic
  // parchment tint, a lighter top bevel and darker bottom/right bevel, then a
  // thin gold inner border. Cards snap onto this mat (M9.2/M9.3).
  const TablePalette &palette = tablePalette();
  const sf::Vector2u size = image.getSize();
  for (unsigned y = 0; y < size.y; ++y) {
    for (unsigned x = 0; x < size.x; ++x) {
      const bool onTopBevel = y < 2;
      const bool onBottomBevel = y >= size.y - 2;
      const bool onLeftBevel = x < 2;
      const bool onRightBevel = x >= size.x - 2;
      const bool onBorder = onTopBevel || onBottomBevel || onLeftBevel || onRightBevel;
      const bool onInnerGold = (x == 3 || y == 3 || x == size.x - 4 || y == size.y - 4);
      sf::Color color = palette.stone;
      if (onBorder) {
        // Bevel shading: light catches the top/left, the bottom/right recede.
        color = (onTopBevel || onLeftBevel) ? palette.stoneLight : palette.stoneDark;
      } else if (onInnerGold) {
        color = palette.gold;
      } else {
        // Parchment tint variation so adjacent tiles do not look identical.
        const float tint =
            ((static_cast<float>(hashMix(x / 2, y / 2, seed) & 0x3fu) / 63.f) - 0.5f) * 0.06f;
        color = mixColor(palette.stone, palette.parchment, clamp01(0.25f + tint));
      }
      image.setPixel(x, y, color);
    }
  }
}

void paintCardBack(sf::Image &image) {
  // The ornate card back (used for face-down cards and until Sprint 10 loads
  // real art): a velvet field, a double gold filigree frame, corner studs and a
  // central diamond medallion. Everything is computed per-pixel from (x, y).
  const TablePalette &palette = tablePalette();
  const sf::Vector2u size = image.getSize();
  const float w = static_cast<float>(size.x);
  const float h = static_cast<float>(size.y);
  for (unsigned y = 0; y < size.y; ++y) {
    for (unsigned x = 0; x < size.x; ++x) {
      const float fx = static_cast<float>(x);
      const float fy = static_cast<float>(y);
      const float outer = 4.f; // outer gold frame thickness
      const float inner = 9.f; // inner gold line position
      const bool onOuter = fx < outer || fy < outer || fx >= w - outer || fy >= h - outer;
      const bool onInner = (std::abs(fx - inner) < 1.f || std::abs(fy - inner) < 1.f ||
                            std::abs(fx - (w - inner)) < 1.f || std::abs(fy - (h - inner)) < 1.f);
      // Central diamond medallion outline.
      const float cx = w / 2.f;
      const float cy = h / 2.f;
      const float halfDiag = std::min(w, h) * 0.22f;
      const bool onDiamond = std::abs(std::abs(fx - cx) + std::abs(fy - cy) - halfDiag) < 1.f;
      sf::Color color = palette.velvet;
      if (onOuter || onInner || onDiamond) {
        color = palette.gold;
      }
      image.setPixel(x, y, color);
    }
  }
}

void paintCardFront(sf::Image &image) {
  // The procedural card front (pre-art-cache): a parchment face with a gold
  // frame and a slightly darker name band across the top. The name/cost text
  // is drawn by the card view on top of this texture.
  const TablePalette &palette = tablePalette();
  const sf::Vector2u size = image.getSize();
  const float w = static_cast<float>(size.x);
  const float h = static_cast<float>(size.y);
  for (unsigned y = 0; y < size.y; ++y) {
    for (unsigned x = 0; x < size.x; ++x) {
      const float fx = static_cast<float>(x);
      const float fy = static_cast<float>(y);
      const bool onBorder = fx < 2.f || fy < 2.f || fx >= w - 2.f || fy >= h - 2.f;
      const bool onBand = fy < h * 0.24f; // name band near the top
      sf::Color color = onBand ? palette.parchmentDim : palette.parchment;
      if (onBorder) {
        color = palette.gold;
      }
      image.setPixel(x, y, color);
    }
  }
}

void paintTokenCard(sf::Image &image) {
  // Token placeholder: parchment with a dashed gold inner frame (the dashes
  // make it read as a token rather than a real card, matching the webapp's
  // "Token" placeholder). No text here — the view adds the token's name.
  const TablePalette &palette = tablePalette();
  const sf::Vector2u size = image.getSize();
  for (unsigned y = 0; y < size.y; ++y) {
    for (unsigned x = 0; x < size.x; ++x) {
      const bool onOuter = x < 2 || y < 2 || x >= size.x - 2 || y >= size.y - 2;
      const bool onInner = (x == 6 || y == 6 || x == size.x - 7 || y == size.y - 7);
      // Dash pattern along the inner frame: a dash every 8px.
      const unsigned perimeterStep = (x + y) % 16;
      const bool onDashed = onInner && perimeterStep < 8;
      sf::Color color = palette.parchment;
      if (onOuter) {
        color = palette.gold;
      } else if (onDashed) {
        color = palette.goldDim;
      }
      image.setPixel(x, y, color);
    }
  }
}

void paintLifeRing(sf::Image &image) {
  // A life-total ring: transparent outside, a gold annulus (with a top-left
  // highlight so it reads as lit metal), and a burgundy inner disc the number
  // is drawn on. The alpha channel makes the corner ring blend into the mat.
  const TablePalette &palette = tablePalette();
  const sf::Vector2u size = image.getSize();
  const float cx = (static_cast<float>(size.x) - 1.f) / 2.f;
  const float cy = (static_cast<float>(size.y) - 1.f) / 2.f;
  const float innerR = static_cast<float>(size.x) * 0.34f;
  const float outerR = static_cast<float>(size.x) * 0.5f;
  for (unsigned y = 0; y < size.y; ++y) {
    for (unsigned x = 0; x < size.x; ++x) {
      const float dx = static_cast<float>(x) - cx;
      const float dy = static_cast<float>(y) - cy;
      const float dist = std::sqrt((dx * dx) + (dy * dy));
      if (dist <= innerR) {
        image.setPixel(x, y, palette.velvet); // inner disc (life number area)
      } else if (dist <= outerR) {
        // Gold ring with a highlight toward the top-left light source.
        const float highlight = clamp01(0.5f - ((dx + dy) / (2.f * outerR)));
        image.setPixel(x, y, mixColor(palette.gold, sf::Color(0xff, 0xea, 0xb0), highlight));
      } else {
        image.setPixel(x, y, sf::Color::Transparent);
      }
    }
  }
}

bool buildTexture(sf::Texture &out, const sf::Image &image) {
  // loadFromImage uploads the CPU-side pixels to the GPU; this is the only
  // step that needs a valid OpenGL context (fine in-app and in tests here).
  return out.loadFromImage(image);
}

bool buildPlaymatTexture(sf::Texture &out, sf::Vector2u size) {
  sf::Image image;
  image.create(size.x, size.y);
  paintPlaymat(image, 0);
  return buildTexture(out, image);
}

bool buildZoneTileTexture(sf::Texture &out, unsigned size) {
  sf::Image image;
  image.create(size, size);
  paintZoneTile(image, 0);
  return buildTexture(out, image);
}

bool buildCardBackTexture(sf::Texture &out, sf::Vector2u size) {
  sf::Image image;
  image.create(size.x, size.y);
  paintCardBack(image);
  return buildTexture(out, image);
}

bool buildCardFrontTexture(sf::Texture &out, sf::Vector2u size) {
  sf::Image image;
  image.create(size.x, size.y);
  paintCardFront(image);
  return buildTexture(out, image);
}

bool buildTokenTexture(sf::Texture &out, sf::Vector2u size) {
  sf::Image image;
  image.create(size.x, size.y);
  paintTokenCard(image);
  return buildTexture(out, image);
}

bool buildLifeRingTexture(sf::Texture &out, unsigned diameter) {
  sf::Image image;
  image.create(diameter, diameter);
  paintLifeRing(image);
  return buildTexture(out, image);
}

} // namespace mtgcpp::core
