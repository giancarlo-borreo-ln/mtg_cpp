// M4.3 profile store tests: persist/load/clear the selected player on a temp
// dir. Ports the webapp's profile.service.ts localStorage semantics: a stored
// id is only trusted when it names one of the four fixed profiles, and corrupt
// or missing files fall back to "no profile" (the picker) instead of crashing.

#include "store/profile_store.h"

#include "core/card.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace mtgcpp::core {
namespace {

// A throwaway directory removed on destruction, so tests never touch real data.
class TempDir {
public:
  TempDir() {
    const std::string unique =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = std::filesystem::temp_directory_path() / ("mtgcpp_profile_" + unique);
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

// Write a raw profile document (bypassing the store) to exercise loading.
void writeRawProfile(const std::filesystem::path &dir, const std::string &content) {
  std::ofstream out(dir / "profile.json");
  out << content;
}

TEST(ProfileStore, NoSavedProfileLoadsAsNone) {
  TempDir dir;
  EXPECT_FALSE(loadPlayerId(dir.path()).has_value());
}

TEST(ProfileStore, SaveThenLoadRoundTrips) {
  TempDir dir;
  savePlayerId(dir.path(), "carlo");
  const std::optional<std::string> loaded = loadPlayerId(dir.path());
  if (loaded.has_value()) {
    EXPECT_EQ(loaded.value(), "carlo");
  } else {
    FAIL() << "expected the saved profile to load";
  }
}

TEST(ProfileStore, ClearForgetsTheSavedProfile) {
  TempDir dir;
  savePlayerId(dir.path(), "nicola");
  clearPlayerId(dir.path());
  EXPECT_FALSE(loadPlayerId(dir.path()).has_value());
}

TEST(ProfileStore, ClearWithNoSavedProfileIsNotAnError) {
  TempDir dir;
  EXPECT_NO_THROW(clearPlayerId(dir.path()));
  EXPECT_FALSE(loadPlayerId(dir.path()).has_value());
}

TEST(ProfileStore, UnknownPlayerIdIsRejectedAndRefused) {
  TempDir dir;
  // Saving an unknown id is a no-op (the store cannot be corrupted).
  EXPECT_NO_THROW(savePlayerId(dir.path(), "hacker"));
  EXPECT_FALSE(loadPlayerId(dir.path()).has_value());
}

TEST(ProfileStore, UnknownStoredIdFallsBackToNoProfile) {
  TempDir dir;
  // A hand-edited file naming a profile that no longer exists must be ignored.
  writeRawProfile(dir.path(), R"({"playerId": "alex"})");
  EXPECT_FALSE(loadPlayerId(dir.path()).has_value());
}

TEST(ProfileStore, CorruptFileFallsBackToNoProfile) {
  TempDir dir;
  writeRawProfile(dir.path(), "{ not valid json");
  EXPECT_FALSE(loadPlayerId(dir.path()).has_value());
}

TEST(ProfileStore, MalformedShapeFallsBackToNoProfile) {
  TempDir dir;
  writeRawProfile(dir.path(), R"([1, 2, 3])");
  EXPECT_FALSE(loadPlayerId(dir.path()).has_value());

  writeRawProfile(dir.path(), R"({"playerId": 42})");
  EXPECT_FALSE(loadPlayerId(dir.path()).has_value());
}

TEST(ProfileStore, AllFourFixedProfilesRoundTrip) {
  for (const PlayerProfile &profile : playerProfiles()) {
    TempDir dir;
    savePlayerId(dir.path(), profile.id);
    const std::optional<std::string> loaded = loadPlayerId(dir.path());
    if (loaded.has_value()) {
      EXPECT_EQ(loaded.value(), profile.id);
    } else {
      FAIL() << "expected profile " << profile.id << " to load";
    }
  }
}

} // namespace
} // namespace mtgcpp::core
