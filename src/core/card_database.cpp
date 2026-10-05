// CardDatabase: streaming loader + lookup indexes over the bulk JSONL (M2.4).
//
// Each validated record becomes a typed `Card` and lands in two maps: one keyed
// by `set|collector_number`, one by name. Both keys are lowercased so a deck
// export's `(WAR) 263` matches a stored `(war, 263)` the same way the Python
// importer lowercased both sides before matching.

#include "core/card_database.h"

#include "core/card_record.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <istream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
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

CardDatabase::LoadResult CardDatabase::loadFromFile(const std::filesystem::path &jsonlPath,
                                                    const std::filesystem::path &cachePath) {
  // The sidecar is keyed to the source's size + mtime: a re-fetched (or edited)
  // JSONL changes at least one of the two, so the cache never serves stale data.
  std::error_code ec;
  const std::uint64_t size = static_cast<std::uint64_t>(std::filesystem::file_size(jsonlPath, ec));
  const std::uint64_t mtime =
      ec ? 0u
         : static_cast<std::uint64_t>(
               std::filesystem::last_write_time(jsonlPath, ec).time_since_epoch().count());
  if (ec) {
    ec.clear();
  }

  if (readCache(cachePath, size, mtime)) {
    return {.loaded = byPrinting_.size(), .rejected = 0};
  }

  // Cold path: parse the JSONL, then warm the cache for next time. A failed
  // cache write (read-only dir, disk full) is a warning, never an error.
  std::ifstream in(jsonlPath);
  if (!in) {
    return {.loaded = 0, .rejected = 0};
  }
  const LoadResult result = load(in);
  if (result.loaded > 0 && size != 0) {
    writeCache(cachePath, size, mtime);
  }
  return result;
}

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
    const std::size_t index = cards_.size();
    cards_.push_back(card);
    byPrinting_.insert_or_assign(toLower(card.set_code) + "|" + toLower(card.collector_number),
                                 index);
    const std::string nameKey = toLower(card.name);
    if (!byName_.contains(nameKey)) {
      byName_.emplace(nameKey, index);
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
  return cards_.at(entry->second);
}

