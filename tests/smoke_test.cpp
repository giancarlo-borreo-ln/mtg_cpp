// M0.1 smoke tests: prove every toolchain dependency downloads, builds,
// links and runs, without needing a display. SFML is exercised through
// out-of-line symbols so the shared libraries are genuinely linked.

#include <SFML/Graphics.hpp>
#include <asio.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <string_view>

namespace {

// Out-of-line sfml-graphics symbol so the linker must resolve the .so.
sf::Image makeImage(unsigned width, unsigned height) {
  sf::Image image;
  image.create(width, height, sf::Color::Red);
  return image;
}

} // namespace

TEST(Smoke, NlohmannJsonRoundTrips) {
  const nlohmann::json doc = nlohmann::json::parse(
      R"({"event":"ready","room":"ABC12","from":"server","payload":{"players":["player_1"]}})",
      /*cb=*/nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/false);
  EXPECT_EQ(doc.at("event"), "ready");
  EXPECT_EQ(doc.at("room"), "ABC12");
  EXPECT_EQ(doc.at("from"), "server");
  EXPECT_EQ(doc.at("payload").at("players").at(0), "player_1");

  const nlohmann::json round_tripped = nlohmann::json::parse(doc.dump());
  EXPECT_EQ(round_tripped, doc);
}

TEST(Smoke, AsioStandaloneHeaderCompilesAndRuns) {
  asio::io_context io;
  asio::steady_timer timer(io, std::chrono::milliseconds(1));
  timer.async_wait([](const std::error_code &) {});
  io.run();

  // asio-1-30-2 exposes the version as the ASIO_VERSION macro (major*10000 + minor*100 + patch).
  EXPECT_GE(ASIO_VERSION, 103000);
}

TEST(Smoke, SfmlCoreTypesAndSharedLibraryLink) {
  const sf::Vector2u size{960u, 600u};
  EXPECT_EQ(size.x, 960u);
  EXPECT_EQ(size.y, 600u);

  const sf::Color background(0x12, 0x0a, 0x0a);
  EXPECT_EQ(background.r, 0x12);

  // Forces libsfml-graphics.so to be pulled in at link time.
  const sf::Image image = makeImage(4u, 4u);
  EXPECT_EQ(image.getSize().x, 4u);
  EXPECT_EQ(image.getSize().y, 4u);
}

TEST(Smoke, VersionMacroDefined) {
  const std::string_view version = MTG_CPP_VERSION;
  EXPECT_FALSE(version.empty());
}
