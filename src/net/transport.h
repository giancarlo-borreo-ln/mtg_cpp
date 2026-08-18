// Transport seam + concrete Asio implementation (M6.2 / M6.5).
//
// `ITransport` abstracts the network boundary so the relay's routing logic is
// independent of any concrete wire: an embedded Asio TCP listener today, a
// future central relay or NAT-punchthrough implementation tomorrow. The seam is
// deliberately narrow — inbound lifecycle/frame events are pushed to a
// `MessageQueue` by the transport's threads (never delivered inline), and
// outbound `send`/`close` are thread-safe so the routing owner can call them
// from the main thread.
//
// `AsioTransport` is the TCP implementation: an Asio listener on a background
// thread, length-prefixed framing applied on the wire, oversize frames rejected
// by dropping the connection (never trust the wire), and reconnection-safe
// teardown that joins its thread.
#pragma once

#include "net/message_queue.h"

#include <asio.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>

namespace mtgcpp::net {

// One accepted TCP connection (implementation detail of AsioTransport, defined
// in net/transport.cpp). Forward-declared so AsioTransport can own its sessions.
class Session;

// A lifecycle or data event delivered off the network thread. `frame` is only
// populated for `Type::Frame` and holds an *unframed* message (the transport
// owns length-prefix framing on the wire).
struct TransportEvent {
  enum class Type {
    Connected,    // a new connection was accepted; `connectionId` is fresh
    Frame,        // a complete message arrived on `connectionId`
    Disconnected, // `connectionId` closed (cleanly or with an error)
  };

  Type type;
  std::size_t connectionId;
  std::string frame;
};

// The network boundary a relay runs over. Implementations must be thread-safe:
// the transport owns its threads, pushes events into `inbound()`, and accepts
// `send`/`close` from any thread.
class ITransport {
public:
  virtual ~ITransport() = default;

  // Begin accepting/connecting. After this returns, inbound events may arrive
  // on `inbound()` from the transport's background thread.
  virtual void start() = 0;

  // Stop the transport: close every connection, join threads, and close the
  // inbound queue so blocked drains return. Idempotent.
  virtual void stop() = 0;

  // Send one unframed message to a connection. Dropped when the connection is
  // unknown or already closed. Thread-safe.
  virtual void send(std::size_t connectionId, std::string_view frame) = 0;

  // Close a single connection. A `Disconnected` event is eventually delivered.
  // Thread-safe.
  virtual void close(std::size_t connectionId) = 0;

  // The inbound event queue the transport's threads push to; the owner drains
  // it (typically via a `Server::runOnce()` from the main loop).
  virtual MessageQueue<TransportEvent> &inbound() = 0;
};

// Concrete TCP `ITransport` for the embedded dumb relay. Implementation in
// `net/transport.cpp`.
class AsioTransport final : public ITransport {
public:
  // `port == 0` asks the OS for an ephemeral port (see `localPort`).
  explicit AsioTransport(std::uint16_t port = 0) : port_(port), acceptor_(ioContext_) {}

  AsioTransport(const AsioTransport &) = delete;
  AsioTransport &operator=(const AsioTransport &) = delete;

  ~AsioTransport() override { stop(); }

  // The port actually bound (valid after `start()`); used to build the
  // `IP:PORT` the guest connects to.
  std::uint16_t localPort() const;

  // ITransport
  void start() override;
  void stop() override;
  void send(std::size_t connectionId, std::string_view frame) override;
  void close(std::size_t connectionId) override;
  MessageQueue<TransportEvent> &inbound() override { return inbound_; }

private:
  friend class Session;

  // Called by a session (on the io thread) to deliver a complete frame.
  void pushFrame(std::size_t connectionId, const std::string &frame);

  // Called by a session (on the io thread) when its socket failed/closed.
  void onSessionGone(std::size_t connectionId, const asio::error_code &ec);

  void startAccept();

  std::uint16_t port_;
  asio::io_context ioContext_;
  asio::ip::tcp::acceptor acceptor_;
  std::thread thread_;
  std::atomic<bool> started_ = false;
  std::atomic<bool> stopped_ = false;
  std::atomic<std::size_t> nextId_ = 0;
  // Only touched on the io thread.
  std::unordered_map<std::size_t, std::shared_ptr<Session>> sessions_;
  MessageQueue<TransportEvent> inbound_{128};
};

} // namespace mtgcpp::net
