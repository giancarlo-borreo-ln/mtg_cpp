// App-data path layout, injectable so the engine can be pointed at any root
// directory (Phase 1 — the Godot port maps `user://` here instead of the OS
// default).
//
// Before this change the OS app-data directory was computed inside a free
// function (`defaultDataDir()`) that consulted environment variables directly.
// That is fine for the desktop app but impossible to redirect for an embedded
// host (Godot) without lying about the environment. `DataPaths` collapses the
// whole layout onto one injectable `root`: every derived path hangs off it, so
// a caller supplies one directory and the engine lays everything else out.
#pragma once

#include <filesystem>

namespace mtgcpp::core {

// The engine's on-disk data layout, derived from a single root directory. A
// value type: construct once at startup and pass by const-ref.
struct DataPaths {
  // The root directory everything else hangs off (e.g. `user://` in Godot, or
  // the OS app-data dir on desktop). `decks/`, `profile.json`, `card_db.cache`
  // and `art_cache/` all live here.
  std::filesystem::path root;

  // The OS-default root: `$XDG_DATA_HOME/mtg_cpp` (or `~/.local/share/mtg_cpp`)
  // on Linux, `%APPDATA%\mtg_cpp` on Windows. Same logic the app used before
  // paths became injectable.
  static DataPaths defaultApp();

  // An explicit root. Every derived path hangs off it.
  static DataPaths fromRoot(std::filesystem::path root);

  // <root>/card_db.cache — binary sidecar for the card database.
  std::filesystem::path cardDbCache() const;

  // <root>/decks — the deck repository's directory.
  std::filesystem::path decksDir() const;

  // <root>/profile.json — the persisted player selection.
  std::filesystem::path profileFile() const;

  // <root>/art_cache — the runtime card-art cache.
  std::filesystem::path artCacheDir() const;
};

// Back-compat alias for the OS-default root (kept because the desktop launcher
// and packaging docs reference it). Prefer `DataPaths::defaultApp().root`.
std::filesystem::path defaultDataDir();

} // namespace mtgcpp::core
