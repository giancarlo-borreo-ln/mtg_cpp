// CardDatabase: streaming loader + lookup indexes over the bulk JSONL (M2.4).
//
// Each validated record becomes a typed `Card` and lands in two maps: one keyed
// by `set|collector_number`, one by name. Both keys are lowercased so a deck
// export's `(WAR) 263` matches a stored `(war, 263)` the same way the Python
// importer lowercased both sides before matching.

#include "core/card_database.h"

#include "core/card_record.h"

#include <algorithm>
#include <cctype>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace mtgcpp::core {

namespace {

std::string toLower(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

// True when `value` is a collector number: digits, optionally followed by a
// single letter (Scryfall letter-suffixes like Arena's `51a`).
bool isCollectorNumber(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  std::size_t index = 0;
  while (index < value.size() && std::isdigit(static_cast<unsigned char>(value.at(index))) != 0) {
    ++index;
  }
  // The trailing letter must be the very last character (at most one).
  return index > 0 && index == value.size() - 1
             ? std::isalpha(static_cast<unsigned char>(value.at(index))) != 0
             : index == value.size();
}

// True when `value` is a parenthesized token like `(WAR)`: the inner content is
// non-empty and alphanumeric, which covers set codes and blocks nothing else.
bool isParenthesizedSet(std::string_view value) {
  if (value.size() < 3 || value.front() != '(' || value.back() != ')') {
    return false;
  }
  const std::string_view inner = value.substr(1, value.size() - 2);
  return !inner.empty() && std::all_of(inner.begin(), inner.end(),
                                       [](unsigned char c) { return std::isalnum(c) != 0; });
}

std::string_view trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return value;
}

// Lowercased set code inside `(SET)`, without the parentheses.
std::string setCodeOf(std::string_view token) { return toLower(token.substr(1, token.size() - 2)); }

} // namespace

CardDatabase::LoadResult CardDatabase::load(std::istream &stream) {
  LoadResult result;
  std::string line;
  while (std::getline(stream, line)) {
    // Strip a single trailing carriage return so CRLF files read cleanly.
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    std::string reason;
    const std::optional<json> record = parseCardRecord(line, reason);
    if (!record.has_value()) {
      result.rejected += 1;
      continue;
    }

    const Card card = toCard(record.value());
    byPrinting_.insert_or_assign(toLower(card.set_code) + "|" + toLower(card.collector_number),
                                 card);
    const std::string nameKey = toLower(card.name);
    if (!byName_.contains(nameKey)) {
      byName_.emplace(nameKey, card);
    }
    result.loaded += 1;
  }
  return result;
}

std::optional<Card> CardDatabase::findByPrinting(std::string_view set,
                                                 std::string_view collectorNumber) const {
  const auto entry = byPrinting_.find(toLower(set) + "|" + toLower(collectorNumber));
  if (entry == byPrinting_.end()) {
    return std::nullopt;
  }
  return entry->second;
}

std::optional<Card> CardDatabase::findByName(std::string_view name) const {
  const auto entry = byName_.find(toLower(name));
  if (entry == byName_.end()) {
    return std::nullopt;
  }
  return entry->second;
}

std::vector<Card> CardDatabase::search(std::string_view query, std::size_t limit) const {
  std::vector<Card> results;
  if (limit == 0) {
    return results;
  }

  // Tokenize the trimmed query, classifying every token (see the header).
  std::optional<std::string> setCode;
  std::optional<std::string> collectorNumber;
  std::vector<std::string> nameTokens;
  std::size_t start = 0;
  const std::string_view trimmed = trim(query);
  while (start < trimmed.size()) {
    while (start < trimmed.size() &&
           std::isspace(static_cast<unsigned char>(trimmed.at(start))) != 0) {
      ++start;
    }
    std::size_t end = start;
    while (end < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed.at(end))) == 0) {
      ++end;
    }
    if (end == start) {
      break;
    }
    const std::string_view token = trimmed.substr(start, end - start);
    if (isParenthesizedSet(token)) {
      setCode = setCodeOf(token);
    } else if (isCollectorNumber(token) && !collectorNumber.has_value()) {
      collectorNumber = std::string(token);
    } else {
      // Name tokens are stored lowercased so the substring comparison below
      // matches case-insensitively (the card names are lowered too).
      nameTokens.emplace_back(toLower(token));
    }
    start = end;
  }

  // A set + collector number is an exact printing lookup (Arena line syntax).
  if (setCode.has_value() && collectorNumber.has_value()) {
    const std::optional<Card> printing = findByPrinting(setCode.value(), collectorNumber.value());
    if (printing.has_value()) {
      results.push_back(printing.value());
    }
    return results;
  }

  // A blank query (or a lone collector number with no set) matches nothing —
  // an empty token list would otherwise match every name.
  if (nameTokens.empty() && !setCode.has_value()) {
    return results;
  }

  // Otherwise scan the distinct names; every name token must be a substring of
  // the card name, and an explicit `(SET)` restricts the representative set.
  const std::string wantedSet = toLower(setCode.value_or(""));
  for (const auto &entry : byName_) {
    const Card &card = entry.second;
    if (!wantedSet.empty() && toLower(card.set_code) != wantedSet) {
      continue;
    }
    const std::string lowerCardName = toLower(card.name);
    const bool allTokensMatch =
        std::all_of(nameTokens.begin(), nameTokens.end(), [&lowerCardName](const std::string &t) {
          return lowerCardName.find(t) != std::string::npos;
        });
    if (allTokensMatch) {
      results.push_back(card);
      if (results.size() >= limit) {
        break;
      }
    }
  }
  // Distinct-name scans arrive in hash order; present them sorted by name so
  // the result list reads like Scryfall's `order=name`.
  std::sort(results.begin(), results.end(), [](const Card &a, const Card &b) {
    if (a.name != b.name) {
      return toLower(a.name) < toLower(b.name);
    }
    return a.set_code < b.set_code;
  });
  return results;
}

} // namespace mtgcpp::core
