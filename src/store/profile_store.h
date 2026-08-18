// Persistent selected player (M4.3; ports the webapp's profile.service.ts).
//
// The webapp keeps the chosen player in localStorage; the desktop equivalent is
// a tiny JSON document in the per-install data dir, written atomically like the
// deck store. Only the id is stored — the name is always re-derived from the
// fixed profile list, so a stale stored name can never be shown and the file
// cannot drift out of sync with PLAYER_PROFILES.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mtgcpp::core {

// The chosen player id, or nullopt when nothing valid is stored: a missing or
// unreadable file, a malformed document, or an id that is not one of the fixed
// profiles — exactly the webapp's "default profile" case (the picker shows).
std::optional<std::string> loadPlayerId(const std::filesystem::path &baseDir);

// Remember the chosen player. Ids that are not one of the fixed profiles are
// ignored (a no-op), so a caller can never corrupt the store. Throws
// std::runtime_error when the file cannot be written.
void savePlayerId(const std::filesystem::path &baseDir, std::string_view playerId);

// Forget the chosen player (the "switch player" action). A missing file is not
// an error.
void clearPlayerId(const std::filesystem::path &baseDir);

} // namespace mtgcpp::core
