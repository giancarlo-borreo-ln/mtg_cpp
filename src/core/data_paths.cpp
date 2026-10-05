// DataPaths implementation (Phase 1): the injectable data layout + the OS
// default that used to live in deck_repository.cpp as defaultDataDir().

#include "core/data_paths.h"

#include <cstdlib>
#include <filesystem>

namespace mtgcpp::core {

namespace {

// The OS-default app-data root, resolved once from the environment.
std::filesystem::path osDefaultRoot() {
#ifdef _WIN32
  if (const char *appData = std::getenv("APPDATA"); appData != nullptr && appData[0] != '\0') {
    return std::filesystem::path(appData) / "mtg_cpp";
  }
  return std::filesystem::path("mtg_cpp");
#else
  if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "mtg_cpp";
  }
  if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return std::filesystem::path(home) / ".local" / "share" / "mtg_cpp";
  }
  return std::filesystem::path("mtg_cpp");
#endif
}

} // namespace

DataPaths DataPaths::defaultApp() { return DataPaths{osDefaultRoot()}; }

DataPaths DataPaths::fromRoot(std::filesystem::path root) { return DataPaths{std::move(root)}; }

std::filesystem::path DataPaths::cardDbCache() const { return root / "card_db.cache"; }

std::filesystem::path DataPaths::decksDir() const { return root / "decks"; }

std::filesystem::path DataPaths::profileFile() const { return root / "profile.json"; }

std::filesystem::path DataPaths::artCacheDir() const { return root / "art_cache"; }

std::filesystem::path defaultDataDir() { return DataPaths::defaultApp().root; }

} // namespace mtgcpp::core
