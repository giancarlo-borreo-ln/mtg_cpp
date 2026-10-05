// M10.1 art-cache tests: disk hit/miss/persist, worker-thread downloads via an
// injected fake fetcher (no network), and the TableScreen fallback path — the
// procedural front texture is used while the cache is absent, disabled, or the
// art has not arrived yet.

#include "ui/art_cache.h"
#include "ui/screens/table_screen.h"

#include <gtest/gtest.h>

#include <SFML/Graphics.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mtgcpp::core {
namespace {

using namespace std::chrono_literals;

// --- Fixtures ---------------------------------------------------------------
// RAII temp dir (the other test files use the same pattern).
class TempDir {
public:
  TempDir() {
    // Unique per construction (the other test files use the same pattern):
    // parallel ctest processes each run this code, so a shared counter would
    // collide across processes and make the suites flaky under -j.
    const std::string unique =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    root_ = std::filesystem::temp_directory_path() / ("mtgcpp_art_" + unique);
    std::filesystem::create_directories(root_);
  }
  ~TempDir() { std::filesystem::remove_all(root_); }
  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;
  TempDir(TempDir &&) = delete;
  TempDir &operator=(TempDir &&) = delete;
  std::filesystem::path path() const { return root_; }

private:
  std::filesystem::path root_;
};

// Encode a solid-color image as a PNG byte buffer (the fake fetcher's payload).
std::vector<std::byte> pngBytes(sf::Color color) {
  sf::Image image;
  image.create(64u, 64u, color);
  std::vector<sf::Uint8> png;
  if (!image.saveToMemory(png, "png")) {
    return {};
  }
  std::vector<std::byte> bytes(png.size());
  std::memcpy(bytes.data(), png.data(), png.size());
  return bytes;
}

// clang-tidy does not model ASSERT_* as a guard for optional access; use this
// helper instead (the same pattern as table_screen_test.cpp).
template <typename T> const T &expectValue(const std::optional<T> &opt) {
  if (opt.has_value()) {
    return opt.value();
  }
  ADD_FAILURE() << "expected an optional value";
  static const T empty{};
  return empty;
}

// Pump the cache (main-thread drain) until `predicate` holds or the deadline
// passes; fails the test on timeout.
template <typename Predicate>
void pumpUntil(ArtCache &cache, Predicate predicate, std::string_view what) {
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    cache.pump();
    if (predicate()) {
      return;
    }
    std::this_thread::sleep_for(1ms);
  }
  FAIL() << "timed out waiting for: " << what;
}

// The fixed art size every test cache uses (a function so no namespace-scope
// object does throwing dynamic init; sf::Vector2u is not constexpr-friendly).
sf::Vector2u artSize() { return {240u, 335u}; }

// --- downscaleImage ----------------------------------------------------------

TEST(DownscaleImage, MatchingSizeIsUnchanged) {
  sf::Image image;
  image.create(240u, 335u, sf::Color::Red);
  const sf::Image scaled = downscaleImage(image, {240u, 335u});
  EXPECT_EQ(scaled.getSize().x, 240u);
  EXPECT_EQ(scaled.getSize().y, 335u);
  EXPECT_EQ(scaled.getPixel(0u, 0u), sf::Color::Red);
}

TEST(DownscaleImage, DownscalesAndKeepsSolidColor) {
  sf::Image image;
  image.create(64u, 64u, sf::Color::Blue);
  const sf::Image scaled = downscaleImage(image, {32u, 32u});
  EXPECT_EQ(scaled.getSize().x, 32u);
  EXPECT_EQ(scaled.getSize().y, 32u);
  EXPECT_EQ(scaled.getPixel(16u, 16u), sf::Color::Blue);
}

TEST(DownscaleImage, KeepsTheHalfSplitBoundary) {
  sf::Image image;
  image.create(64u, 16u, sf::Color::Black);
  for (unsigned x = 32u; x < 64u; ++x) {
    for (unsigned y = 0u; y < 16u; ++y) {
      image.setPixel(x, y, sf::Color::White);
    }
  }
  const sf::Image scaled = downscaleImage(image, {16u, 4u});
  EXPECT_EQ(scaled.getPixel(0u, 0u), sf::Color::Black);
  EXPECT_EQ(scaled.getPixel(15u, 0u), sf::Color::White);
}

TEST(DownscaleImage, ZeroSizeReturnsSource) {
  sf::Image image;
  image.create(64u, 64u, sf::Color::Green);
  const sf::Image scaled = downscaleImage(image, {0u, 0u});
  EXPECT_EQ(scaled.getSize().x, 64u);
  EXPECT_EQ(scaled.getSize().y, 64u);
}

