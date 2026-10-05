// Client implementation (M6.5). The asio socket + io context live in the
// pImpl (`Client::Impl`) so client.h stays free of <asio.hpp> (Phase 1).

#include "net/client.h"

#include "net/envelope.h"
#include "net/server.h"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <deque>
#include <string>
#include <utility>

namespace mtgcpp::net {

// The whole networking state. Only the io thread (and calls marshalled onto it
// via asio::post) touch the socket; `connected`/`inbound` are the
// synchronization points with the owning thread.
struct Client::Impl {
  asio::io_context ioContext;
  asio::ip::tcp::socket socket{ioContext};
  std::thread thread;
  FrameDecoder decoder;
  std::array<char, 4096> readBuffer{};
  std::deque<std::string> pending;
  std::string frame;
  bool writing = false;
  // Set before an intentional close so the read error does not synthesize a
  // `connection_lost` frame. Atomic: written by disconnect() (any thread) and
  // read by the io thread inside onRead, so TSan sees a synchronized flag.
  std::atomic<bool> closingIntentionally = false;
  MessageQueue<std::string> inbound{64};
  std::atomic<bool> connected = false;

  void doRead();
  void queueWrite(std::string frame);
  void startWrite();
  void onWrite(const asio::error_code &ec, std::size_t bytesWritten);
  void onRead(const asio::error_code &ec, std::size_t bytesRead);
};

Client::Client() : impl_(std::make_unique<Impl>()) {}

Client::~Client() { disconnect(); }

bool Client::connect(std::string_view host, std::string_view port) {
  if (impl_->connected.load()) {
    return false;
  }
  asio::error_code ec;
  asio::ip::tcp::resolver resolver(impl_->ioContext);
  const asio::ip::tcp::resolver::results_type results = resolver.resolve(host, port, ec);
  if (ec) {
    return false;
  }
  asio::connect(impl_->socket, results, ec);
  if (ec) {
    return false;
  }
  impl_->connected.store(true);
  // Initiate the read before the io thread starts so the socket is only
  // touched from one thread at a time; everything after runs on the thread.
  impl_->doRead();
  impl_->thread = std::thread([impl = impl_.get()] { impl->ioContext.run(); });
  return true;
}

void Client::disconnect() {
  if (impl_->thread.joinable()) {
    // Intentional: no `connection_lost` marker is synthesized for this close.
    impl_->closingIntentionally = true;
    asio::post(impl_->ioContext, [impl = impl_.get()] {
      asio::error_code ignored;
      impl->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
      impl->socket.close(ignored);
    });
    impl_->thread.join();
  }
  impl_->connected.store(false);
}

bool Client::connected() const { return impl_->connected.load(); }

bool Client::send(std::string_view frame) {
  if (!impl_->connected.load()) {
    return false;
  }
  const std::string encoded = Framing::encodeFrame(frame);
  if (encoded.empty()) {
    return false; // oversize payload
  }
  asio::post(impl_->ioContext, [impl = impl_.get(), encoded]() {
    if (!impl->connected.load()) {
      return;
    }
    impl->queueWrite(encoded);
  });
  return true;
}

bool Client::sendEnvelope(const WsEnvelope &envelope) { return send(serializeEnvelope(envelope)); }

std::optional<std::string> Client::receive(std::chrono::milliseconds timeout) {
  return impl_->inbound.popFor(timeout);
}

void Client::Impl::doRead() {
  socket.async_read_some(
      asio::buffer(readBuffer),
      [this](const asio::error_code &ec, std::size_t bytesRead) { onRead(ec, bytesRead); });
}

void Client::Impl::onRead(const asio::error_code &ec, std::size_t bytesRead) {
  if (ec) {
    // Peer closed or connection errored. If this was NOT an intentional
    // disconnect, tell the owner the host/relay is gone (M10.2 host-shutdown
    // fan-out) so the session can clean up instead of hanging on a stale room.
    connected.store(false);
    if (!closingIntentionally) {
      inbound.push(serializeEnvelope(connectionLostEvent(std::string(Server::kDefaultRoom))));
    }
    return;
  }
  decoder.push(std::string_view(readBuffer.data(), bytesRead));
  for (;;) {
    const std::optional<std::string> frame = decoder.next();
    if (!frame.has_value()) {
      break;
    }
    inbound.push(frame.value());
  }
  if (decoder.oversize()) {
    // Oversize frame from the peer: drop the connection, never trust the wire.
    connected.store(false);
    asio::error_code ignored;
    socket.close(ignored);
    return;
  }
  doRead();
}

void Client::Impl::queueWrite(std::string frame) {
  pending.push_back(std::move(frame));
  if (!writing) {
    startWrite();
  }
}

void Client::Impl::startWrite() {
  writing = true;
  frame = std::move(pending.front());
  pending.pop_front();
  asio::async_write(
      socket, asio::buffer(frame),
      [this](const asio::error_code &ec, std::size_t bytesWritten) { onWrite(ec, bytesWritten); });
}

void Client::Impl::onWrite(const asio::error_code &ec, std::size_t /*bytesWritten*/) {
  writing = false;
  if (ec) {
    connected.store(false);
    return;
  }
  if (!pending.empty()) {
    startWrite();
  }
}

} // namespace mtgcpp::net
