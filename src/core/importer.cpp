// importDeck: resolve Arena export text against the local CardDatabase (M2.5).
//
// Mirrors deck_importer.py's import_deck control flow with the Scryfall HTTP
// calls swapped for index lookups. Per-section aggregation keeps the same
// printing in mainboard and sideboard as separate entries; `total_cards` sums
// every aggregated quantity (resolved or not), while `unique_cards` counts
// distinct resolved printings by lowercased (set, collector_number).

#include "core/importer.h"

#include "core/deck_parser.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mtgcpp::core {

namespace {

std::string toLower(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

} // namespace

DeckParseResult importDeck(const std::string &arenaText, const CardDatabase &db) {
  const ParsedSections sections = parseArenaSections(arenaText);

  DeckParseResult result;
  std::set<std::string> uniquePrintings;
  int totalCards = 0;

  for (const ArenaSection section : kSectionOrder) {
    const std::vector<DeckEntry> entries = aggregateEntries(sections.at(section));
    for (const DeckEntry &entry : entries) {
      totalCards += entry.quantity;

      // Printing first: a (set, collector_number) match wins. Anything else —
      // name-only lines, unknown printings, letter-suffixed MDFC numbers the
      // database lacks — falls back to a name match, like the Python importer.
      std::optional<Card> resolved;
      if (entry.hasPrinting()) {
        resolved = db.findByPrinting(entry.set_code, entry.number);
      }
      if (!resolved.has_value()) {
        resolved = db.findByName(entry.name);
      }

      if (resolved.has_value()) {
        Card card = resolved.value();
        card.quantity = entry.quantity;
        card.section = section;
        uniquePrintings.insert(toLower(card.set_code) + "|" + toLower(card.collector_number));
        result.cards.push_back(std::move(card));
      } else {
        result.not_found.push_back(
            MissingCard{entry.name, entry.set_code, entry.number, entry.quantity, section});
      }
    }
  }

  result.total_cards = totalCards;
  result.unique_cards = static_cast<int>(uniquePrintings.size());
  return result;
}

} // namespace mtgcpp::core