// --- artCachePath ------------------------------------------------------------

TEST(ArtCachePath, KeyedByScryfallIdAndSize) {
  const std::filesystem::path dir{"/cache"};
  EXPECT_EQ(artCachePath(dir, "abc-123", {240u, 335u}), dir / "abc-123_240x335.png");
}

TEST(ArtCachePath, SanitizesUnsafeCharacters) {
  const std::filesystem::path dir{"/cache"};
  EXPECT_EQ(artCachePath(dir, "a/b\\c:d?e*", {10u, 10u}), dir / "abcde_10x10.png");
}

// --- ArtCache: hit / miss / persist -----------------------------------------

TEST(ArtCache, MissReturnsNulloptWhenNothingCached) {
  TempDir tmp;
  ArtCache cache(tmp.path() / "art_cache", artSize());
  cache.setFetcher(
      [](const std::string &) -> std::optional<std::vector<std::byte>> { return std::nullopt; });
  EXPECT_FALSE(cache.imageFor("does-not-exist").has_value());
  EXPECT_EQ(cache.diskEntryCount(), 0u);
}

TEST(ArtCache, HitServesTheDownloadedArt) {
  TempDir tmp;
  ArtCache cache(tmp.path() / "art_cache", artSize());
  cache.setFetcher([](const std::string &url) {
    EXPECT_FALSE(url.empty());
    return std::make_optional(pngBytes(sf::Color::Red));
  });
  cache.request("aaa-111", "https://example.invalid/a.png");
  std::optional<sf::Image> art;
  pumpUntil(
      cache,
      [&] {
        art = cache.imageFor("aaa-111");
        return art.has_value();
      },
      "art downloaded");
  const sf::Image &image = expectValue(art);
  EXPECT_EQ(image.getSize().x, artSize().x);
  EXPECT_EQ(image.getSize().y, artSize().y);
  EXPECT_EQ(image.getPixel(artSize().x / 2u, artSize().y / 2u), sf::Color::Red);
}

TEST(ArtCache, CoalescesDuplicateRequests) {
  TempDir tmp;
  ArtCache cache(tmp.path() / "art_cache", artSize());
  std::atomic<int> calls{0};
  cache.setFetcher([&calls](const std::string &) {
    ++calls;
    return std::make_optional(pngBytes(sf::Color::Red));
  });
  cache.request("bbb-222", "https://example.invalid/b.png");
  cache.request("bbb-222", "https://example.invalid/b.png");
  pumpUntil(cache, [&] { return cache.imageFor("bbb-222").has_value(); }, "art downloaded");
  EXPECT_EQ(calls.load(), 1);
}

TEST(ArtCache, PersistsAcrossCacheInstances) {
  TempDir tmp;
  const std::filesystem::path cacheDir = tmp.path() / "art_cache";
  {
    ArtCache cache(cacheDir, artSize());
    cache.setFetcher(
        [](const std::string &) { return std::make_optional(pngBytes(sf::Color::Green)); });
    cache.request("ccc-333", "https://example.invalid/c.png");
    pumpUntil(cache, [&] { return cache.imageFor("ccc-333").has_value(); }, "art downloaded");
    // The PNG is written before the result is queued, so a hit implies a file.
    EXPECT_EQ(cache.diskEntryCount(), 1u);
  }
  // A fresh cache (different instance) with a *failing* fetcher must still serve
  // the art from disk — the real "offline but previously cached" case.
  ArtCache cache2(cacheDir, artSize());
  cache2.setFetcher(
      [](const std::string &) -> std::optional<std::vector<std::byte>> { return std::nullopt; });
  const std::optional<sf::Image> art = cache2.imageFor("ccc-333");
  const sf::Image &image = expectValue(art);
  EXPECT_EQ(image.getSize().x, artSize().x);
  EXPECT_EQ(image.getPixel(32u, 32u), sf::Color::Green);
}

TEST(ArtCache, FailureDrainsAndStaysMissing) {
  TempDir tmp;
  ArtCache cache(tmp.path() / "art_cache", artSize());
  cache.setFetcher(
      [](const std::string &) -> std::optional<std::vector<std::byte>> { return std::nullopt; });
  cache.request("ddd-444", "https://example.invalid/d.png");
  pumpUntil(cache, [&] { return cache.pendingRequests() == 0u; }, "failed job settles");
  EXPECT_FALSE(cache.imageFor("ddd-444").has_value());
  EXPECT_EQ(cache.diskEntryCount(), 0u);
  // A failed job is retryable: requesting again re-enqueues (and still fails).
  cache.request("ddd-444", "https://example.invalid/d.png");
  pumpUntil(cache, [&] { return cache.pendingRequests() == 0u; }, "retried job settles");
  EXPECT_FALSE(cache.imageFor("ddd-444").has_value());
}

