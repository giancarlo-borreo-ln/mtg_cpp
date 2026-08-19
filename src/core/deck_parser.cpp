#include "core/deck_parser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <map>
#include <regex>
#include <system_error>

// Parsing is deliberately resilient: unknown headers and malformed lines are
// skipped, never fatal. Only a text with zero parseable card lines raises
// DeckParseError, matching the Python module's contract.

namespace mtgcpp::core {

namespace {

// Regexes mirror the Python module exactly. The printing form must be tried
// first: its name group is non-greedy so it stops at the final ` (SET) NUM`,
// and without the parens the same line would otherwise match the name-only
// form with a mangled name. They live behind getters so construction happens on
// first use (inside a catchable context), never during static initialization.
const std::regex &printingLineRe() {
  static const std::regex kRe{R"(^(\d+)\s+(.+?)\s+\(([A-Za-z0-9]+)\)\s+(\d+[a-z]?)$)"};
  return kRe;
}

const std::regex &nameOnlyLineRe() {
  static const std::regex kRe{R"(^(\d+)\s+(.+)$)"};
  return kRe;
}

const std::regex &sectionHeaderRe() {
  static const std::regex kRe{R"(^([A-Za-z]+)$)"};
  return kRe;
}

// Arena prints a `N cards` summary line under each section — never a real card.
constexpr std::array<std::string_view, 2> kSummaryNames{"card", "cards"};

// Upper bound on a single card line. Arena exports never exceed a few hundred
// characters, and `std::regex` backtracks recursively — a pathologically long
// line (fuzzer-found) could otherwise overflow the stack. Oversized lines are
// malformed and skipped, exactly like any other unparseable line.
constexpr std::size_t kMaxLineLength = 4096;

// Header aliases -> canonical section id. Headers are lowercased before matching.
std::optional<ArenaSection> sectionFromHeader(std::string_view header) {
  if (header == "mainboard" || header == "deck") {
    return ArenaSection::Mainboard;
  }
  if (header == "sideboard") {
    return ArenaSection::Sideboard;
  }
  if (header == "commander") {
    return ArenaSection::Commander;
  }
  return std::nullopt;
}

std::string toLower(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

// Python str.strip(): leading/trailing whitespace, including the \r of CRLF
// line endings and the trailing newline the splitter leaves on each line.
std::string_view trim(std::string_view line) {
  const std::size_t first = line.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const std::size_t last = line.find_last_not_of(" \t\r\n");
  return line.substr(first, last - first + 1);
}

// The quantity group is always digits by construction (the regex guarantees
// it), so from_chars cannot fail except on absurdly large input, which we
// clamp rather than crash on.
int parseQuantity(std::string_view digits) {
  int quantity = 0;
  const std::from_chars_result result =
      std::from_chars(digits.data(), digits.data() + digits.size(), quantity);
  if (result.ec == std::errc::result_out_of_range) {
    return std::numeric_limits<int>::max();
  }
  return quantity;
}

// Parse one card line, preferring the printing form `4 Forest (WAR) 263`, then
// the name-only form `4 Clockwork Percussionist`. Returns nullopt when neither
// fits — including Arena's `N cards` summary lines (name-only by shape, but
// filtered out below).
std::optional<DeckEntry> parseCardLine(std::string_view line) {
  const std::string text(line);
  std::smatch match;
  if (std::regex_match(text, match, printingLineRe())) {
    return DeckEntry{
        .name = match.str(2),
        .set_code = match.str(3),
        .number = match.str(4),
        .quantity = parseQuantity(match.str(1)),
    };
  }
  if (std::regex_match(text, match, nameOnlyLineRe())) {
    const std::string name = match.str(2);
    if (std::find(kSummaryNames.begin(), kSummaryNames.end(), toLower(name)) !=
        kSummaryNames.end()) {
      return std::nullopt;
    }
    return DeckEntry{
        .name = name,
        .set_code = "",
        .number = "",
        .quantity = parseQuantity(match.str(1)),
    };
  }
  return std::nullopt;
}

// Dedupe key for aggregateEntries: printings key on lowercased (set, number),
// name-only entries on the lowercased name. The leading `kind` discriminator
// keeps the two forms from ever merging (the Python module uses a 3-tuple
// `("printing"|"name", ...)` with the same effect).
struct AggregateKey {
  std::string kind;
  std::string first;
  std::string second;

  bool operator<(const AggregateKey &other) const {
    if (kind != other.kind) {
      return kind < other.kind;
    }
    if (first != other.first) {
      return first < other.first;
    }
    return second < other.second;
  }
};

AggregateKey makeKey(const DeckEntry &entry) {
  if (entry.hasPrinting()) {
    return {.kind = "printing", .first = toLower(entry.set_code), .second = toLower(entry.number)};
  }
  return {.kind = "name", .first = toLower(entry.name), .second = ""};
}

} // namespace

ParsedSections parseArenaSections(std::string_view text) {
  ParsedSections sections{
      {ArenaSection::Mainboard, {}},
      {ArenaSection::Sideboard, {}},
      {ArenaSection::Commander, {}},
  };
  ArenaSection current = ArenaSection::Mainboard;
  bool found_any = false;

  // Walk lines manually so we can report positions and avoid materializing the
  // whole text. Each line is trimmed, then classified: section header first,
  // otherwise a card line (skipped silently when neither matches).
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find('\n', start);
    const std::string_view raw =
        text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    const std::string line(trim(raw));

    if (!line.empty() && !line.starts_with("//") && !line.starts_with("#") &&
        line.size() <= kMaxLineLength) {
      std::smatch match;
      bool is_section_header = false;
      if (std::regex_match(line, match, sectionHeaderRe())) {
        const std::optional<ArenaSection> section = sectionFromHeader(toLower(match.str(1)));
        if (section) {
          current = *section;
          is_section_header = true;
        }
      }
      if (!is_section_header) {
        if (const std::optional<DeckEntry> entry = parseCardLine(line)) {
          sections.at(current).push_back(*entry);
          found_any = true;
        }
      }
    }

    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }

  if (!found_any) {
    throw DeckParseError("No valid card lines found in the provided text");
  }
  return sections;
}

std::vector<DeckEntry> parseArenaText(std::string_view text) {
  const ParsedSections sections = parseArenaSections(text);
  std::vector<DeckEntry> entries;
  for (const ArenaSection section : kSectionOrder) {
    const std::vector<DeckEntry> &section_entries = sections.at(section);
    entries.insert(entries.end(), section_entries.begin(), section_entries.end());
  }
  return entries;
}

std::vector<DeckEntry> aggregateEntries(const std::vector<DeckEntry> &entries) {
  // First-seen order is preserved by remembering each key's position in the
  // result; later duplicates sum their quantity into that slot instead of
  // creating a new entry.
  std::vector<DeckEntry> result;
  std::map<AggregateKey, std::size_t> index_of;
  for (const DeckEntry &entry : entries) {
    const AggregateKey key = makeKey(entry);
    const auto [it, inserted] = index_of.try_emplace(key, result.size());
    if (inserted) {
      result.push_back(entry);
    } else {
      // Saturating sum: quantities are clamped to INT_MAX on parse, and adding
      // two clamped values must not overflow (a fuzzer found this).
      int &quantity = result.at(it->second).quantity;
      const int remaining = std::numeric_limits<int>::max() - quantity;
      quantity += std::min(entry.quantity, remaining);
    }
  }
  return result;
}

} // namespace mtgcpp::core
