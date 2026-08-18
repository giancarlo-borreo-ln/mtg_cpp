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
#include "store/deck_repository.h"
#include "ui/app.h"

#include <exception>
#include <filesystem>
#include <fstream>
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

// Load the local card database from the standard data/ location into `db`.
// Missing files degrade to an empty database (the Deck Editor then shows a
// "database not loaded" hint) rather than aborting the launch.
void loadCardDatabase(mtgcpp::core::CardDatabase &db) {
  std::ifstream in("data/default-cards.jsonl");
  if (!in) {
    std::cerr << "mtg_cpp: warning: data/default-cards.jsonl not found; "
                 "Deck Editor card search is unavailable\n";
    return;
  }
  std::clog << "mtg_cpp: loading card database (this can take a while)...";
  const mtgcpp::core::CardDatabase::LoadResult result = db.load(in);
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
    loadCardDatabase(cardDb);
    mtgcpp::core::DeckRepository repository(mtgcpp::core::defaultDataDir());
    mtgcpp::core::App app(MTG_CPP_VERSION, repository, cardDb, mtgcpp::core::defaultDataDir());
    app.run();
  } catch (const std::exception &exc) {
    std::cerr << "mtg_cpp: fatal: " << exc.what() << '\n';
    return 1;
  }
  return 0;
}