TEST(ArtCache, CorruptCacheFileDegradesToMiss) {
  TempDir tmp;
  const std::filesystem::path cacheDir = tmp.path() / "art_cache";
  std::filesystem::create_directories(cacheDir);
  ArtCache cache(cacheDir, artSize());
  // A torn/foreign file at the expected path must never crash or be trusted.
  const std::filesystem::path path = artCachePath(cacheDir, "eee-555", artSize());
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "this is not a png";
  }
  EXPECT_FALSE(cache.imageFor("eee-555").has_value());
}

TEST(ArtCache, DisabledNeverFetchesNorServes) {
  TempDir tmp;
  ArtCache cache(tmp.path() / "art_cache", artSize());
  std::atomic<int> calls{0};
  cache.setFetcher([&calls](const std::string &) {
    ++calls;
    return std::make_optional(pngBytes(sf::Color::Red));
  });
  cache.setEnabled(false);
  cache.request("fff-666", "https://example.invalid/f.png");
  cache.pump();
  EXPECT_FALSE(cache.imageFor("fff-666").has_value());
  EXPECT_EQ(calls.load(), 0);
  EXPECT_EQ(cache.diskEntryCount(), 0u);
}

TEST(ArtCache, DisabledIgnoresDiskHits) {
  TempDir tmp;
  const std::filesystem::path cacheDir = tmp.path() / "art_cache";
  {
    ArtCache cache(cacheDir, artSize());
    cache.setFetcher(
        [](const std::string &) { return std::make_optional(pngBytes(sf::Color::Red)); });
    cache.request("ggg-777", "https://example.invalid/g.png");
    pumpUntil(cache, [&] { return cache.imageFor("ggg-777").has_value(); }, "art downloaded");
    EXPECT_EQ(cache.diskEntryCount(), 1u);
  }
  ArtCache cache2(cacheDir, artSize());
  cache2.setEnabled(false);
  EXPECT_FALSE(cache2.imageFor("ggg-777").has_value());
}

// --- TableScreen fallback path ----------------------------------------------

BoardCard artCard() {
  BoardCard card;
  card.id = "host-1";
  card.scryfall_id = "hhh-888";
  card.image_url = "https://example.invalid/h.png";
  card.name = "Bear";
  card.mana_cost = "{G}";
  return card;
}

state::BoardState handBoard() {
  state::BoardState board = state::initialBoardState();
  SeatBoard seat = emptySeatBoard();
  seat.hand.push_back(artCard());
  board.seats.at(0) = seat;
  return board;
}

TEST(TableScreenArt, NoCacheRendersProceduralFront) {
  TableScreen table;
  table.setBoard(handBoard());
  table.setRole(PlayerSeat::Host);
  table.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);
  // No cache attached: the art lookup is nullptr, so draw() uses frontTex_.
  EXPECT_EQ(table.artTextureFor(artCard()), nullptr);
  table.pumpArt(); // must be a safe no-op
}

TEST(TableScreenArt, DisabledCacheFallsBack) {
  TempDir tmp;
  ArtCache cache(tmp.path() / "art_cache", artSize());
  cache.setEnabled(false);
  TableScreen table;
  table.setArtCache(&cache);
  table.setBoard(handBoard());
  table.setRole(PlayerSeat::Host);
  table.relayout({0.f, 0.f, 960.f, 600.f}, 1.f);
  table.pumpArt();
  EXPECT_EQ(table.artTextureFor(artCard()), nullptr);
}

TEST(TableScreenArt, EnabledCacheReplacesFallback) {
  TempDir tmp;
  ArtCache cache(tmp.path() / "art_cache", artSize());
  cache.setFetcher(
      [](const std::string &) { return std::make_optional(pngBytes(sf::Color::Red)); });
  TableScreen table;
  table.setArtCache(&cache);
  table.setBoard(handBoard());
  table.setRole(PlayerSeat::Host);
  table.relayout({0.f, 0.f, 960.f, 600.f}, 1.f); // enqueues the download
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + 5s;
  while (table.artTextureFor(artCard()) == nullptr && std::chrono::steady_clock::now() < deadline) {
    table.pumpArt(); // drain the cache + build the texture on the main thread
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_NE(table.artTextureFor(artCard()), nullptr);
  EXPECT_EQ(table.artTextureFor(artCard())->getSize().x, artSize().x);
  EXPECT_EQ(table.artTextureFor(artCard())->getSize().y, artSize().y);
}

} // namespace
} // namespace mtgcpp::core
