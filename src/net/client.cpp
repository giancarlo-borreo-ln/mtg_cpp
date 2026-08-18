// Client implementation (M6.5).
#include "net/client.h"

namespace mtgcpp::net {

bool Client::connect(std::string_view host, std::string_view port) {
  if (connected_.load()) {
    return false;
  }
  asio::error_code ec;
  asio::ip::tcp::resolver resolver(ioContext_);
  const asio::ip::tcp::resolver::results_type results = resolver.resolve(host, port, ec);
  if (ec) {
    return false;
  }
  asio::connect(socket_, results, ec);
  if (ec) {
    return false;
  }
  connected_.store(true);
  // Initiate the read before the io thread starts so the socket is only
  // touched from one thread at a time; everything after runs on the thread.
  doRead();
  thread_ = std::thread([this] { ioContext_.run(); });
  return true;
}

void Client::disconnect() {
  if (thread_.joinable()) {
    asio::post(ioContext_, [this] {
      asio::error_code ignored;
      socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
      socket_.close(ignored);
    });
    thread_.join();
  }
  connected_.store(false);
}

bool Client::send(std::string_view frame) {
  if (!connected_.load()) {
    return false;
  }
  const std::string encoded = Framing::encodeFrame(frame);
  if (encoded.empty()) {
    return false; // oversize payload
  }
  asio::post(ioContext_, [this, encoded]() {
    if (!connected_.load()) {
      return;
    }
    queueWrite(encoded);
  });
  return true;
}

void Client::doRead() {
  socket_.async_read_some(
      asio::buffer(readBuffer_),
      [this](const asio::error_code &ec, std::size_t bytesRead) { onRead(ec, bytesRead); });
}

void Client::onRead(const asio::error_code &ec, std::size_t bytesRead) {
  if (ec) {
    // Peer closed or connection errored: mark disconnected and let the io
    // thread drain (no further reads are scheduled).
    connected_.store(false);
    return;
  }
  decoder_.push(std::string_view(readBuffer_.data(), bytesRead));
  for (;;) {
    const std::optional<std::string> frame = decoder_.next();
    if (!frame.has_value()) {
      break;
    }
    inbound_.push(frame.value());
  }
  if (decoder_.oversize()) {
    // Oversize frame from the peer: drop the connection, never trust the wire.
    connected_.store(false);
    asio::error_code ignored;
    socket_.close(ignored);
    return;
  }
  doRead();
}

void Client::queueWrite(std::string frame) {
  pending_.push_back(std::move(frame));
  if (!writing_) {
    startWrite();
  }
}

void Client::startWrite() {
  writing_ = true;
  frame_ = std::move(pending_.front());
  pending_.pop_front();
  asio::async_write(
      socket_, asio::buffer(frame_),
      [this](const asio::error_code &ec, std::size_t bytesWritten) { onWrite(ec, bytesWritten); });
}

void Client::onWrite(const asio::error_code &ec, std::size_t /*bytesWritten*/) {
  writing_ = false;
  if (ec) {
    connected_.store(false);
    return;
  }
  if (!pending_.empty()) {
    startWrite();
  }
}

} // namespace mtgcpp::net