std::optional<Card> CardDatabase::findByName(std::string_view name) const {
  const auto entry = byName_.find(toLower(name));
  if (entry == byName_.end()) {
    return std::nullopt;
  }
  return cards_.at(entry->second);
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
    const Card &card = cards_.at(entry.second);
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

// ---------------------------------------------------------------------------
// Binary sidecar cache (M12.3)
// ---------------------------------------------------------------------------
// Format: a fixed header, then `count` length-prefixed cards. Every length is
// validated against a hard bound before use, so a corrupt/truncated/tampered
// sidecar degrades to a cache miss (and a re-parse), never a crash.
namespace {

// Magic identifying the sidecar + the wire version (bump on format changes).
constexpr std::array<unsigned char, 8> kCacheMagic{'M', 'T', 'G', 'C', 'C', 'D', 'B', '1'};
constexpr std::uint32_t kCacheVersion = 1;

// Hard bounds so a hostile cache file cannot allocate absurd buffers.
constexpr std::uint64_t kMaxCacheCount = 1u << 20; // 1M cards
constexpr std::uint64_t kMaxStringLen = 1u << 20;  // 1 MiB per string field
constexpr std::uint64_t kMaxMapSize = 1u << 16;    // 64k image_uris / faces
constexpr std::uint64_t kMaxCardBytes = 1u << 24;  // 16 MiB per card
constexpr std::uint64_t kMaxCacheBytes = 1u << 31; // 2 GiB whole cache

// The number of ArenaSection enumerators (valid wire values for a cached card).
constexpr std::int32_t kSectionCount = 3;

// Appends `n` raw bytes.
void putBytes(std::string &out, const void *data, std::size_t n) {
  out.append(static_cast<const char *>(data), n);
}

void putString(std::string &out, std::string_view value) {
  const std::uint32_t len = static_cast<std::uint32_t>(value.size());
  putBytes(out, &len, sizeof(len));
  putBytes(out, value.data(), value.size());
}

void putMap(std::string &out, const std::map<std::string, std::string> &map) {
  const std::uint32_t count = static_cast<std::uint32_t>(map.size());
  putBytes(out, &count, sizeof(count));
  for (const auto &entry : map) {
    putString(out, entry.first);
    putString(out, entry.second);
  }
}

// Appends one Card in the canonical field order (matches the reader).
void putCard(std::string &out, const Card &card) {
  putString(out, card.scryfall_id);
  putString(out, card.name);
  putString(out, card.set_code);
  putString(out, card.set_name);
  putString(out, card.collector_number);
  const std::int32_t quantity = card.quantity;
  putBytes(out, &quantity, sizeof(quantity));
  const std::int32_t section = static_cast<std::int32_t>(card.section);
  putBytes(out, &section, sizeof(section));
  putString(out, card.mana_cost);
  const std::uint8_t hasCmc = card.cmc.has_value() ? 1u : 0u;
  putBytes(out, &hasCmc, sizeof(hasCmc));
  if (card.cmc.has_value()) {
    const float cmc = card.cmc.value();
    putBytes(out, &cmc, sizeof(cmc));
  }
  const std::uint32_t colors = static_cast<std::uint32_t>(card.colors.size());
  putBytes(out, &colors, sizeof(colors));
  for (const std::string &color : card.colors) {
    putString(out, color);
  }
  putString(out, card.type_line);
  putMap(out, card.image_uris);
  const std::uint32_t faces = static_cast<std::uint32_t>(card.card_faces.size());
  putBytes(out, &faces, sizeof(faces));
  for (const ScryfallFace &face : card.card_faces) {
    putString(out, face.name);
    putMap(out, face.image_uris);
  }
}

// A bounds-checked reader over a byte span. Any overrun or invalid length sets
// `ok` to false and the reader is dead afterwards.
class CacheReader {
public:
  explicit CacheReader(std::string_view bytes) : data_(bytes) {}

  bool ok() const { return ok_; }

  std::size_t position() const { return pos_; }

  bool readBytes(void *out, std::size_t n) {
    if (!ok_ || n > data_.size() - pos_) {
      ok_ = false;
      return false;
    }
    std::memcpy(out, data_.data() + pos_, n);
    pos_ += n;
    return true;
  }

  template <typename T> bool readValue(T &out) {
    static_assert(std::is_trivially_copyable_v<T>);
    return readBytes(&out, sizeof(T));
  }

  bool readString(std::string &out) {
    std::uint32_t len = 0;
    if (!readValue(len) || len > kMaxStringLen) {
      ok_ = false;
      return false;
    }
    if (len > data_.size() - pos_) {
      ok_ = false;
      return false;
    }
    out.assign(data_.data() + pos_, len);
    pos_ += len;
    return true;
  }

  bool readMap(std::map<std::string, std::string> &out) {
    std::uint32_t count = 0;
    if (!readValue(count) || count > kMaxMapSize) {
      ok_ = false;
      return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
      std::string key;
      std::string value;
      if (!readString(key) || !readString(value)) {
        return false;
      }
      out.emplace(std::move(key), std::move(value));
    }
    return true;
  }

  // Reads one Card in the canonical field order. Returns false on any overrun /
  // invalid value; the reader is then dead.
  bool readCard(Card &card) {
    if (!readString(card.scryfall_id) || !readString(card.name) || !readString(card.set_code) ||
        !readString(card.set_name) || !readString(card.collector_number)) {
      return false;
    }
    std::int32_t quantity = 0;
    std::int32_t section = 0;
    if (!readValue(quantity) || !readValue(section)) {
      return false;
    }
    card.quantity = quantity;
    if (section < 0 || section >= kSectionCount) {
      ok_ = false;
      return false;
    }
    card.section = static_cast<ArenaSection>(section);
    if (!readString(card.mana_cost)) {
      return false;
    }
    std::uint8_t hasCmc = 0;
    if (!readValue(hasCmc)) {
      return false;
    }
    if (hasCmc != 0u) {
      float cmc = 0.f;
      if (!readValue(cmc)) {
        return false;
      }
      card.cmc = cmc;
    }
    std::uint32_t colors = 0;
    if (!readValue(colors) || colors > kMaxMapSize) {
      ok_ = false;
      return false;
    }
    card.colors.reserve(colors);
    for (std::uint32_t i = 0; i < colors; ++i) {
      std::string color;
      if (!readString(color)) {
        return false;
      }
      card.colors.push_back(std::move(color));
    }
    if (!readString(card.type_line) || !readMap(card.image_uris)) {
      return false;
    }
    std::uint32_t faces = 0;
    if (!readValue(faces) || faces > kMaxMapSize) {
      ok_ = false;
      return false;
    }
    card.card_faces.reserve(faces);
    for (std::uint32_t i = 0; i < faces; ++i) {
      ScryfallFace face;
      if (!readString(face.name) || !readMap(face.image_uris)) {
        return false;
      }
      card.card_faces.push_back(std::move(face));
    }
    return true;
  }

private:
  std::string_view data_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

} // namespace

bool CardDatabase::writeCache(const std::filesystem::path &path, std::uint64_t sourceSize,
                              std::uint64_t sourceMtime) const {
  std::string out;
  out.reserve(static_cast<std::size_t>(
      std::min<std::uint64_t>(byPrinting_.size() * 1536u, static_cast<std::uint64_t>(1u << 30))));
  for (const unsigned char byte : kCacheMagic) {
    out.push_back(static_cast<char>(byte));
  }
  const std::uint32_t version = kCacheVersion;
  putBytes(out, &version, sizeof(version));
  // The header records the source identity (size + mtime) so a changed JSONL
  // can never be served from a stale cache.
  putBytes(out, &sourceSize, sizeof(sourceSize));
  putBytes(out, &sourceMtime, sizeof(sourceMtime));
  const std::uint64_t count = byPrinting_.size();
  putBytes(out, &count, sizeof(count));
  for (const auto &entry : byPrinting_) {
    putCard(out, cards_.at(entry.second));
  }

  // Atomic publish (same pattern as the deck store): write a sibling `.tmp`,
  // then rename over the target so a reader never sees a half-written cache.
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    ec.clear();
  }
  std::filesystem::path tmp = path;
  tmp.replace_extension("tmp");
  {
    std::ofstream outStream(tmp, std::ios::binary);
    if (!outStream) {
      return false;
    }
    outStream.write(out.data(), static_cast<std::streamsize>(out.size()));
    outStream.flush();
    if (!outStream) {
      return false;
    }
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
      std::filesystem::remove(tmp, ec);
      return false;
    }
  }
  return true;
}

