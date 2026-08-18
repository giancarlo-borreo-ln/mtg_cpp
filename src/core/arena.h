// Arena export formatter, ported from the webapp's core/arena.ts (M2.2).
//
// This is the reverse of the parser: it renders a resolved deck back into the
// exact MTG Arena text format (for export/copy) and provides the grouping and
// key helpers the import-preview flow uses. Cards without a concrete printing
// are dropped because an Arena line requires one.
#pragma once

#include "core/card.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mtgcpp::core {

// Section header label, in canonical export order (Deck / Sideboard / Commander).
struct SectionLabel {
  ArenaSection section;
  std::string_view label;
};

inline constexpr std::array kSectionLabels{
    SectionLabel{ArenaSection::Mainboard, "Deck"},
    SectionLabel{ArenaSection::Sideboard, "Sideboard"},
    SectionLabel{ArenaSection::Commander, "Commander"},
};

// Human-readable label for a section header (e.g. `Deck`); falls back to the
// lowercase wire id when the section is somehow not in kSectionLabels.
std::string sectionLabel(ArenaSection section);

// Stable identity of a flagged (unresolved) card within the preview: printings
// key on `section|set|number`, name-only cards on `section|name|<name>`.
std::string missingCardKey(const MissingCard &missing);

// Internal helper shared by groupBySection and toArenaText: the canonical index
// of a section (0 mainboard, 1 sideboard, 2 commander), derived from
// kSectionLabels so the order can only drift in one place.
inline std::size_t sectionRank(ArenaSection section) {
  for (std::size_t i = 0; i < kSectionLabels.size(); ++i) {
    if (kSectionLabels.at(i).section == section) {
      return i;
    }
  }
  return 0;
}

// A group of section-tagged items, in canonical section order.
template <typename T> struct ArenaSectionGroup {
  ArenaSection section;
  std::vector<T> items;
};

// Group section-tagged items into canonical Mainboard → Sideboard → Commander
// order, omitting empty sections. Items without an explicit section are treated
// as mainboard. `T` must expose a `section` member of type ArenaSection.
template <typename T>
std::vector<ArenaSectionGroup<T>> groupBySection(const std::vector<T> &items) {
  std::array<std::vector<T>, kSectionLabels.size()> buckets;
  for (const T &item : items) {
    buckets.at(sectionRank(item.section)).push_back(item);
  }
  std::vector<ArenaSectionGroup<T>> groups;
  for (std::size_t i = 0; i < kSectionLabels.size(); ++i) {
    if (buckets.at(i).empty()) {
      continue;
    }
    groups.push_back({kSectionLabels.at(i).section, std::move(buckets.at(i))});
  }
  return groups;
}

// Render a visual deck as the exact MTG Arena text format: `4 Forest (WAR) 263`
// grouped under `Deck` / `Sideboard` / `Commander` headers. Cards missing a
// printing (no set + collector number) are skipped because Arena lines require
// one. Set codes are uppercased; collector numbers are kept verbatim.
std::string toArenaText(const std::vector<Card> &cards);

// Read a text file (e.g. an Arena `.txt` export) into a string. Returns nullopt
// when the file cannot be opened or read.
std::optional<std::string> readFileText(const std::filesystem::path &path);

} // namespace mtgcpp::core
