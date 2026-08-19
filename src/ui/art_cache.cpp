// ArtCache implementation (M10.1): worker-thread downloads, disk persistence,
// procedural fallback. The cpr-based default fetcher lives here so the header
// stays dependency-light.

#include "ui/art_cache.h"

#include <cpr/cpr.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <system_error>

namespace mtgcpp::core {

namespace {

// A single cache file keyed by (scryfall id, pixel size); the id is sanitized
// to filename-safe characters first (validate-before-trust).

// Download `url` over HTTPS via cpr. Returns nullopt on any failure (connect,
// timeout, non-200, empty body) so the caller keeps its procedural fallback.
std::optional<std::vector<std::byte>> fetchViaCpr(const std::string &url) {
  const cpr::Response response =
      cpr::Get(cpr::Url{url}, cpr::Timeout{std::chrono::seconds(10)},
               cpr::ConnectTimeout{std::chrono::seconds(5)}, cpr::UserAgent{"mtg_cpp/0.1.0"});
  if (response.status_code != 200 || response.text.empty()) {
    return std::nullopt;
  }
  // cpr stores the raw body as a std::string; PNG bytes pass through unchanged.
  const std::string &body = response.text;
  std::vector<std::byte> bytes(body.size());
  std::memcpy(bytes.data(), body.data(), body.size());
  return bytes;
}

// Clamped bilinear sample: map a fractional source coordinate to a color,
// never reading out of bounds (memory-safety rule: bounds-checked access).
sf::Color sampleBilinear(const sf::Image &image, float x, float y) {
  const sf::Vector2u size = image.getSize();
  const float cx = std::clamp(x, 0.f, static_cast<float>(size.x) - 1.f);
  const float cy = std::clamp(y, 0.f, static_cast<float>(size.y) - 1.f);
  const unsigned x0 = static_cast<unsigned>(cx);
  const unsigned y0 = static_cast<unsigned>(cy);
  const unsigned x1 = std::min(x0 + 1u, size.x - 1u);
  const unsigned y1 = std::min(y0 + 1u, size.y - 1u);
  const float fx = cx - static_cast<float>(x0);
  const float fy = cy - static_cast<float>(y0);

  const sf::Color c00 = image.getPixel(x0, y0);
  const sf::Color c10 = image.getPixel(x1, y0);
  const sf::Color c01 = image.getPixel(x0, y1);
  const sf::Color c11 = image.getPixel(x1, y1);
  const auto lerp = [fx, fy](float a, float b, float c, float d) {
    const float top = a + (fx * (b - a));
    const float bottom = c + (fx * (d - c));
    return top + (fy * (bottom - top));
  };
  return sf::Color{
      static_cast<sf::Uint8>(std::clamp(lerp(c00.r, c10.r, c01.r, c11.r), 0.f, 255.f)),
      static_cast<sf::Uint8>(std::clamp(lerp(c00.g, c10.g, c01.g, c11.g), 0.f, 255.f)),
      static_cast<sf::Uint8>(std::clamp(lerp(c00.b, c10.b, c01.b, c11.b), 0.f, 255.f)),
      static_cast<sf::Uint8>(std::clamp(lerp(c00.a, c10.a, c01.a, c11.a), 0.f, 255.f))};
}

} // namespace

sf::Image downscaleImage(const sf::Image &source, sf::Vector2u size) {
  const sf::Vector2u src = source.getSize();
  if (src.x == 0u || src.y == 0u || size.x == 0u || size.y == 0u) {
    return source;
  }
  if (src.x == size.x && src.y == size.y) {
    return source;
  }
  sf::Image out;
  out.create(size.x, size.y);
  for (unsigned y = 0; y < size.y; ++y) {
    for (unsigned x = 0; x < size.x; ++x) {
      // Map the destination pixel's center back into source coordinates.
      const float sx = ((static_cast<float>(x) + 0.5f) * static_cast<float>(src.x) /
                        static_cast<float>(size.x)) -
                       0.5f;
      const float sy = ((static_cast<float>(y) + 0.5f) * static_cast<float>(src.y) /
                        static_cast<float>(size.y)) -
                       0.5f;
      out.setPixel(x, y, sampleBilinear(source, sx, sy));
    }
  }
  return out;
}

std::filesystem::path artCachePath(const std::filesystem::path &cacheDir,
                                   std::string_view scryfallId, sf::Vector2u size) {
  std::string safe;
  safe.reserve(scryfallId.size());
  for (const char c : scryfallId) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_') {
      safe.push_back(c);
    }
  }
  if (safe.empty()) {
    safe = "card";
  }
  return cacheDir / (safe + "_" + std::to_string(size.x) + "x" + std::to_string(size.y) + ".png");
}

ArtCache::ArtCache(std::filesystem::path cacheDir, sf::Vector2u artSize)
    : cacheDir_(std::move(cacheDir)), artSize_(artSize), fetcher_(fetchViaCpr),
      worker_(&ArtCache::workerLoop, this) {
  // The cache dir is created lazily on first save, so a read-only data dir
  // degrades to an always-miss cache instead of failing construction.
}

