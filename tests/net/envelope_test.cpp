// M6.1 envelope + framing tests, mirroring the webapp's schemas/ws.py schema
// validation and the framing acceptance criteria (round-trip, validation
// rejections, oversize frame rejected at the framing layer).

#include "net/envelope.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace mtgcpp::net {
namespace {

// Serialize an envelope and parse it back; asserts a faithful round-trip.
TEST(Envelope, RoundTripsThroughJson) {
  const WsEnvelope envelope{"move_card", "room", "player_1", {{"id", "c1"}, {"zone", "creatures"}}};

  const std::optional<WsEnvelope> parsed = parseEnvelope(serializeEnvelope(envelope));

  if (parsed.has_value()) {
    const WsEnvelope &p = parsed.value();
    EXPECT_EQ(p.event, "move_card");
    EXPECT_EQ(p.room, "room");
    EXPECT_EQ(p.from, "player_1");
    EXPECT_EQ(p.payload, envelope.payload);
  } else {
    FAIL() << "expected the envelope to round-trip";
  }
}

TEST(Envelope, PayloadDefaultsToAnEmptyObject) {
  const std::optional<WsEnvelope> parsed =
      parseEnvelope(R"({"event": "joined", "room": "room", "from": "server"})");

  if (parsed.has_value()) {
    EXPECT_TRUE(parsed.value().payload.is_object());
    EXPECT_TRUE(parsed.value().payload.empty());
  } else {
    FAIL() << "expected a valid envelope";
  }
}

TEST(Envelope, RejectsInvalidJson) {
  EXPECT_FALSE(parseEnvelope("this is not json").has_value());
  EXPECT_FALSE(parseEnvelope("").has_value());
}

TEST(Envelope, RejectsANonObjectRoot) {
  EXPECT_FALSE(parseEnvelope(R"([1, 2, 3])").has_value());
  EXPECT_FALSE(parseEnvelope("42").has_value());
  EXPECT_FALSE(parseEnvelope(R"("hello")").has_value());
}

TEST(Envelope, RejectsMissingFields) {
  EXPECT_FALSE(parseEnvelope(R"({"room": "room", "from": "player_1"})").has_value());
  EXPECT_FALSE(parseEnvelope(R"({"event": "joined", "from": "player_1"})").has_value());
  EXPECT_FALSE(parseEnvelope(R"({"event": "joined", "room": "room"})").has_value());
  EXPECT_FALSE(parseEnvelope(R"({})").has_value());
}

TEST(Envelope, RejectsNonStringFields) {
  EXPECT_FALSE(parseEnvelope(R"({"event": 123, "room": "room", "from": "player_1"})").has_value());
  EXPECT_FALSE(
      parseEnvelope(R"({"event": "joined", "room": true, "from": "player_1"})").has_value());
  EXPECT_FALSE(parseEnvelope(R"({"event": "joined", "room": "room", "from": []})").has_value());
}

TEST(Envelope, RejectsEmptyOrOversizedFields) {
  EXPECT_FALSE(parseEnvelope(R"({"event": "", "room": "room", "from": "p1"})").has_value());
  EXPECT_FALSE(parseEnvelope(R"({"event": "joined", "room": "room", "from": ""})").has_value());
  const std::string longField(65, 'x');
  const std::string json = R"({"event": "joined", "room": "room", "from": ")" + longField + R"("})";
  EXPECT_FALSE(parseEnvelope(json).has_value());
}

TEST(Envelope, RejectsNonObjectPayload) {
  EXPECT_FALSE(parseEnvelope(R"({"event": "joined", "room": "room", "from": "p1", "payload": [1]})")
                   .has_value());
  EXPECT_FALSE(
      parseEnvelope(R"({"event": "joined", "room": "room", "from": "p1", "payload": "cards"})")
          .has_value());
}

TEST(Envelope, SerializeAlwaysCarriesAPayload) {
  const WsEnvelope envelope{"joined", "room", "server", nlohmann::json::object()};

  const std::string json = serializeEnvelope(envelope);
  EXPECT_NE(json.find("\"payload\":{}"), std::string::npos);
}

TEST(Envelope, JoinedEventCarriesPlayerSeatAndRole) {
  const WsEnvelope event = joinedEvent("room", "player_1", {"player_1"}, "host");

  EXPECT_EQ(event.event, WSEvents::kJoined);
  EXPECT_EQ(event.from, "server");
  EXPECT_EQ(event.payload.at("player_id").get<std::string>(), "player_1");
  EXPECT_EQ(event.payload.at("role").get<std::string>(), "host");
  EXPECT_EQ(event.payload.at("players").at(0).get<std::string>(), "player_1");
}

TEST(Envelope, ErrorEventCarriesCodeAndMessage) {
  const WsEnvelope event = errorEvent("room", WSErrorCodes::kRoomFull, "Room is full");

  EXPECT_EQ(event.event, WSEvents::kError);
  EXPECT_EQ(event.payload.at("code").get<std::string>(), "room_full");
  EXPECT_EQ(event.payload.at("message").get<std::string>(), "Room is full");
}

TEST(Framing, EncodePrependsABigEndianLengthHeader) {
  const std::string frame = Framing::encodeFrame("hello");

  ASSERT_EQ(frame.size(), 4 + 5);
  // 5 = payload length, big-endian: 00 00 00 05 then the payload.
  EXPECT_EQ(static_cast<unsigned char>(frame.at(0)), 0);
  EXPECT_EQ(static_cast<unsigned char>(frame.at(1)), 0);
  EXPECT_EQ(static_cast<unsigned char>(frame.at(2)), 0);
  EXPECT_EQ(static_cast<unsigned char>(frame.at(3)), 5);
  EXPECT_EQ(frame.substr(4), "hello");
}

TEST(Framing, DecodeRoundTripsSingleFrames) {
  const std::string frame = Framing::encodeFrame("{\"event\":\"joined\"}");

  FrameDecoder decoder;
  decoder.push(frame);

  const std::optional<std::string> out = decoder.next();
  if (out.has_value()) {
    EXPECT_EQ(out.value(), "{\"event\":\"joined\"}");
  } else {
    FAIL() << "expected one decoded frame";
  }
  EXPECT_FALSE(decoder.next().has_value());
  EXPECT_EQ(decoder.buffered(), 0u);
}

TEST(Framing, DecodeReassemblesFramesFromSplitChunks) {
  const std::string frame = Framing::encodeFrame("0123456789");

  FrameDecoder decoder;
  decoder.push(frame.substr(0, 3)); // partial header
  EXPECT_FALSE(decoder.next().has_value());

  decoder.push(frame.substr(3, 5)); // rest of header + partial payload
  EXPECT_FALSE(decoder.next().has_value());

  decoder.push(frame.substr(8)); // remainder of the payload
  const std::optional<std::string> out = decoder.next();
  if (out.has_value()) {
    EXPECT_EQ(out.value(), "0123456789");
  } else {
    FAIL() << "expected the reassembled frame";
  }
}

TEST(Framing, DecodeHandlesMultipleFramesInOneChunk) {
  const std::string a = Framing::encodeFrame("first");
  const std::string b = Framing::encodeFrame("second");

  FrameDecoder decoder;
  decoder.push(a + b);

  const std::optional<std::string> first = decoder.next();
  const std::optional<std::string> second = decoder.next();
  if (first.has_value()) {
    EXPECT_EQ(first.value(), "first");
  } else {
    FAIL() << "expected the first frame";
  }
  if (second.has_value()) {
    EXPECT_EQ(second.value(), "second");
  } else {
    FAIL() << "expected the second frame";
  }
}

TEST(Framing, RejectsAnOversizedFrameAtTheFramingLayer) {
  FrameDecoder decoder;

  // Length header of kMaxFrameSize + 1 = 0x00100001 (big-endian), no payload.
  std::string bytes(4, '\0');
  bytes.at(1) = static_cast<char>(0x10);
  bytes.at(3) = static_cast<char>(0x01);
  decoder.push(bytes);

  EXPECT_FALSE(decoder.next().has_value());
  EXPECT_TRUE(decoder.oversize());
  EXPECT_EQ(decoder.buffered(), 0u);
  // The stream is rejected: no further frames are produced even if more data
  // arrives.
  decoder.push(Framing::encodeFrame("later"));
  EXPECT_FALSE(decoder.next().has_value());
}

TEST(Framing, DecoderResetClearsTheOversizeState) {
  FrameDecoder decoder;
  std::string bytes(4, '\0');
  bytes.at(1) = static_cast<char>(0x10);
  bytes.at(3) = static_cast<char>(0x01);
  decoder.push(bytes);
  EXPECT_FALSE(decoder.next().has_value()); // oversize is detected on next()
  EXPECT_TRUE(decoder.oversize());

  decoder.reset();

  EXPECT_FALSE(decoder.oversize());
  decoder.push(Framing::encodeFrame("ok"));
  const std::optional<std::string> out = decoder.next();
  if (out.has_value()) {
    EXPECT_EQ(out.value(), "ok");
  } else {
    FAIL() << "expected a frame after reset";
  }
}

TEST(Framing, EncodeRefusesAPayloadLargerThanTheMaxFrameSize) {
  const std::string tooBig(Framing::kMaxFrameSize + 1, 'x');
  EXPECT_TRUE(Framing::encodeFrame(tooBig).empty());
  EXPECT_EQ(Framing::encodeFrame("").size(), Framing::kHeaderSize);
}

} // namespace
} // namespace mtgcpp::net
