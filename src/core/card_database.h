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
#include <cstdint>
#include <filesystem>
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

  // Load from `jsonlPath`, using a binary sidecar cache at `cachePath` when it
  // matches the source file (same size + mtime). A cold cache parses the JSONL
  // the slow way, then writes the sidecar so the NEXT launch skips the ~3
  // minute parse. A corrupt/stale cache is discarded and rebuilt, never fatal.
  LoadResult loadFromFile(const std::filesystem::path &jsonlPath,
                          const std::filesystem::path &cachePath);

  // Write the current index to the binary sidecar format (used by
  // loadFromFile to warm the cache after a slow parse). `sourceSize` /
  // `sourceMtime` identify the JSONL the index came from, so a changed source
  // invalidates the cache. Returns false when the file cannot be written
  // (read-only dir) — the caller keeps running from memory.
  bool writeCache(const std::filesystem::path &path, std::uint64_t sourceSize,
                  std::uint64_t sourceMtime) const;

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
  // The actual card storage: one Card per printing, in load order. The two
  // indexes below map a lookup key to an index here, so a printing is never
  // deep-copied per index — the bulk JSONL parse already pays that cost once.
  std::vector<Card> cards_;
  // `lower(set)|lower(collector_number)` → index into `cards_`. Last loaded
  // wins for a duplicate key (its slot is appended, then the index re-points).
  std::unordered_map<std::string, std::size_t> byPrinting_;
  // `lower(name)` → index into `cards_` of the representative printing (first
  // loaded wins).
  std::unordered_map<std::string, std::size_t> byName_;

  // Try to fill the index from the binary sidecar. Returns false when the file
  // is missing, stale, or malformed (the caller then falls back to a slow JSON
  // parse). On success `rejected` is 0 (the sidecar was written from a clean
  // load).
  bool readCache(const std::filesystem::path &path, std::uint64_t sourceSize,
                 std::uint64_t sourceMtime);

  // Header of a valid sidecar: identifies the source file (size + mtime) so a
  // changed JSONL cannot be served from a stale cache.
  struct CacheHeader {
    std::uint64_t source_size = 0;
    std::uint64_t source_mtime = 0;
    std::uint64_t count = 0;
  };
  bool readCacheHeader(const std::filesystem::path &path, CacheHeader &header) const;
};

} // namespace mtgcpp::core
