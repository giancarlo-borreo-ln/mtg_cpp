// Arena deck text parser, ported from backend/app/services/deck_parser.py (M2.1).
//
// Arena exports cards under single-word section headers (`Deck`/`Mainboard`,
// `Sideboard`, `Commander`); each card line is `4 Forest (WAR) 263` (or a
// name-only `4 Clockwork Percussionist` when the export lacks printings). This
// module splits export text into per-section entries and aggregates duplicates.
// It never touches the card database — resolving entries to real cards is the
// importer's job (M2.5).
#pragma once

#include "core/card.h"

#include <array>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mtgcpp::core {

// A single parsed Arena card line, before resolution against the card database.
// `set_code`/`number` are empty for name-only lines.
struct DeckEntry {
  std::string name;
  std::string set_code;
  std::string number;
  int quantity = 1;

  // True when the line names a concrete printing (set + collector number).
  bool hasPrinting() const { return !set_code.empty() && !number.empty(); }

  bool operator==(const DeckEntry &) const = default;
};

// Raised when no valid card lines can be extracted from the text.
class DeckParseError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Section identifiers in canonical output order (mainboard, sideboard, commander).
inline constexpr std::array kSectionOrder{ArenaSection::Mainboard, ArenaSection::Sideboard,
                                          ArenaSection::Commander};

// Per-section card entries, keyed by ArenaSection, in the order they appeared
// in the export. Always contains all three sections (empty where unused).
using ParsedSections = std::map<ArenaSection, std::vector<DeckEntry>>;

// Parse Arena export text into per-section card entries. A single-word header
// (`Deck`/`Mainboard`, `Sideboard`, `Commander`) switches the active section;
// lines before any header belong to the mainboard. Blank lines and `//`/`#`
// comments are skipped, and malformed lines are skipped without failing.
// Throws DeckParseError when no card line parses.
ParsedSections parseArenaSections(std::string_view text);

// Flatten the per-section result into a single list in section order
// (mainboard, then sideboard, then commander).
std::vector<DeckEntry> parseArenaText(std::string_view text);

// Deduplicate entries, summing quantities. Printings dedupe by lowercased
// (set, number); name-only entries by lowercased name — the two forms never
// merge. Order of first appearance is preserved.
std::vector<DeckEntry> aggregateEntries(const std::vector<DeckEntry> &entries);

} // namespace mtgcpp::core
