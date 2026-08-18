// Deterministic fake `ITransport` for the server/routing tests (M6.2/M6.3).
// Instead of real sockets it records outbound sends/closes and lets the test
// inject lifecycle events straight into the inbound queue, so routing behavior
// is asserted synchronously without threads.
#pragma once

#include "net/transport.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mtgcpp::net {

class FakeTransport final : public ITransport {
public:
  struct SentFrame {
    std::size_t connectionId;
    std::string frame;
  };

  void start() override { started_ = true; }

  void stop() override {
    stopped_ = true;
    inbound_.close();
  }

  void send(std::size_t connectionId, std::string_view frame) override {
    sent_.push_back(SentFrame{connectionId, std::string(frame)});
  }

  void close(std::size_t connectionId) override { closed_.push_back(connectionId); }

  MessageQueue<TransportEvent> &inbound() override { return inbound_; }

  // Test helpers: inject events as if they came from the network threads.
  void simulateConnection(std::size_t connectionId) {
    inbound_.push(TransportEvent{TransportEvent::Type::Connected, connectionId, {}});
  }

  void simulateFrame(std::size_t connectionId, std::string frame) {
    inbound_.push(TransportEvent{TransportEvent::Type::Frame, connectionId, std::move(frame)});
  }

  void simulateDisconnect(std::size_t connectionId) {
    inbound_.push(TransportEvent{TransportEvent::Type::Disconnected, connectionId, {}});
  }

  bool started() const { return started_; }
  bool stopped() const { return stopped_; }

  const std::vector<SentFrame> &sent() const { return sent_; }
  const std::vector<std::size_t> &closed() const { return closed_; }

  // Frames sent to one connection.
  std::vector<SentFrame> sentTo(std::size_t connectionId) const {
    std::vector<SentFrame> result;
    for (const SentFrame &frame : sent_) {
      if (frame.connectionId == connectionId) {
        result.push_back(frame);
      }
    }
    return result;
  }

  // All envelopes sent to one connection, in order.
  std::vector<WsEnvelope> envelopesTo(std::size_t connectionId) const {
    std::vector<WsEnvelope> result;
    for (const SentFrame &frame : sentTo(connectionId)) {
      const std::optional<WsEnvelope> envelope = parseEnvelope(frame.frame);
      if (envelope.has_value()) {
        result.push_back(envelope.value());
      }
    }
    return result;
  }

  // Envelopes of one event sent to one connection, in order.
  std::vector<WsEnvelope> eventsTo(std::size_t connectionId, std::string_view event) const {
    std::vector<WsEnvelope> result;
    for (const WsEnvelope &envelope : envelopesTo(connectionId)) {
      if (envelope.event == event) {
        result.push_back(envelope);
      }
    }
    return result;
  }

private:
  MessageQueue<TransportEvent> inbound_{64};
  std::vector<SentFrame> sent_;
  std::vector<std::size_t> closed_;
  bool started_ = false;
  bool stopped_ = false;
};

} // namespace mtgcpp::net
