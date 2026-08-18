// ConnectionManager implementation (M6.3).
#include "net/connection_manager.h"

#include <algorithm>
#include <utility>

namespace mtgcpp::net {

std::size_t ConnectionManager::playerCount(std::string_view room) const {
  const auto it = rooms_.find(std::string(room));
  if (it == rooms_.end()) {
    return 0;
  }
  return it->second.size();
}

bool ConnectionManager::isRoomFull(std::string_view room) const {
  return playerCount(room) >= maxPlayers_;
}

std::optional<std::string> ConnectionManager::connect(std::string_view room,
                                                      std::size_t connectionId) {
  std::vector<PlayerSlot> &slots = rooms_[std::string(room)];
  if (slots.size() >= maxPlayers_) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < maxPlayers_; ++index) {
    std::string candidate = playerIdAt(index);
    const bool taken = std::ranges::any_of(
        slots, [&candidate](const PlayerSlot &slot) { return slot.playerId == candidate; });
    if (!taken) {
      slots.push_back(PlayerSlot{connectionId, candidate});
      return candidate;
    }
  }
  return std::nullopt;
}

std::optional<std::string> ConnectionManager::playerIdFor(std::string_view room,
                                                          std::size_t connectionId) const {
  const auto it = rooms_.find(std::string(room));
  if (it == rooms_.end()) {
    return std::nullopt;
  }
  for (const PlayerSlot &slot : it->second) {
    if (slot.connectionId == connectionId) {
      return slot.playerId;
    }
  }
  return std::nullopt;
}

std::optional<std::string> ConnectionManager::disconnect(std::string_view room,
                                                         std::size_t connectionId) {
  auto it = rooms_.find(std::string(room));
  if (it == rooms_.end()) {
    return std::nullopt;
  }
  std::vector<PlayerSlot> &slots = it->second;
  for (std::size_t index = 0; index < slots.size(); ++index) {
    if (slots.at(index).connectionId == connectionId) {
      std::string freed = std::move(slots.at(index).playerId);
      slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(index));
      if (slots.empty()) {
        rooms_.erase(it);
      }
      return freed;
    }
  }
  return std::nullopt;
}

std::vector<std::size_t> ConnectionManager::sockets(std::string_view room) const {
  std::vector<std::size_t> result;
  const auto it = rooms_.find(std::string(room));
  if (it == rooms_.end()) {
    return result;
  }
  result.reserve(it->second.size());
  for (const PlayerSlot &slot : it->second) {
    result.push_back(slot.connectionId);
  }
  return result;
}

std::vector<std::size_t> ConnectionManager::socketsForPlayer(std::string_view room,
                                                             std::string_view playerId) const {
  std::vector<std::size_t> result;
  const auto it = rooms_.find(std::string(room));
  if (it == rooms_.end()) {
    return result;
  }
  for (const PlayerSlot &slot : it->second) {
    if (slot.playerId == playerId) {
      result.push_back(slot.connectionId);
    }
  }
  return result;
}

std::vector<std::string> ConnectionManager::playerIds(std::string_view room) const {
  std::vector<std::string> result;
  const auto it = rooms_.find(std::string(room));
  if (it == rooms_.end()) {
    return result;
  }
  result.reserve(it->second.size());
  for (const PlayerSlot &slot : it->second) {
    result.push_back(slot.playerId);
  }
  return result;
}

std::vector<std::string> ConnectionManager::roomCodes() const {
  std::vector<std::string> result;
  result.reserve(rooms_.size());
  for (const auto &entry : rooms_) {
    result.push_back(entry.first);
  }
  return result;
}

void ConnectionManager::reset() { rooms_.clear(); }

// The player id a connection index within a room maps to (`player_N`); the
// seat naming scheme is fixed and does not depend on instance state.
std::string ConnectionManager::playerIdAt(std::size_t index) {
  return "player_" + std::to_string(index + 1);
}

} // namespace mtgcpp::net
