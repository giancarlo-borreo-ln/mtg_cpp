// WebSocket envelope + framing, ported from the webapp's backend schemas/ws.py
// and the frontend's core/models.ts (M6.1).
//
// Every client<->relay message is a JSON envelope `{event, room, from, payload}`.
// `from` identifies the sending player; the relay is authoritative about the
// sender and overwrites it with the connection's registered seat, so a client
// can never impersonate another player. Frames on the wire are length-prefixed
// (4-byte big-endian header) with a hard 1 MiB cap — the memory-safety rule:
// every network frame is length-bounded and malformed frames are rejected, never
// trusted.
#pragma once

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mtgcpp::net {

// Every client<->relay WebSocket message. `payload` is an arbitrary JSON object
// whose shape depends on `event`; the relay inspects it only to build routed
// hand-reveal envelopes and relays everything else verbatim.
struct WsEnvelope {
  std::string event;
  std::string room;
  std::string from;
  nlohmann::json payload = nlohmann::json::object();
};

// The fixed event names, mirroring `WSEvents` in the webapp. All events a
// session can send or receive. `BoardUpdate` and `DeckSelected` are the dumb
// battlefield-sync pipe: the relay forwards their payload verbatim.
namespace WSEvents {
inline constexpr std::string_view kJoined = "joined";
inline constexpr std::string_view kPlayerJoined = "player_joined";
inline constexpr std::string_view kPlayerLeft = "player_left";
inline constexpr std::string_view kReady = "ready";
inline constexpr std::string_view kError = "error";
inline constexpr std::string_view kRequestHandReveal = "request_hand_reveal";
inline constexpr std::string_view kHandRevealRequest = "hand_reveal_request";
inline constexpr std::string_view kHandRevealAccept = "hand_reveal_accept";
inline constexpr std::string_view kHandRevealDeny = "hand_reveal_deny";
inline constexpr std::string_view kHandRevealResult = "hand_reveal_result";
inline constexpr std::string_view kBoardUpdate = "board_update";
inline constexpr std::string_view kDeckSelected = "deck_selected";
// Local-only (never on the relay): the client's socket failed, so the peer /
// relay is gone. Synthesized by the client and consumed by the session.
inline constexpr std::string_view kConnectionLost = "connection_lost";
} // namespace WSEvents

// Error codes carried in an `error` event's payload, mirroring `WSErrorCodes`.
namespace WSErrorCodes {
inline constexpr std::string_view kRoomFull = "room_full";
inline constexpr std::string_view kRoomNotFound = "room_not_found";
inline constexpr std::string_view kInvalidMessage = "invalid_message";
inline constexpr std::string_view kRoomMismatch = "room_mismatch";
inline constexpr std::string_view kSubscribeFailed = "subscribe_failed";
inline constexpr std::string_view kNoTarget = "no_target";
} // namespace WSErrorCodes

// Strict length bounds for the envelope text fields, mirroring pydantic's
// `min_length=1, max_length=64`.
inline constexpr std::size_t kMinFieldLength = 1;
inline constexpr std::size_t kMaxFieldLength = 64;

// Parse a JSON envelope, validating the schema exactly like pydantic: `event`,
// `room` and `from` must be strings of length 1..64; `payload` (optional) must
// be a JSON object. Returns nullopt for anything else — the caller replies with
// an `invalid_message` error. JSON numbers and other root shapes are rejected.
std::optional<WsEnvelope> parseEnvelope(std::string_view jsonText);

// Serialize an envelope to a single-line JSON string (always carries a payload,
// defaulting to `{}`, matching the webapp's `model_dump_json`).
std::string serializeEnvelope(const WsEnvelope &envelope);

// Server-generated event factories (all have `from` = "server", like the
// webapp's `joined_event`/`player_joined_event`/... helpers in schemas/ws.py).
WsEnvelope joinedEvent(std::string_view room, std::string_view playerId,
                       const std::vector<std::string> &players, std::string_view role);
WsEnvelope playerJoinedEvent(std::string_view room, const std::vector<std::string> &players);
WsEnvelope playerLeftEvent(std::string_view room, std::string_view playerId,
                           const std::vector<std::string> &players);
WsEnvelope readyEvent(std::string_view room, const std::vector<std::string> &players);
WsEnvelope errorEvent(std::string_view room, std::string_view code, std::string_view message);
// Synthesized by the client when the connection drops unexpectedly (the host /
// relay died); lets the session clean up instead of hanging on a stale room.
WsEnvelope connectionLostEvent(std::string_view room);

// ---------------------------------------------------------------------------
// Non-throwing payload readers (M10.2 wire hardening)
// ---------------------------------------------------------------------------
// nlohmann's `.value()`/`.at().get<>()` throw on a type mismatch, and inbound
// payloads are untrusted — a malformed frame must be rejected or ignored, never
// allowed to crash the app. These readers return nullopt when `key` is missing
// or has the wrong type, and never throw.
std::optional<std::string> readPayloadString(const nlohmann::json &payload, std::string_view key);
std::optional<bool> readPayloadBool(const nlohmann::json &payload, std::string_view key);
std::optional<std::vector<std::string>> readPayloadStringArray(const nlohmann::json &payload,
                                                               std::string_view key);
// A sub-object or array payload value (validated by the consumer).
std::optional<nlohmann::json> readPayloadObject(const nlohmann::json &payload,
                                                std::string_view key);
std::optional<nlohmann::json> readPayloadArray(const nlohmann::json &payload, std::string_view key);

// ---------------------------------------------------------------------------
// Length-prefixed framing
// ---------------------------------------------------------------------------

// Length-prefix header size (bytes) and the hard maximum frame payload size.
// Any frame whose length header exceeds kMaxFrameSize is rejected outright.
namespace Framing {
inline constexpr std::size_t kHeaderSize = 4;
inline constexpr std::size_t kMaxFrameSize = 1024 * 1024; // 1 MiB

// Encode `payload` as `[4-byte big-endian length][payload]`. Returns an empty
// string (an encode failure) when the payload exceeds kMaxFrameSize.
std::string encodeFrame(std::string_view payload);
} // namespace Framing

// Incremental length-prefixed frame decoder for a byte stream (e.g. a TCP
// socket). Feed arbitrary chunks with `push`, then pull complete frames with
// `next`. On a length header above kMaxFrameSize the decoder enters the
// oversize state, clears its buffer, and stops producing frames — the caller
// must drop the connection (never trust the wire).
class FrameDecoder {
public:
  // Append raw bytes from the wire to the internal buffer.
  void push(std::string_view bytes);

  // Return the next complete frame, or nullopt when the buffer does not hold a
  // full frame yet (or the decoder is in the oversize state).
  std::optional<std::string> next();

  // True once an oversize length header was seen; the stream is then rejected.
  bool oversize() const { return oversize_; }

  // Number of buffered bytes that do not yet form a complete frame.
  std::size_t buffered() const { return buffer_.size(); }

  // Clear the buffer and leave the oversize state, e.g. to reuse a connection.
  void reset();

private:
  std::size_t readLength() const;

  std::string buffer_;
  bool oversize_ = false;
};

} // namespace mtgcpp::net
