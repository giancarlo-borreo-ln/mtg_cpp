// Server routing core (M6.3 + M6.4).
#include "net/server.h"

#include <nlohmann/json.hpp>

namespace mtgcpp::net {

void Server::start() { transport_.start(); }

void Server::stop() {
  transport_.stop();
  manager_.reset();
  hostId_.reset();
  roomReady_ = false;
}

std::size_t Server::runOnce() {
  std::size_t processed = 0;
  for (;;) {
    const std::optional<TransportEvent> event = transport_.inbound().tryPop();
    if (!event.has_value()) {
      break;
    }
    handleEvent(event.value());
    ++processed;
  }
  return processed;
}

void Server::handleEvent(const TransportEvent &event) {
  switch (event.type) {
  case TransportEvent::Type::Connected:
    handleConnected(event.connectionId);
    break;
  case TransportEvent::Type::Frame:
    handleFrame(event.connectionId, event.frame);
    break;
  case TransportEvent::Type::Disconnected:
    handleDisconnected(event.connectionId);
    break;
  }
}

void Server::handleConnected(std::size_t connectionId) {
  const std::optional<std::string> seat = manager_.connect(kDefaultRoom, connectionId);
  if (!seat.has_value()) {
    // The room is full: reject with `room_full` and drop the connection,
    // mirroring the webapp's `RoomFullError` path.
    sendToConnection(connectionId,
                     errorEvent(kDefaultRoom, WSErrorCodes::kRoomFull, "Room is full"));
    transport_.close(connectionId);
    return;
  }
  if (!hostId_.has_value()) {
    hostId_ = *seat;
  }
  const std::vector<std::string> players = manager_.playerIds(kDefaultRoom);
  sendToConnection(connectionId, joinedEvent(kDefaultRoom, *seat, players, roleFor(*seat)));
  broadcast(serializeEnvelope(playerJoinedEvent(kDefaultRoom, players)));
  if (players.size() == kMaxPlayersPerRoom && !roomReady_) {
    roomReady_ = true;
    broadcast(serializeEnvelope(readyEvent(kDefaultRoom, players)));
  }
}

void Server::handleFrame(std::size_t connectionId, std::string_view rawFrame) {
  const std::optional<WsEnvelope> envelope = parseEnvelope(rawFrame);
  if (!envelope.has_value()) {
    sendToConnection(connectionId, errorEvent(kDefaultRoom, WSErrorCodes::kInvalidMessage,
                                              "Envelope must have event, room, from, payload"));
    return;
  }
  if (envelope.value().room != kDefaultRoom) {
    sendToConnection(connectionId, errorEvent(kDefaultRoom, WSErrorCodes::kRoomMismatch,
                                              "Envelope room does not match connection"));
    return;
  }
  const std::optional<std::string> seat = manager_.playerIdFor(kDefaultRoom, connectionId);
  if (!seat.has_value()) {
    return; // unknown connection; drop silently
  }

  // Authoritative `from`: the seat, never the claimed sender.
  WsEnvelope routed = envelope.value();
  routed.from = *seat;

  if (routed.event == WSEvents::kBoardUpdate || routed.event == WSEvents::kDeckSelected) {
    // Dumb battlefield sync / deck announcement: relayed verbatim to the
    // opponent's private channel only. Dropped silently when solo (no error).
    const std::optional<std::string> opponent = opponentOf(*seat);
    if (opponent.has_value()) {
      sendToPlayer(opponent.value(), serializeEnvelope(routed));
    }
    return;
  }
  if (routed.event == WSEvents::kRequestHandReveal || routed.event == WSEvents::kHandRevealAccept ||
      routed.event == WSEvents::kHandRevealDeny) {
    routeHandReveal(routed, *seat);
    return;
  }
  // Any other event is broadcast to the room — the sender receives its own
  // echo, exactly like the webapp's room-channel publish.
  broadcast(serializeEnvelope(routed));
}

void Server::handleDisconnected(std::size_t connectionId) {
  const std::optional<std::string> freed = manager_.disconnect(kDefaultRoom, connectionId);
  if (!freed.has_value()) {
    return; // never seated (e.g. a rejected room-full connection)
  }
  if (hostId_.has_value() && hostId_.value() == freed.value()) {
    hostId_.reset();
  }
  const std::vector<std::string> players = manager_.playerIds(kDefaultRoom);
  if (players.size() < kMaxPlayersPerRoom) {
    roomReady_ = false;
  }
  // `player_left` goes to the remaining members; when the room is empty the
  // broadcast is a no-op (mirrors the webapp's last-player-deletes-room).
  broadcast(serializeEnvelope(playerLeftEvent(kDefaultRoom, freed.value(), players)));
}

void Server::routeHandReveal(const WsEnvelope &envelope, const std::string &seat) {
  const std::optional<std::string> opponent = opponentOf(seat);
  if (!opponent.has_value()) {
    sendToPlayer(seat, serializeEnvelope(errorEvent(kDefaultRoom, WSErrorCodes::kNoTarget,
                                                    "No opponent connected to reveal to")));
    return;
  }
  WsEnvelope routed;
  routed.room = kDefaultRoom;
  routed.from = seat;
  if (envelope.event == WSEvents::kRequestHandReveal) {
    routed.event = WSEvents::kHandRevealRequest;
    routed.payload = {{"from", seat}};
  } else if (envelope.event == WSEvents::kHandRevealAccept) {
    routed.event = WSEvents::kHandRevealResult;
    const nlohmann::json cards = envelope.payload.value("cards", nlohmann::json::array());
    routed.payload = {{"accepted", true}, {"cards", cards}};
  } else { // HandRevealDeny
    routed.event = WSEvents::kHandRevealResult;
    routed.payload = {{"accepted", false}};
  }
  sendToPlayer(opponent.value(), serializeEnvelope(routed));
}

void Server::sendToConnection(std::size_t connectionId, const WsEnvelope &envelope) {
  transport_.send(connectionId, serializeEnvelope(envelope));
}

void Server::sendToPlayer(const std::string &playerId, const std::string &frame) {
  for (const std::size_t connectionId : manager_.socketsForPlayer(kDefaultRoom, playerId)) {
    transport_.send(connectionId, frame);
  }
}

void Server::broadcast(const std::string &frame) {
  for (const std::size_t connectionId : manager_.sockets(kDefaultRoom)) {
    transport_.send(connectionId, frame);
  }
}

std::optional<std::string> Server::opponentOf(const std::string &playerId) const {
  for (const std::string &pid : manager_.playerIds(kDefaultRoom)) {
    if (pid != playerId) {
      return pid;
    }
  }
  return std::nullopt;
}

std::string Server::roleFor(const std::string &playerId) const {
  return hostId_.has_value() && hostId_.value() == playerId ? "host" : "guest";
}

std::size_t Server::playerCount() const { return manager_.playerCount(kDefaultRoom); }

std::vector<std::string> Server::playerIds() const { return manager_.playerIds(kDefaultRoom); }

std::optional<std::string> Server::hostId() const { return hostId_; }

bool Server::isRoomReady() const { return roomReady_; }

} // namespace mtgcpp::net
