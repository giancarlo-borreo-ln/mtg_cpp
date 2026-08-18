// Profile store implementation (M4.3): read/write the chosen player id.

#include "store/profile_store.h"

#include "core/card.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace mtgcpp::core {

namespace {

// The single profile document lives directly in the app-data dir, next to the
// decks/ folder the repository manages.
std::filesystem::path profilePath(const std::filesystem::path &baseDir) {
  return baseDir / "profile.json";
}

// Is this one of the four fixed identities? The webapp refuses to restore any
// other id, mirroring its localStorage guard against a stale/corrupt value.
bool isKnownPlayer(std::string_view id) {
  return std::ranges::any_of(playerProfiles(),
                             [id](const PlayerProfile &profile) { return profile.id == id; });
}

} // namespace

std::optional<std::string> loadPlayerId(const std::filesystem::path &baseDir) {
  std::ifstream in(profilePath(baseDir));
  if (!in) {
    return std::nullopt; // no profile saved yet
  }
  json doc;
  try {
    in >> doc;
  } catch (const json::parse_error &) {
    return std::nullopt; // corrupt file: fall back to the picker, never crash
  }
  if (!doc.is_object() || !doc.contains("playerId") || !doc.at("playerId").is_string()) {
    return std::nullopt;
  }
  std::string id = doc.at("playerId").get<std::string>();
  if (!isKnownPlayer(id)) {
    return std::nullopt;
  }
  return id;
}

void savePlayerId(const std::filesystem::path &baseDir, std::string_view playerId) {
  if (!isKnownPlayer(playerId)) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(baseDir, ec);
  const std::filesystem::path path = profilePath(baseDir);
  // Atomic write: a sibling .tmp file rename()d over the target, so a reader
  // always sees either the old or the new complete document, never a torn one.
  const std::filesystem::path tmp(path.string() + ".tmp");
  {
    std::ofstream out(tmp, std::ios::binary);
    if (!out) {
      throw std::runtime_error("profile store: cannot open temporary file " + tmp.string());
    }
    out << json{{"playerId", std::string(playerId)}}.dump(2);
    out.flush();
    if (!out) {
      throw std::runtime_error("profile store: cannot write temporary file " + tmp.string());
    }
  }
  std::error_code renameEc;
  std::filesystem::rename(tmp, path, renameEc);
  if (renameEc) {
    std::filesystem::remove(tmp, ec);
    throw std::runtime_error("profile store: cannot finalize " + path.string());
  }
}

void clearPlayerId(const std::filesystem::path &baseDir) {
  std::error_code ec;
  std::filesystem::remove(profilePath(baseDir), ec);
}

} // namespace mtgcpp::core