ArtCache::~ArtCache() {
  requests_.close();
  results_.close();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void ArtCache::setEnabled(bool enabled) { enabled_ = enabled; }

void ArtCache::request(std::string_view scryfallId, std::string_view url) {
  if (!enabled_ || scryfallId.empty() || url.empty()) {
    return;
  }
  const std::string id(scryfallId);
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (images_.contains(id) || pending_.contains(id)) {
      return; // already cached in memory or queued
    }
  }
  // Already persisted on a previous run: imageFor() will serve it from disk, so
  // there is nothing to download. This stat is cheap and only runs for cards
  // that are neither in memory nor queued.
  if (std::filesystem::exists(artCachePath(cacheDir_, id, artSize_))) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    pending_.insert(id);
  }
  requests_.push(ArtJob{id, std::string(url)});
}

void ArtCache::pump() {
  for (;;) {
    std::optional<ArtResult> result = results_.tryPop();
    if (!result.has_value()) {
      break;
    }
    std::lock_guard<std::mutex> lock(stateMutex_);
    pending_.erase(result->scryfall_id);
    images_.insert_or_assign(result->scryfall_id, std::move(result->image));
  }
}

std::optional<sf::Image> ArtCache::imageFor(std::string_view scryfallId) const {
  if (!enabled_ || scryfallId.empty()) {
    return std::nullopt;
  }
  const std::string id(scryfallId);
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const std::unordered_map<std::string, sf::Image>::const_iterator it = images_.find(id);
    if (it != images_.end()) {
      return it->second;
    }
  }
  return loadFromDisk(id);
}

void ArtCache::setFetcher(Fetcher fetcher) {
  std::lock_guard<std::mutex> lock(stateMutex_);
  fetcher_ = std::move(fetcher);
}

std::size_t ArtCache::pendingRequests() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return pending_.size();
}

std::size_t ArtCache::diskEntryCount() const {
  std::error_code ec;
  std::size_t count = 0;
  for (std::filesystem::directory_iterator it(cacheDir_, ec), end; it != end && !ec;
       it.increment(ec)) {
    if (it->is_regular_file(ec) && it->path().extension() == ".png") {
      ++count;
    }
  }
  return count;
}

void ArtCache::workerLoop() {
  for (;;) {
    const std::optional<ArtJob> job = requests_.pop();
    if (!job.has_value()) {
      return; // queue closed (shutdown)
    }
    Fetcher fetcher;
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      fetcher = fetcher_;
    }
    const std::optional<std::vector<std::byte>> bytes = fetcher(job->url);
    if (!bytes.has_value()) {
      // Offline / HTTP failure: clear the pending marker so a later request can
      // retry; the UI keeps the procedural fallback meanwhile.
      std::lock_guard<std::mutex> lock(stateMutex_);
      pending_.erase(job->scryfall_id);
      continue;
    }
    sf::Image image;
    if (!image.loadFromMemory(bytes->data(), bytes->size())) {
      // Undecodable body — treated as a failure, never a crash.
      std::lock_guard<std::mutex> lock(stateMutex_);
      pending_.erase(job->scryfall_id);
      continue;
    }
    image = downscaleImage(image, artSize_);
    saveToDisk(*job, image);
    // sf::Image is copy-only in SFML 2.6 (not move-constructible), so the
    // result carries a copy; the queue owns it after the push.
    results_.push(ArtResult{job->scryfall_id, image});
  }
}

std::optional<sf::Image> ArtCache::loadFromDisk(std::string_view scryfallId) const {
  std::error_code ec;
  const std::filesystem::path path = artCachePath(cacheDir_, scryfallId, artSize_);
  if (!std::filesystem::exists(path, ec) || ec) {
    return std::nullopt;
  }
  sf::Image image;
  if (!image.loadFromFile(path.string())) {
    return std::nullopt; // corrupt cache file degrades to a miss
  }
  return image;
}

void ArtCache::saveToDisk(const ArtJob &job, const sf::Image &image) const {
  const std::filesystem::path target = artCachePath(cacheDir_, job.scryfall_id, artSize_);
  std::error_code ec;
  std::filesystem::create_directories(cacheDir_, ec);
  if (ec) {
    return;
  }
  // SFML picks the image encoder from the file extension, so the temp file must
  // end in `.png` (e.g. `<id>_240x335.tmp.png`) even though it is never served.
  std::filesystem::path tmp = target;
  tmp.replace_extension("tmp.png");
  if (!image.saveToFile(tmp.string())) {
    return;
  }
  // Atomically publish: rename over the target. POSIX overwrites in place; on
  // Windows a pre-existing target must be removed first. Either way a reader
  // never observes a partially-written file.
  std::filesystem::rename(tmp, target, ec);
  if (ec) {
    std::filesystem::remove(target, ec);
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
      std::filesystem::remove(tmp, ec);
    }
  }
}

} // namespace mtgcpp::core
