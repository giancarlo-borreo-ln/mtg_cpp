// DeckRepository: one-JSON-file-per-deck store with atomic writes (M3.1).
//
// Each deck lives at <base>/decks/<id>.json. Writes go to a sibling `.tmp`
// file that is rename()d over the target, so a reader always sees either the
// old or the new complete file, never a torn one. Deck ids are restricted to
// `[A-Za-z0-9_-]` so they can never escape the decks/ directory.

#include "store/deck_repository.h"

#include "core/card_serialization.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace mtgcpp::core {

namespace {

// ---- Helpers ---------------------------------------------------------------

// Ids must be filesystem-safe: they end up in a filename, so only letters,
// digits, `-` and `_` are allowed (this also blocks any path traversal).
bool isValidId(const std::string &id) {
  if (id.empty()) {
    return false;
  }
  return std::all_of(id.begin(), id.end(),
                     [](unsigned char c) { return std::isalnum(c) != 0 || c == '-' || c == '_'; });
}

std::string toLower(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

// Preview size preference mirrors the webapp's PREVIEW_SIZE_ORDER.
inline constexpr std::array kPreviewSizes{"png", "normal", "large", "small"};

// First card that has any preferred art size wins; returns nullopt when none
// does (mirrors the webapp's `_preview_image`).
std::optional<std::string> previewImage(const std::vector<Card> &cards) {
  for (const Card &card : cards) {
    for (const char *size : kPreviewSizes) {
      const auto entry = card.image_uris.find(size);
      if (entry != card.image_uris.end()) {
        return entry->second;
      }
    }
  }
  return std::nullopt;
}

// Distinct printing keys: lowercased `set|collector_number`, falling back to
// lowercased name for printing-less cards (tokens, hand-built entries).
std::size_t uniquePrintingCount(const std::vector<Card> &cards) {
  std::set<std::string> keys;
  for (const Card &card : cards) {
    if (card.hasPrinting()) {
      keys.insert(toLower(card.set_code) + "|" + toLower(card.collector_number));
    } else {
      keys.insert("name|" + toLower(card.name));
    }
  }
  return keys.size();
}

// 16 hex chars from a fresh 64-bit draw; plenty of entropy for a local store.
std::string newId() {
  static std::random_device rd;
  static std::mt19937_64 generator(rd());
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << generator();
  return out.str();
}

// UTC ISO-8601 with millisecond precision, e.g. `2026-08-18T14:00:00.123Z`.
// Milliseconds keep consecutive creates distinct for stable list ordering.
std::string utcNowIso() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  const std::tm *utc = std::gmtime(&time);
  std::ostringstream out;
  out << std::put_time(utc, "%Y-%m-%dT%H:%M:%S");
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  out << '.' << std::setw(3) << std::setfill('0') << milliseconds.count() << 'Z';
  return out.str();
}

// Write the document to `<path>.tmp` then rename over `path`, so the target is
// either the old or the new complete file. Throws std::runtime_error on failure.
void writeAtomic(const std::filesystem::path &path, const json &doc) {
  const std::filesystem::path tmp(path.string() + ".tmp");
  {
    std::ofstream out(tmp, std::ios::binary);
    if (!out) {
      throw std::runtime_error("deck store: cannot open temporary file " + tmp.string());
    }
    out << doc.dump(2);
    out.flush();
    if (!out) {
      throw std::runtime_error("deck store: cannot write temporary file " + tmp.string());
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    throw std::runtime_error("deck store: cannot finalize " + path.string() + ": " + ec.message());
  }
}

} // namespace

DeckRepository::DeckRepository(std::filesystem::path baseDir) : baseDir_(std::move(baseDir)) {
  std::error_code ec;
  std::filesystem::create_directories(baseDir_ / "decks", ec);
}

Deck DeckRepository::create(const Deck &deck) {
  Deck stored = deck;
  stored.id = newId();
  if (stored.created_at.empty()) {
    stored.created_at = utcNowIso();
  }
  if (stored.updated_at.empty()) {
    stored.updated_at = stored.created_at;
  }
  writeAtomic(filePathFor(stored.id), deckToJson(stored));
  return stored;
}

std::variant<Deck, DeckReadError> DeckRepository::read(const std::string &id) const {
  if (!isValidId(id)) {
    return DeckReadError::NotFound;
  }
  std::ifstream in(filePathFor(id));
  if (!in) {
    return DeckReadError::NotFound;
  }
  json doc;
  try {
    in >> doc;
  } catch (const json::parse_error &) {
    return DeckReadError::InvalidDeck;
  }
  // A plausible deck document must at least name an id; anything else (a bare
  // object, an array, a truncated file) is treated as corrupt, not as a deck.
  if (!doc.is_object() || !doc.contains("id") || !doc.at("id").is_string() ||
      doc.at("id").get<std::string>().empty()) {
    return DeckReadError::InvalidDeck;
  }
  return deckFromJson(doc);
}

std::vector<Deck> DeckRepository::list() const {
  std::vector<Deck> decks;
  const std::filesystem::path decksDir = baseDir_ / "decks";
  std::error_code ec;
  if (!std::filesystem::is_directory(decksDir, ec) || ec) {
    return decks;
  }
  for (std::filesystem::directory_iterator it(decksDir, ec), end; it != end; it.increment(ec)) {
    if (ec) {
      break;
    }
    const std::filesystem::directory_entry &entry = *it;
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    const std::string id = entry.path().stem().string();
    const std::variant<Deck, DeckReadError> result = read(id);
    if (std::holds_alternative<Deck>(result)) {
      decks.push_back(std::get<Deck>(result));
    }
  }
  std::sort(decks.begin(), decks.end(),
            [](const Deck &a, const Deck &b) { return a.created_at > b.created_at; });
  return decks;
}

std::vector<DeckSummary> DeckRepository::listSummaries() const {
  const std::vector<Deck> decks = list();
  std::vector<DeckSummary> summaries;
  summaries.reserve(decks.size());
  for (const Deck &deck : decks) {
    summaries.push_back(deriveSummary(deck));
  }
  return summaries;
}

bool DeckRepository::update(const Deck &deck) {
  if (!isValidId(deck.id)) {
    return false;
  }
  const std::variant<Deck, DeckReadError> existing = read(deck.id);
  if (!std::holds_alternative<Deck>(existing)) {
    return false;
  }
  Deck stored = deck;
  stored.created_at = std::get<Deck>(existing).created_at;
  stored.updated_at = utcNowIso();
  writeAtomic(filePathFor(stored.id), deckToJson(stored));
  return true;
}

bool DeckRepository::remove(const std::string &id) {
  if (!isValidId(id)) {
    return false;
  }
  const std::filesystem::path path = filePathFor(id);
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return false;
  }
  return std::filesystem::remove(path, ec) && !ec;
}

DeckSummary deriveSummary(const Deck &deck) {
  DeckSummary summary;
  summary.id = deck.id;
  summary.name = deck.name;
  summary.format = deck.format;
  summary.total_cards = 0;
  for (const Card &card : deck.cards) {
    summary.total_cards += card.quantity;
  }
  summary.unique_cards = static_cast<int>(uniquePrintingCount(deck.cards));
  summary.preview_image = previewImage(deck.cards);
  summary.created_at = deck.created_at;
  summary.updated_at = deck.updated_at;
  return summary;
}

std::filesystem::path DeckRepository::filePathFor(const std::string &id) const {
  return baseDir_ / "decks" / (id + ".json");
}

std::filesystem::path defaultDataDir() {
#ifdef _WIN32
  if (const char *appData = std::getenv("APPDATA"); appData != nullptr && appData[0] != '\0') {
    return std::filesystem::path(appData) / "mtg_cpp";
  }
  return std::filesystem::path("mtg_cpp");
#else
  if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "mtg_cpp";
  }
  if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return std::filesystem::path(home) / ".local" / "share" / "mtg_cpp";
  }
  return std::filesystem::path("mtg_cpp");
#endif
}

} // namespace mtgcpp::core
