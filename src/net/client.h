// Relay client (M6.5).
//
// `Client` is the guest (or host) side of the TCP relay: it connects to the
// `IP:PORT` the host shares, sends length-prefixed frames, and delivers inbound
// frames through a thread-safe `MessageQueue`. Reading runs on a background
// io thread; sends are marshalled onto it via `asio::post`, so `send` is safe
// from any thread and outbound frames are chained (never overlapping on the
// socket).
//
// The client is single-use: `connect` once, `disconnect` when done (idempotent
// and reconnection-safe — it joins the read thread so nothing outlives the
// object).
//
// Phase 1 pImpl: every asio type lives in `Impl` (defined in the .cpp), so this
// header pulls only the standard library. That keeps the engine's public
// surface (state/session.h -> net/client.h) free of <asio.hpp>.
#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace mtgcpp::net {

// Defined in net/envelope.h (internal); forward-declared here so the header
// stays nlohmann-free.
struct WsEnvelope;

class Client {
public:
  Client();
  ~Client();

  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  // Connect to `host:port` (synchronously). Returns false on failure; a failed
  // client can be discarded. Single-use: after a successful connect the client
  // cannot be reused.
  bool connect(std::string_view host, std::string_view port);

  // Close the connection and join the read thread. Idempotent and safe to call
  // multiple times; also safe when the peer already closed the connection.
  void disconnect();

  // True while the socket is connected and the read loop is running. Set false
  // when the peer closes the connection or the client is disconnected.
  bool connected() const;

  // Send one unframed message (framing applied here). Returns false when not
  // connected or the message exceeds the max frame size.
  bool send(std::string_view frame);

  // Convenience: serialize + send an envelope.
  bool sendEnvelope(const WsEnvelope &envelope);

  // Block up to `timeout` for the next inbound frame; nullopt on timeout.
  // Also returns nullopt once disconnected (the queue just stops producing).
  std::optional<std::string> receive(std::chrono::milliseconds timeout);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace mtgcpp::net
