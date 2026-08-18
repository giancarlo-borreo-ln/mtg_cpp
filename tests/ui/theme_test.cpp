// M9.1 theme tests: the procedural Shandalar table textures.
//
// The pixel painters are pure CPU work, so their sizes + sampled colors are
// fully testable headless; the upload path (buildTexture) needs an OpenGL
// context, which exists in the test environment, so it is exercised here too
// (a real texture with the right size + pixels).

#include "ui/theme.h"

#include <gtest/gtest.h>

#include <SFML/Graphics.hpp>

namespace mtgcpp::core {
namespace {

// Sample helpers -------------------------------------------------------------

sf::Color at(const sf::Image &image, unsigned x, unsigned y) { return image.getPixel(x, y); }

// M9.1: palette + painters ---------------------------------------------------

TEST(TablePalette, ProvidesTheShandalarFamily) {
  const TablePalette &p = tablePalette();
  EXPECT_EQ(p.velvet, sf::Color(0x2a, 0x12, 0x14));
  EXPECT_EQ(p.parchment, sf::Color(0xdc, 0xc9, 0xa0));
  EXPECT_EQ(p.gold, sf::Color(0xd8, 0xb4, 0x3f));
  // The mat must be dark and the parchment light: readable faces on velvet.
  EXPECT_LT(p.velvet.r, p.parchment.r);
}

TEST(PaintPlaymat, CoversTheWholeImage) {
  sf::Image image;
  image.create(128u, 96u);
  paintPlaymat(image, 0);
  EXPECT_EQ(image.getSize().x, 128u);
  EXPECT_EQ(image.getSize().y, 96u);
}

TEST(PaintPlaymat, VignetteDimsTheCorners) {
  sf::Image image;
  image.create(256u, 256u);
  paintPlaymat(image, 0);
  // The center is brighter (higher red) than the corner: the vignette darkens
  // the edges toward velvetEdge.
  const sf::Color center = at(image, 128u, 128u);
  const sf::Color corner = at(image, 0u, 0u);
  EXPECT_GT(center.r, corner.r);
  EXPECT_GT(center.g, corner.g);
}

TEST(PaintPlaymat, DeterministicForAFixedSeed) {
  sf::Image a;
  sf::Image b;
  a.create(128u, 128u);
  b.create(128u, 128u);
  paintPlaymat(a, 7);
  paintPlaymat(b, 7);
  EXPECT_EQ(at(a, 64u, 64u), at(b, 64u, 64u));
  EXPECT_EQ(at(a, 10u, 100u), at(b, 10u, 100u));
}

TEST(PaintZoneTile, BevelsAndGoldInnerBorder) {
  sf::Image image;
  image.create(64u, 64u);
  paintZoneTile(image, 0);
  // Top bevel is the light stone; the inner gold line sits at inset 3.
  EXPECT_EQ(at(image, 0u, 0u), tablePalette().stoneLight);
  EXPECT_EQ(at(image, 3u, 3u), tablePalette().gold);
  // The interior is a stone-family tone, not the border gold.
  const sf::Color interior = at(image, 32u, 40u);
  EXPECT_NE(interior, tablePalette().gold);
}

TEST(PaintCardBack, GoldFrameOnVelvet) {
  sf::Image image;
  image.create(64u, 88u);
  paintCardBack(image);
  EXPECT_EQ(at(image, 0u, 0u), tablePalette().gold);     // outer frame
  EXPECT_EQ(at(image, 32u, 44u), tablePalette().velvet); // field center
}

TEST(PaintCardFront, ParchmentFaceWithGoldFrameAndBand) {
  sf::Image image;
  image.create(64u, 88u);
  paintCardFront(image);
  EXPECT_EQ(at(image, 0u, 0u), tablePalette().gold);           // border
  EXPECT_EQ(at(image, 10u, 10u), tablePalette().parchmentDim); // name band
  EXPECT_EQ(at(image, 10u, 70u), tablePalette().parchment);    // below band
}

TEST(PaintTokenCard, DashedGoldInnerFrame) {
  sf::Image image;
  image.create(64u, 88u);
  paintTokenCard(image);
  EXPECT_EQ(at(image, 0u, 0u), tablePalette().gold);        // outer frame
  EXPECT_EQ(at(image, 10u, 6u), tablePalette().goldDim);    // a dash on the line
  EXPECT_EQ(at(image, 30u, 30u), tablePalette().parchment); // interior
}

TEST(PaintLifeRing, TransparentOutsideGoldRingVelvetDisc) {
  sf::Image image;
  image.create(64u, 64u);
  paintLifeRing(image);
  EXPECT_EQ(at(image, 0u, 0u).a, 0u);                    // transparent corner
  EXPECT_EQ(at(image, 32u, 32u), tablePalette().velvet); // inner disc
  // A point on the ring band (between innerR and outerR) is gold with alpha.
  const sf::Color ring = at(image, 54u, 32u);
  EXPECT_EQ(ring.a, 255u);
  EXPECT_GT(ring.r, tablePalette().velvet.r);
}

// Texture uploads (need a GL context; present in the test environment) ------

TEST(BuildTexture, UploadsAnImage) {
  sf::Image image;
  image.create(8u, 8u, sf::Color::Red);
  sf::Texture texture;
  ASSERT_TRUE(buildTexture(texture, image));
  EXPECT_EQ(texture.getSize().x, 8u);
  EXPECT_EQ(texture.getSize().y, 8u);
  EXPECT_EQ(texture.copyToImage().getPixel(0u, 0u), sf::Color::Red);
}

TEST(BuildPlaymatTexture, NonNullWithRightSize) {
  sf::Texture texture;
  ASSERT_TRUE(buildPlaymatTexture(texture, {320u, 240u}));
  EXPECT_EQ(texture.getSize().x, 320u);
  EXPECT_EQ(texture.getSize().y, 240u);
}

TEST(BuildCardBackTexture, NonNullWithRightSize) {
  sf::Texture texture;
  ASSERT_TRUE(buildCardBackTexture(texture, {120u, 168u}));
  EXPECT_EQ(texture.getSize().x, 120u);
  EXPECT_EQ(texture.getSize().y, 168u);
}

TEST(BuildLifeRingTexture, NonNullSquare) {
  sf::Texture texture;
  ASSERT_TRUE(buildLifeRingTexture(texture, 96u));
  EXPECT_EQ(texture.getSize().x, 96u);
  EXPECT_EQ(texture.getSize().y, 96u);
}

} // namespace
} // namespace mtgcpp::core
