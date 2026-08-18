// Embedded dumb relay server, ported from the webapp's backend/routers/ws.py
// (M6.3 + M6.4).
//
// The server owns the routing core over a transport seam (`ITransport`). The
// host app embeds it; the guest connects over TCP. There is exactly one room —
// room codes were removed (locked decision: the connection identity is
// `IP:PORT`) — so `Server` uses the fixed room id `kDefaultRoom` but keeps the
// webapp's per-room semantics (seat assignment, room-full, waiting/ready).
//
// Responsibilities:
//   * seat up to 2 connections in arrival order (`player_1` host, `player_2`)
//   * `joined` / `player_joined` / `ready` / `player_left` / `error` events
//   * authoritative `from`: the sender id is overwritten with the seat, so a
//     client can never impersonate another player
//   * hand-reveal flow and verbatim `board_update` / `deck_selected` relaying
//     to the opponent's private channel only
//   * anything else is broadcast to the room (the sender gets its own echo,
//     exactly like the original)
//
// The server is single-owner: the owner drains inbound events with `runOnce()`
// from the main/UI thread, so routing state is never mutated off that thread.
#pragma once

#include "net/connection_manager.h"
#include "net/envelope.h"
#include "net/transport.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mtgcpp::net {

class Server {
public:
  // The single fixed room id for the embedded relay (room codes are removed).
  inline static constexpr std::string_view kDefaultRoom = "room";

  explicit Server(ITransport &transport) : transport_(transport) {}

  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;

  // Start the transport (begins accepting connections).
  void start();

  // Stop the transport and reset all room state. Idempotent.
  void stop();

  // Drain every pending transport event and route it. Returns the number of
  // events processed. Call from the main loop / UI thread.
  std::size_t runOnce();

  // Direct routing entry points (the transport event loop and tests use these
  // through `runOnce`; exposed so a driver can drive them explicitly).
  void handleConnected(std::size_t connectionId);
  void handleFrame(std::size_t connectionId, std::string_view rawFrame);
  void handleDisconnected(std::size_t connectionId);

  // Room introspection (used by tests and the Lobby screen).
  std::size_t playerCount() const;
  std::vector<std::string> playerIds() const;
  std::optional<std::string> hostId() const;
  bool isRoomReady() const;

private:
  void handleEvent(const TransportEvent &event);
  void routeHandReveal(const WsEnvelope &envelope, const std::string &seat);
  void sendToConnection(std::size_t connectionId, const WsEnvelope &envelope);
  void sendToPlayer(const std::string &playerId, const std::string &frame);
  void broadcast(const std::string &frame);
  std::optional<std::string> opponentOf(const std::string &playerId) const;
  std::string roleFor(const std::string &playerId) const;

  ITransport &transport_;
  ConnectionManager manager_;
  std::optional<std::string> hostId_;
  bool roomReady_ = false;
};

} // namespace mtgcpp::net
