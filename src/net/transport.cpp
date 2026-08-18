// AsioTransport implementation (M6.5).
#include "net/transport.h"

#include "net/envelope.h"

#include <asio.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mtgcpp::net {

// One accepted TCP connection. All socket access runs on the session's strand
// (the single io thread), so no extra locking is needed inside the session;
// `send`/`close` are dispatched onto the strand from any thread.
class Session : public std::enable_shared_from_this<Session> {
public:
  Session(asio::io_context &ioContext, asio::ip::tcp::socket socket, std::size_t id,
          AsioTransport &transport)
      : strand_(asio::make_strand(ioContext)), socket_(std::move(socket)), id_(id),
        transport_(&transport) {}

  // Begin the read loop.
  void start() { doRead(); }

  // Queue one *already-framed* message for delivery (thread-safe).
  void send(std::string frame) {
    asio::dispatch(strand_, [self = shared_from_this(), frame = std::move(frame)]() mutable {
      self->queueWrite(std::move(frame));
    });
  }

  // Close the socket after any queued writes flush (thread-safe). The pending
  // read aborts and the transport is told the session went away — so a final
  // error frame sent just before close() is still delivered, not cancelled.
  void close() {
    asio::dispatch(strand_, [self = shared_from_this()]() { self->requestClose(); });
  }

private:
  void requestClose() {
    closing_ = true;
    if (!writing_ && pending_.empty()) {
      closeSocket();
    }
  }

  void closeSocket() {
    asio::error_code ignored;
    socket_.close(ignored);
  }
  void doRead() {
    socket_.async_read_some(
        asio::buffer(readBuffer_),
        asio::bind_executor(strand_, [self = shared_from_this()](const asio::error_code &ec,
                                                                 std::size_t bytesRead) {
          if (ec) {
            self->transport_->onSessionGone(self->id_, ec);
            return;
          }
          self->decoder_.push(std::string_view(self->readBuffer_.data(), bytesRead));
          for (;;) {
            const std::optional<std::string> frame = self->decoder_.next();
            if (!frame.has_value()) {
              break;
            }
            self->transport_->pushFrame(self->id_, frame.value());
          }
          if (self->decoder_.oversize()) {
            // Oversize frame: drop the connection, never trust the wire.
            asio::error_code ignored;
            self->socket_.close(ignored);
            return;
          }
          self->doRead();
        }));
  }

  void queueWrite(std::string frame) {
    if (closing_) {
      return; // the session is being closed; drop further outbound messages
    }
    pending_.push_back(std::move(frame));
    if (!writing_) {
      startWrite();
    }
  }

  // Chained writes: at most one `async_write` in flight, the buffer lives in
  // `frame_` until completion.
  void startWrite() {
    writing_ = true;
    frame_ = std::move(pending_.front());
    pending_.pop_front();
    asio::async_write(socket_, asio::buffer(frame_),
                      asio::bind_executor(strand_, [self = shared_from_this()](
                                                       const asio::error_code &ec, std::size_t) {
                        self->writing_ = false;
                        if (ec) {
                          self->transport_->onSessionGone(self->id_, ec);
                          return;
                        }
                        if (!self->pending_.empty()) {
                          self->startWrite();
                          return;
                        }
                        if (self->closing_) {
                          self->closeSocket();
                        }
                      }));
  }

  asio::strand<asio::io_context::executor_type> strand_;
  asio::ip::tcp::socket socket_;
  std::size_t id_;
  AsioTransport *transport_;
  FrameDecoder decoder_;
  std::array<char, 4096> readBuffer_{};
  std::deque<std::string> pending_;
  std::string frame_;
  bool writing_ = false;
  bool closing_ = false;
};

void AsioTransport::start() {
  if (started_.exchange(true)) {
    return;
  }
  stopped_.store(false);
  acceptor_.open(asio::ip::tcp::v4());
  acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
  acceptor_.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port_));
  acceptor_.listen();
  thread_ = std::thread([this] { ioContext_.run(); });
  asio::post(ioContext_, [this] { startAccept(); });
}

void AsioTransport::stop() {
  if (!started_.load() || stopped_.exchange(true)) {
    return;
  }
  asio::post(ioContext_, [this] {
    asio::error_code ignored;
    acceptor_.close(ignored);
    for (auto &entry : sessions_) {
      entry.second->close();
    }
  });
  if (thread_.joinable()) {
    thread_.join();
  }
  // No more events can arrive; unblock any drain blocked on the queue.
  inbound_.close();
  sessions_.clear();
}

void AsioTransport::send(std::size_t connectionId, std::string_view frame) {
  const std::string encoded = Framing::encodeFrame(frame);
  if (encoded.empty()) {
    return; // oversize payload: refuse to send
  }
  asio::post(ioContext_, [this, connectionId, encoded]() {
    const auto it = sessions_.find(connectionId);
    if (it != sessions_.end()) {
      it->second->send(encoded);
    }
  });
}

void AsioTransport::close(std::size_t connectionId) {
  asio::post(ioContext_, [this, connectionId]() {
    const auto it = sessions_.find(connectionId);
    if (it != sessions_.end()) {
      it->second->close();
    }
  });
}

void AsioTransport::pushFrame(std::size_t connectionId, const std::string &frame) {
  if (stopped_.load()) {
    return;
  }
  inbound_.push(TransportEvent{TransportEvent::Type::Frame, connectionId, frame});
}

void AsioTransport::onSessionGone(std::size_t connectionId, const asio::error_code & /*unused*/) {
  if (stopped_.load()) {
    return;
  }
  inbound_.push(TransportEvent{TransportEvent::Type::Disconnected, connectionId, {}});
  sessions_.erase(connectionId);
}

void AsioTransport::startAccept() {
  acceptor_.async_accept([this](const asio::error_code &ec, asio::ip::tcp::socket socket) {
    if (ec) {
      return; // acceptor closed during shutdown — stop accepting
    }
    const std::size_t id = nextId_.fetch_add(1);
    auto session = std::make_shared<Session>(ioContext_, std::move(socket), id, *this);
    sessions_.emplace(id, session);
    session->start();
    inbound_.push(TransportEvent{TransportEvent::Type::Connected, id, {}});
    startAccept();
  });
}

std::uint16_t AsioTransport::localPort() const { return acceptor_.local_endpoint().port(); }

} // namespace mtgcpp::net
