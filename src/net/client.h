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
#pragma once

#include "net/envelope.h"
#include "net/message_queue.h"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace mtgcpp::net {

class Client {
public:
  Client() : socket_(ioContext_) {}
  ~Client() { disconnect(); }

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
  bool connected() const { return connected_.load(); }

  // Send one unframed message (framing applied here). Returns false when not
  // connected or the message exceeds the max frame size.
  bool send(std::string_view frame);

  // Convenience: serialize + send an envelope.
  bool sendEnvelope(const WsEnvelope &envelope) { return send(serializeEnvelope(envelope)); }

  // Block up to `timeout` for the next inbound frame; nullopt on timeout.
  // Also returns nullopt once disconnected (the queue just stops producing).
  std::optional<std::string> receive(std::chrono::milliseconds timeout) {
    return inbound_.popFor(timeout);
  }

private:
  void doRead();
  void queueWrite(std::string frame);
  void startWrite();
  void onWrite(const asio::error_code &ec, std::size_t bytesWritten);
  void onRead(const asio::error_code &ec, std::size_t bytesRead);

  asio::io_context ioContext_;
  asio::ip::tcp::socket socket_;
  std::thread thread_;
  FrameDecoder decoder_;
  std::array<char, 4096> readBuffer_{};
  std::deque<std::string> pending_;
  std::string frame_;
  bool writing_ = false;
  // Set before an intentional close so the read error does not synthesize a
  // `connection_lost` frame. Atomic: written by disconnect() (any thread) and
  // read by the io thread inside onRead, so TSan sees a synchronized flag.
  std::atomic<bool> closingIntentionally_ = false;
  MessageQueue<std::string> inbound_{64};
  std::atomic<bool> connected_ = false;
};

} // namespace mtgcpp::net
