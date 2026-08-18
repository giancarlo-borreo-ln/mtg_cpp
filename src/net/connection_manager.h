// Room seat assignment, ported from the webapp's backend/ws/manager.py (M6.3).
//
// Rooms are sealed at `maxPlayers` (2) seats; extra connections are rejected.
// Seats are assigned in arrival order as `player_1`, `player_2`, ... and a
// freed seat is handed to the next joiner. Each room is isolated: connections
// in different rooms never share state. The relay uses a single fixed room (the
// connection identity is `IP:PORT`, not a room code), but the port keeps the
// webapp's multi-room logic faithful and tested.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mtgcpp::net {

// The seats a room can hold; arrival order of the connections.
inline constexpr std::size_t kMaxPlayersPerRoom = 2;

class ConnectionManager {
public:
  ConnectionManager() = default;
  explicit ConnectionManager(std::size_t maxPlayers) : maxPlayers_(maxPlayers) {}

  ConnectionManager(const ConnectionManager &) = delete;
  ConnectionManager &operator=(const ConnectionManager &) = delete;

  // Number of connections seated in `room`.
  std::size_t playerCount(std::string_view room) const;

  // True when the room holds `maxPlayers_` connections.
  bool isRoomFull(std::string_view room) const;

  // Seat `connectionId` in `room`, returning its assigned player id
  // (`player_1`/`player_2`/...) or nullopt when the room is full.
  std::optional<std::string> connect(std::string_view room, std::size_t connectionId);

  // Map a connection back to its player id; nullopt when unknown.
  std::optional<std::string> playerIdFor(std::string_view room, std::size_t connectionId) const;

  // Remove `connectionId` from `room`, returning its freed player id, or
  // nullopt when it was not seated there.
  std::optional<std::string> disconnect(std::string_view room, std::size_t connectionId);

  // All connection ids seated in `room`, in arrival order.
  std::vector<std::size_t> sockets(std::string_view room) const;

  // Connection ids for one player (each seat maps to exactly one connection).
  std::vector<std::size_t> socketsForPlayer(std::string_view room, std::string_view playerId) const;

  // Player ids seated in `room`, in arrival order.
  std::vector<std::string> playerIds(std::string_view room) const;

  // All room codes with at least one seated connection.
  std::vector<std::string> roomCodes() const;

  // Forget every room (all seats freed).
  void reset();

private:
  struct PlayerSlot {
    std::size_t connectionId;
    std::string playerId;
  };

  // The player id a connection index within a room maps to (`player_N`).
  static std::string playerIdAt(std::size_t index);

  std::size_t maxPlayers_ = kMaxPlayersPerRoom;
  std::unordered_map<std::string, std::vector<PlayerSlot>> rooms_;
};

} // namespace mtgcpp::net
