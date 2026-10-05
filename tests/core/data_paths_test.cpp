// DataPaths tests (Phase 1): the injectable data layout. fromRoot must derive
// every path off the single root; defaultApp must always yield a non-empty root
// (it falls back to a bare directory name when the environment is unset).

#include "core/data_paths.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace mtgcpp::core {
namespace {

TEST(DataPaths, FromRootDerivesEveryPath) {
  const DataPaths paths = DataPaths::fromRoot("/some/root");
  EXPECT_EQ(paths.root, std::filesystem::path("/some/root"));
  EXPECT_EQ(paths.cardDbCache(), std::filesystem::path("/some/root/card_db.cache"));
  EXPECT_EQ(paths.decksDir(), std::filesystem::path("/some/root/decks"));
  EXPECT_EQ(paths.profileFile(), std::filesystem::path("/some/root/profile.json"));
  EXPECT_EQ(paths.artCacheDir(), std::filesystem::path("/some/root/art_cache"));
}

TEST(DataPaths, FromRootIsAValueType) {
  DataPaths a = DataPaths::fromRoot("/a");
  DataPaths b = DataPaths::fromRoot("/b");
  const DataPaths copy = a; // copy is an independent value, not a view
  EXPECT_EQ(copy.root, std::filesystem::path("/a"));
  a = b;
  EXPECT_EQ(a.root, std::filesystem::path("/b"));
  EXPECT_EQ(copy.root, std::filesystem::path("/a"));
}

TEST(DataPaths, DefaultAppHasANonEmptyRoot) {
  const DataPaths paths = DataPaths::defaultApp();
  EXPECT_FALSE(paths.root.empty());
  EXPECT_EQ(defaultDataDir(), paths.root);
}

} // namespace
} // namespace mtgcpp::core