bool CardDatabase::readCacheHeader(const std::filesystem::path &path, CacheHeader &header) const {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  char magic[8] = {0};
  in.read(magic, 8);
  if (in.gcount() != 8 || std::memcmp(magic, kCacheMagic.data(), 8) != 0) {
    return false;
  }
  std::uint32_t version = 0;
  if (!in.read(reinterpret_cast<char *>(&version), sizeof(version)) || version != kCacheVersion) {
    return false;
  }
  std::uint64_t size = 0;
  std::uint64_t mtime = 0;
  std::uint64_t count = 0;
  if (!in.read(reinterpret_cast<char *>(&size), sizeof(size)) ||
      !in.read(reinterpret_cast<char *>(&mtime), sizeof(mtime)) ||
      !in.read(reinterpret_cast<char *>(&count), sizeof(count)) || count > kMaxCacheCount) {
    return false;
  }
  header = CacheHeader{size, mtime, count};
  return true;
}

bool CardDatabase::readCache(const std::filesystem::path &path, std::uint64_t sourceSize,
                             std::uint64_t sourceMtime) {
  CacheHeader header;
  if (!readCacheHeader(path, header)) {
    return false;
  }
  // A stale sidecar (different source file) or an empty one is ignored.
  if (header.source_size != sourceSize || header.source_mtime != sourceMtime || header.count == 0) {
    return false;
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  in.seekg(0, std::ios::end);
  const std::streamoff fileSize = in.tellg();
  in.seekg(0, std::ios::beg);
  if (fileSize <= 0 || static_cast<std::uint64_t>(fileSize) > kMaxCacheBytes) {
    return false;
  }
  std::string bytes(static_cast<std::size_t>(fileSize), '\0');
  if (!in.read(bytes.data(), static_cast<std::streamsize>(fileSize))) {
    return false;
  }

  CacheReader reader(bytes);
  char magic[8] = {0};
  std::uint32_t version = 0;
  std::uint64_t size = 0;
  std::uint64_t mtime = 0;
  std::uint64_t count = 0;
  if (!reader.readBytes(magic, 8) || std::memcmp(magic, kCacheMagic.data(), 8) != 0 ||
      !reader.readValue(version) || version != kCacheVersion || !reader.readValue(size) ||
      !reader.readValue(mtime) || !reader.readValue(count) || count > kMaxCacheCount ||
      size != sourceSize || mtime != sourceMtime) {
    return false;
  }

  // Build the index in throwaway containers first so a corrupt tail never
  // leaves a half-filled database behind. Cards are stored once (one slot per
  // printing) and the two maps point into the vector, so a duplicate printing
  // key appends its own slot and re-points the index (last wins).
  std::vector<Card> printingCards;
  printingCards.reserve(static_cast<std::size_t>(count));
  std::unordered_map<std::string, std::size_t> printing;
  std::unordered_map<std::string, std::size_t> names;
  printing.reserve(static_cast<std::size_t>(count));
  names.reserve(static_cast<std::size_t>(count));
  std::uint64_t totalBytes = 8u + sizeof(version) + (3u * sizeof(count));
  for (std::uint64_t i = 0; i < count; ++i) {
    const std::size_t before = reader.position();
    Card card;
    if (!reader.readCard(card)) {
      return false;
    }
    totalBytes += reader.position() - before;
    if (totalBytes > kMaxCacheBytes) {
      return false;
    }
    const std::string printingKey = toLower(card.set_code) + "|" + toLower(card.collector_number);
    const std::size_t index = printingCards.size();
    printingCards.push_back(std::move(card));
    printing.insert_or_assign(printingKey, index);
    const std::string nameKey = toLower(printingCards.at(index).name);
    if (!names.contains(nameKey)) {
      names.emplace(nameKey, index);
    }
  }
  // The sidecar may carry trailing garbage; the header count is authoritative.
  if (!reader.ok()) {
    return false;
  }

  cards_ = std::move(printingCards);
  byPrinting_ = std::move(printing);
  byName_ = std::move(names);
  return true;
}

} // namespace mtgcpp::core
