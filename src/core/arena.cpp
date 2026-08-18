#include "core/arena.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

// Renders resolved decks back into the exact MTG Arena text format and groups
// preview items for the import flow. Pure string/format work — no state.

namespace mtgcpp::core {

namespace {

// Arena expects uppercase set codes in exported lines, even though the card
// database stores them lowercase.
std::string toUpper(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return result;
}

} // namespace

std::string sectionLabel(ArenaSection section) {
  for (const SectionLabel &entry : kSectionLabels) {
    if (entry.section == section) {
      return std::string(entry.label);
    }
  }
  return arenaSectionToString(section);
}

std::string missingCardKey(const MissingCard &missing) {
  const std::string section = arenaSectionToString(missing.section);
  if (!missing.set.empty() && !missing.number.empty()) {
    return section + "|" + missing.set + "|" + missing.number;
  }
  return section + "|name|" + missing.name;
}

std::string toArenaText(const std::vector<Card> &cards) {
  // Bucket cards by their section rank first so each header's lines stay in
  // input order; cards without a printing are dropped before bucketing.
  std::array<std::vector<Card>, kSectionLabels.size()> groups;
  for (const Card &card : cards) {
    if (!card.hasPrinting()) {
      continue;
    }
    groups.at(sectionRank(card.section)).push_back(card);
  }

  std::string lines;
  for (std::size_t i = 0; i < kSectionLabels.size(); ++i) {
    if (groups.at(i).empty()) {
      continue;
    }
    if (!lines.empty()) {
      lines += '\n';
    }
    lines += kSectionLabels.at(i).label;
    for (const Card &card : groups.at(i)) {
      lines += '\n';
      lines += std::to_string(card.quantity);
      lines += ' ';
      lines += card.name;
      lines += " (";
      lines += toUpper(card.set_code);
      lines += ") ";
      lines += card.collector_number;
    }
  }
  return lines;
}

std::optional<std::string> readFileText(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) {
    return std::nullopt;
  }
  return buffer.str();
}

} // namespace mtgcpp::core
