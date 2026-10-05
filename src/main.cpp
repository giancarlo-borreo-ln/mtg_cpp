// mtg_cpp — application entry point (M0.1 window skeleton → M5.1 Deck Editor).
//
// Two modes:
//   * `mtg_cpp check-cards [jsonl] [manifest]` — headless data-integrity tool
//     (M2.3); exits before any window is opened.
//   * anything else — launch the SFML app shell (M4.1): load the local card
//     database, open the window, run the screen router, block until the user
//     quits (Esc / close button). The try/catch sits at this boundary so an
//     exception escaping a screen cannot unwind past the event loop unnoticed.

#include "core/card_database.h"
#include "core/check_cards.h"
#include "core/data_paths.h"
#include "store/deck_repository.h"
#include "ui/app.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

// `mtg_cpp check-cards [jsonl] [manifest]` — stream the bulk card file, print
// counts + provenance, return the CLI exit code. Paths default to the standard
// data/ locations relative to the working directory.
int runCheckCardsCommand(int argc, char **argv) {
  std::filesystem::path jsonl = "data/default-cards.jsonl";
  std::filesystem::path manifest = "data/manifest.json";
  if (argc >= 3) {
    jsonl = argv[2];
  }
  if (argc >= 4) {
    manifest = argv[3];
  }
  return mtgcpp::core::runCheckCards(jsonl, manifest);
}

// Directory of the running executable (bundle layout: assets/data live next
// to the binary). Empty on failure — callers fall back to the working dir.
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

// Load the local card database from the standard data/ location into `db`.
// Missing files degrade to an empty database (the Deck Editor then shows a
// "database not loaded" hint) rather than aborting the launch. The packaged
// bundle ships `data/` next to the executable, so that location is preferred;
// the source/build-tree `./data` is the fallback. A binary sidecar cache in the
// app-data dir skips the ~3 minute JSON parse on warm launches (it is keyed to
// the JSONL's size + mtime, so a re-fetched database invalidates it).
void loadCardDatabase(mtgcpp::core::CardDatabase &db, const mtgcpp::core::DataPaths &paths) {
  std::filesystem::path jsonl = "data/default-cards.jsonl";
  const std::filesystem::path bundled = executableDir() / "data" / "default-cards.jsonl";
  std::error_code ec;
  if (!bundled.empty() && std::filesystem::exists(bundled, ec) && !ec) {
    jsonl = bundled;
  }
  const std::filesystem::path cache = paths.cardDbCache();
  if (!std::filesystem::exists(jsonl, ec) || ec) {
    std::cerr << "mtg_cpp: warning: " << jsonl.string()
              << " not found; Deck Editor card search is unavailable\n";
    return;
  }
  // A fresh (or invalidated) cache means a slow first parse; say so up front so
  // the minutes-long JSONL pass is not mistaken for a hang.
  const bool cachePresent =
      std::filesystem::exists(cache, ec) && !ec && std::filesystem::file_size(cache, ec) > 0;
  if (!cachePresent) {
    std::clog << "mtg_cpp: parsing the card database (first launch, this can take a few "
                 "minutes)...";
  } else {
    std::clog << "mtg_cpp: loading card database from cache...";
  }
  const mtgcpp::core::CardDatabase::LoadResult result = db.loadFromFile(jsonl, cache);
  std::clog << " " << result.loaded << " cards indexed (" << result.rejected
            << " malformed records skipped)\n";
}

} // namespace

int main(int argc, char **argv) {
  if (argc >= 2 && std::string_view(argv[1]) == "check-cards") {
    return runCheckCardsCommand(argc, argv);
  }

  try {
    // The version string feeds the window banner ("mtg_cpp 0.1.0 — Home");
    // it is compiled in via the MTG_CPP_VERSION definition on this target.
    // The deck store lives in the OS app-data dir; the App only reads/writes
    // through it (and the profile store), never around it. The card database
    // is owned here and handed to the Deck Editor for offline search.
    mtgcpp::core::CardDatabase cardDb;
    const mtgcpp::core::DataPaths paths = mtgcpp::core::DataPaths::defaultApp();
    loadCardDatabase(cardDb, paths);
    mtgcpp::core::DeckRepository repository(paths.root);
    mtgcpp::core::App app(MTG_CPP_VERSION, repository, cardDb, paths.root);
    // Runtime card art: download on demand into the app-data art cache, with
    // the procedural fallback while offline / still downloading (M10.1).
    app.setArtCacheEnabled(true);
    app.run();
  } catch (const std::exception &exc) {
    std::cerr << "mtg_cpp: fatal: " << exc.what() << '\n';
    return 1;
  }
  return 0;
}
