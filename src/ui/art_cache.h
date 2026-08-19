// Runtime card-art cache (M10.1).
//
// Scryfall card *data* is CC0 but card *art* is not: we download art on demand
// and cache it locally for personal play — the cache must never be
// redistributed (the README notes this). The pipeline keeps the UI non-blocking:
//
//   * a worker thread pulls `request(id, url)` jobs off a bounded queue,
//     downloads the art over HTTPS (cpr), decodes + downscales it, persists a
//     PNG into the cache dir (keyed by scryfall id + pixel size, written via a
//     temp file + rename so a crash never leaves a torn file), and pushes the
//     decoded image to a results queue;
//   * the main thread calls pump() once per frame to drain finished images and
//     imageFor() to resolve art, falling back to the caller's procedural
//     texture when the art is missing / still downloading / offline.
//
// The HTTP fetch is behind an injectable Fetcher so unit tests never touch the
// network (they inject a fake that serves bytes or fails); the default fetcher
// lives in the .cpp only, so this header stays cpr-free. ArtCache is the single
// owner of its worker thread (RAII: the destructor joins it).
//
// Threading follows the single-owner convention: the worker touches only the
// two MessageQueues and the disk; everything else (requests_ coalescing, the
// in-memory image map) is main-thread only and guarded by a mutex for the
// future TSan pass (M11.1).
#pragma once

#include "net/message_queue.h"

#include <SFML/Graphics/Image.hpp>
#include <SFML/System/Vector2.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mtgcpp::core {

// Downscale an image to `size` with clamped bilinear sampling. Returns the
// source unchanged when its size already matches (or when either is zero).
// Pure CPU work, headless-testable.
sf::Image downscaleImage(const sf::Image &source, sf::Vector2u size);

// The on-disk cache file for a scryfall id + pixel size: `<id>_<w>x<h>.png`
// inside `cacheDir`. The id is sanitized to filename-safe characters so an
// untrusted id can never escape the cache directory.
std::filesystem::path artCachePath(const std::filesystem::path &cacheDir,
                                   std::string_view scryfallId, sf::Vector2u size);

class ArtCache {
public:
  // Fetch the bytes at `url`, or nullopt on failure (offline, HTTP error,
  // timeout, undecodable body). `std::byte` so binary art is never treated as
  // text.
  using Fetcher = std::function<std::optional<std::vector<std::byte>>(const std::string &url)>;

  // `cacheDir` is where the PNGs land (e.g. `<data>/art_cache`); `artSize` is
  // the pixel size every cached image is downscaled to. The default fetcher
  // downloads over HTTPS via cpr.
  ArtCache(std::filesystem::path cacheDir, sf::Vector2u artSize);
  ~ArtCache();

  ArtCache(const ArtCache &) = delete;
  ArtCache &operator=(const ArtCache &) = delete;
  ArtCache(ArtCache &&) = delete;
  ArtCache &operator=(ArtCache &&) = delete;

  // Enable/disable. Disabled: request() is a no-op and imageFor() always
  // returns nullopt, forcing the procedural fallback (the "no art" preference
  // and the test seam). Disk hits are still served while enabled.
  void setEnabled(bool enabled);
  bool enabled() const { return enabled_; }

  // Download `url`'s art for `scryfallId` on the worker thread. Fire-and-forget
  // and idempotent: a second request for the same id is coalesced until the
  // first finishes, and cards already in memory or on disk are skipped. Safe to
  // call every frame for every visible card. No-op when disabled.
  void request(std::string_view scryfallId, std::string_view url);

  // Drain finished downloads into the in-memory cache (main thread, once per
  // frame; never blocks and never fetches).
  void pump();

  // The card's art, or nullopt (the caller keeps its procedural fallback).
  // Consults the in-memory results, then the disk cache; never blocks.
  std::optional<sf::Image> imageFor(std::string_view scryfallId) const;

  // Test seam: override the fetcher. Must be called before the first request
  // (the worker reads it under the state mutex, so concurrent calls are safe).
  void setFetcher(Fetcher fetcher);

  // --- Test introspection ---------------------------------------------------
  // Number of ids waiting for a download (drains to zero once each job settles,
  // whether it succeeded or failed).
  std::size_t pendingRequests() const;
  // Number of PNG files currently in the cache directory.
  std::size_t diskEntryCount() const;
  std::filesystem::path cacheDir() const { return cacheDir_; }
  sf::Vector2u artSize() const { return artSize_; }

private:
  struct ArtJob {
    std::string scryfall_id;
    std::string url;
  };
  struct ArtResult {
    std::string scryfall_id;
    sf::Image image;
  };

  void workerLoop();
  std::optional<sf::Image> loadFromDisk(std::string_view scryfallId) const;
  void saveToDisk(const ArtJob &job, const sf::Image &image) const;

  std::filesystem::path cacheDir_;
  sf::Vector2u artSize_;
  bool enabled_ = true;

  Fetcher fetcher_;
  net::MessageQueue<ArtJob> requests_{64};
  net::MessageQueue<ArtResult> results_{32};
  std::thread worker_;

  // Guards fetcher_, pending_ (coalescing) and images_ (main-thread results).
  // The worker takes it briefly to copy the fetcher and to clear pending_ after
  // a job settles; the main thread takes it in request/pump/imageFor.
  mutable std::mutex stateMutex_;
  std::unordered_set<std::string> pending_;
  std::unordered_map<std::string, sf::Image> images_;
};

} // namespace mtgcpp::core
