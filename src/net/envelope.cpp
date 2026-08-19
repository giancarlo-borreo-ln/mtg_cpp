// Envelope parsing, serialization and framing helpers (M6.1).
#include "net/envelope.h"

namespace mtgcpp::net {

namespace {

// Validate an envelope text field against pydantic's bounds (1..64 chars).
bool validField(std::string_view value) {
  return value.size() >= kMinFieldLength && value.size() <= kMaxFieldLength;
}

// Read a string field from a parsed JSON object; fails (nullopt) when missing,
// not a string, or out of bounds.
std::optional<std::string> readField(const nlohmann::json &root, std::string_view key) {
  if (!root.contains(key) || !root.at(key).is_string()) {
    return std::nullopt;
  }
  std::string value = root.at(key).get<std::string>();
  if (!validField(value)) {
    return std::nullopt;
  }
  return value;
}

} // namespace

std::optional<WsEnvelope> parseEnvelope(std::string_view jsonText) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(jsonText);
  } catch (const nlohmann::json::parse_error &) {
    return std::nullopt;
  }
  if (!root.is_object()) {
    return std::nullopt;
  }

  const std::optional<std::string> event = readField(root, "event");
  const std::optional<std::string> room = readField(root, "room");
  const std::optional<std::string> from = readField(root, "from");
  if (!event.has_value() || !room.has_value() || !from.has_value()) {
    return std::nullopt;
  }

  WsEnvelope envelope;
  envelope.event = event.value();
  envelope.room = room.value();
  envelope.from = from.value();
  if (root.contains("payload")) {
    if (!root.at("payload").is_object()) {
      return std::nullopt;
    }
    envelope.payload = root.at("payload");
  }
  return envelope;
}

std::string serializeEnvelope(const WsEnvelope &envelope) {
  const nlohmann::json root = {
      {"event", envelope.event},
      {"room", envelope.room},
      {"from", envelope.from},
      {"payload", envelope.payload},
  };
  return root.dump();
}

WsEnvelope joinedEvent(std::string_view room, std::string_view playerId,
                       const std::vector<std::string> &players, std::string_view role) {
  return WsEnvelope{
      std::string(WSEvents::kJoined),
      std::string(room),
      "server",
      {{"player_id", std::string(playerId)}, {"players", players}, {"role", std::string(role)}}};
}

WsEnvelope playerJoinedEvent(std::string_view room, const std::vector<std::string> &players) {
  return WsEnvelope{
      std::string(WSEvents::kPlayerJoined), std::string(room), "server", {{"players", players}}};
}

WsEnvelope playerLeftEvent(std::string_view room, std::string_view playerId,
                           const std::vector<std::string> &players) {
  return WsEnvelope{std::string(WSEvents::kPlayerLeft),
                    std::string(room),
                    "server",
                    {{"player_id", std::string(playerId)}, {"players", players}}};
}

WsEnvelope readyEvent(std::string_view room, const std::vector<std::string> &players) {
  return WsEnvelope{
      std::string(WSEvents::kReady), std::string(room), "server", {{"players", players}}};
}

WsEnvelope errorEvent(std::string_view room, std::string_view code, std::string_view message) {
  return WsEnvelope{std::string(WSEvents::kError),
                    std::string(room),
                    "server",
                    {{"code", std::string(code)}, {"message", std::string(message)}}};
}

WsEnvelope connectionLostEvent(std::string_view room) {
  return WsEnvelope{std::string(WSEvents::kConnectionLost), std::string(room), "server",
                    nlohmann::json::object()};
}

std::optional<std::string> readPayloadString(const nlohmann::json &payload, std::string_view key) {
  if (!payload.is_object() || !payload.contains(key) || !payload.at(key).is_string()) {
    return std::nullopt;
  }
  return payload.at(key).get<std::string>();
}

std::optional<bool> readPayloadBool(const nlohmann::json &payload, std::string_view key) {
  if (!payload.is_object() || !payload.contains(key) || !payload.at(key).is_boolean()) {
    return std::nullopt;
  }
  return payload.at(key).get<bool>();
}

std::optional<std::vector<std::string>> readPayloadStringArray(const nlohmann::json &payload,
                                                               std::string_view key) {
  if (!payload.is_object() || !payload.contains(key) || !payload.at(key).is_array()) {
    return std::nullopt;
  }
  std::vector<std::string> values;
  values.reserve(payload.at(key).size());
  for (const nlohmann::json &entry : payload.at(key)) {
    if (entry.is_string()) {
      values.push_back(entry.get<std::string>());
    }
  }
  return values;
}

std::optional<nlohmann::json> readPayloadObject(const nlohmann::json &payload,
                                                std::string_view key) {
  if (!payload.is_object() || !payload.contains(key) || !payload.at(key).is_object()) {
    return std::nullopt;
  }
  return payload.at(key);
}

std::optional<nlohmann::json> readPayloadArray(const nlohmann::json &payload,
                                               std::string_view key) {
  if (!payload.is_object() || !payload.contains(key) || !payload.at(key).is_array()) {
    return std::nullopt;
  }
  return payload.at(key);
}

namespace Framing {

std::string encodeFrame(std::string_view payload) {
  if (payload.size() > kMaxFrameSize) {
    return {};
  }
  std::string frame;
  frame.reserve(kHeaderSize + payload.size());
  frame.push_back(static_cast<char>((payload.size() >> 24) & 0xFFu));
  frame.push_back(static_cast<char>((payload.size() >> 16) & 0xFFu));
  frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFFu));
  frame.push_back(static_cast<char>(payload.size() & 0xFFu));
  frame.append(payload);
  return frame;
}

} // namespace Framing

void FrameDecoder::push(std::string_view bytes) {
  if (oversize_) {
    return;
  }
  buffer_.append(bytes);
}

std::optional<std::string> FrameDecoder::next() {
  if (oversize_) {
    return std::nullopt;
  }
  if (buffer_.size() < Framing::kHeaderSize) {
    return std::nullopt;
  }
  const std::size_t length = readLength();
  if (length > Framing::kMaxFrameSize) {
    oversize_ = true;
    buffer_.clear();
    return std::nullopt;
  }
  if (buffer_.size() < Framing::kHeaderSize + length) {
    return std::nullopt;
  }
  std::string frame = buffer_.substr(Framing::kHeaderSize, length);
  buffer_.erase(0, Framing::kHeaderSize + length);
  return frame;
}

void FrameDecoder::reset() {
  buffer_.clear();
  oversize_ = false;
}

std::size_t FrameDecoder::readLength() const {
  // Big-endian 32-bit length header, built without casts so the byte order is
  // explicit and endian-safe.
  return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buffer_.at(0))) << 24) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buffer_.at(1))) << 16) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buffer_.at(2))) << 8) |
         static_cast<std::uint32_t>(static_cast<std::uint8_t>(buffer_.at(3)));
}

} // namespace mtgcpp::net
