// In-memory card database built from the Scryfall bulk JSONL (M2.4).
//
// Loading is fully streamed: each line is validated through the shared record
// grammar (card_record.h), converted to a typed `Card`, and indexed, so a
// ~600 MB artifact is never parsed into one document and peak memory stays
// flat. The database answers the two lookups the importer (M2.5) and deck
// editor need: an exact printing by (set, collector_number) and a
// representative printing by card name — plus the editor's card search
// (M5.1), the local stand-in for Scryfall's /cards/search.
#pragma once

#include "core/card.h"

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mtgcpp::core {

class CardDatabase {
public:
  // Outcome of one streaming load.
  struct LoadResult {
    std::size_t loaded = 0;
    std::size_t rejected = 0;

    bool operator==(const LoadResult &) const = default;
  };

  // Stream the bulk JSONL: validate + convert every record, skip malformed
  // ones. Index keys are lowercased so lookups are case-insensitive.
  LoadResult load(std::istream &stream);

  // Exact printing lookup by (set, collector_number), case-insensitive.
  std::optional<Card> findByPrinting(std::string_view set, std::string_view collectorNumber) const;

  // Representative printing for a card name (first loaded wins),
  // case-insensitive. Handles double-faced names like `Akoum Warrior // Akoum
  // Teeth` verbatim.
  std::optional<Card> findByName(std::string_view name) const;

  // Card search (M5.1), the local equivalent of Scryfall's /cards/search with
  // `unique=cards`: one representative printing per card name, ordered by
  // name. The query is tokenized on whitespace and each token interpreted as:
  //   * `(SET)`            — an explicit set code (parenthesized), which
  //                          restricts a name search to that set;
  //   * a number (`263`) or a letter-suffixed number (`51a`) — a collector
  //                          number, used only together with a `(SET)` token;
  //   * anything else      — a name token that must appear in the card name
  //                          (case-insensitive substring); all name tokens
  //                          must match.
  // A query that carries both a `(SET)` code and a number resolves to that
  // exact printing (or nothing); any other query returns the distinct matching
  // card names capped at `limit`. Empty queries yield no results.
  std::vector<Card> search(std::string_view query, std::size_t limit = 50) const;

  // Number of distinct printings indexed.
  std::size_t size() const { return byPrinting_.size(); }

  bool empty() const { return byPrinting_.empty(); }

private:
  // `lower(set)|lower(collector_number)` → printing. Last loaded wins for a
  // duplicate key.
  std::unordered_map<std::string, Card> byPrinting_;
  // `lower(name)` → representative printing (first loaded wins).
  std::unordered_map<std::string, Card> byName_;
};

} // namespace mtgcpp::core
