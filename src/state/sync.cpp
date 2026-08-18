// Sync payload mappers (M7.2).
#include "state/sync.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace mtgcpp::state {

using mtgcpp::core::PlayerSeat;
using mtgcpp::core::PlayerZone;

std::optional<nlohmann::json> toBoardUpdatePayload(const BoardAction &action) {
  switch (action.kind) {
  case BoardAction::Kind::MoveCardToZone:
    return nlohmann::json{{"action", "moveCardToZone"},
                          {"seat", mtgcpp::core::playerSeatToString(action.seat)},
                          {"cardId", action.card_id},
                          {"zone", mtgcpp::core::playerZoneToString(action.zone)}};
  case BoardAction::Kind::MoveCardToStack:
    return nlohmann::json{{"action", "moveCardToStack"}, {"cardId", action.card_id}};
  case BoardAction::Kind::TapCard:
    return nlohmann::json{{"action", "tapCard"},
                          {"seat", mtgcpp::core::playerSeatToString(action.seat)},
                          {"cardId", action.card_id}};
  case BoardAction::Kind::AddCounter:
    return nlohmann::json{{"action", "addCounter"},
                          {"seat", mtgcpp::core::playerSeatToString(action.seat)},
                          {"cardId", action.card_id}};
  case BoardAction::Kind::FlipCard:
    return nlohmann::json{{"action", "flipCard"},
                          {"seat", mtgcpp::core::playerSeatToString(action.seat)},
                          {"cardId", action.card_id}};
  case BoardAction::Kind::CreateToken:
    return nlohmann::json{{"action", "createToken"},
                          {"seat", mtgcpp::core::playerSeatToString(action.seat)},
                          {"zone", mtgcpp::core::playerZoneToString(action.zone)},
                          {"name", action.name}};
  case BoardAction::Kind::SetLife:
    return nlohmann::json{{"action", "setLife"},
                          {"seat", mtgcpp::core::playerSeatToString(action.seat)},
                          {"life", action.life}};
  default:
    // Deck selection, hands, zones, stack push/clear and board reset are not
    // synced as board_update payloads (they are announced or local-only).
    return std::nullopt;
  }
}

namespace {
// A required, non-empty string field.
std::optional<std::string> requiredString(const nlohmann::json &payload, std::string_view key) {
  if (!payload.contains(key) || !payload.at(key).is_string()) {
    return std::nullopt;
  }
  std::string value = payload.at(key).get<std::string>();
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

// A required seat ("host"/"guest") parsed into the enum.
std::optional<PlayerSeat> requiredSeat(const nlohmann::json &payload) {
  const std::optional<std::string> value = requiredString(payload, "seat");
  if (!value.has_value()) {
    return std::nullopt;
  }
  return mtgcpp::core::playerSeatFromString(value.value());
}

// A required zone parsed into the enum.
std::optional<PlayerZone> requiredZone(const nlohmann::json &payload) {
  const std::optional<std::string> value = requiredString(payload, "zone");
  if (!value.has_value()) {
    return std::nullopt;
  }
  return mtgcpp::core::playerZoneFromString(value.value());
}

} // namespace

std::optional<BoardAction> boardUpdateToAction(const nlohmann::json &payload) {
  if (!payload.contains("action") || !payload.at("action").is_string()) {
    return std::nullopt;
  }
  const std::string actionName = payload.at("action").get<std::string>();

  if (actionName == "moveCardToZone") {
    const std::optional<PlayerSeat> seat = requiredSeat(payload);
    const std::optional<std::string> cardId = requiredString(payload, "cardId");
    const std::optional<PlayerZone> zone = requiredZone(payload);
    if (!seat.has_value() || !cardId.has_value() || !zone.has_value()) {
      return std::nullopt;
    }
    BoardAction action = moveCardToZone(seat.value(), cardId.value(), zone.value());
    action.sync = true;
    return action;
  }
  if (actionName == "moveCardToStack") {
    const std::optional<std::string> cardId = requiredString(payload, "cardId");
    if (!cardId.has_value()) {
      return std::nullopt;
    }
    BoardAction action = moveCardToStack(cardId.value());
    action.sync = true;
    return action;
  }
  if (actionName == "tapCard" || actionName == "addCounter" || actionName == "flipCard") {
    const std::optional<PlayerSeat> seat = requiredSeat(payload);
    const std::optional<std::string> cardId = requiredString(payload, "cardId");
    if (!seat.has_value() || !cardId.has_value()) {
      return std::nullopt;
    }
    BoardAction action;
    if (actionName == "tapCard") {
      action.kind = BoardAction::Kind::TapCard;
    } else if (actionName == "addCounter") {
      action.kind = BoardAction::Kind::AddCounter;
    } else {
      action.kind = BoardAction::Kind::FlipCard;
    }
    action.seat = seat.value();
    action.card_id = cardId.value();
    action.sync = true;
    return action;
  }
  if (actionName == "createToken") {
    const std::optional<PlayerSeat> seat = requiredSeat(payload);
    const std::optional<PlayerZone> zone = requiredZone(payload);
    const std::optional<std::string> name = requiredString(payload, "name");
    if (!seat.has_value() || !zone.has_value() || !name.has_value()) {
      return std::nullopt;
    }
    BoardAction action = createToken(seat.value(), zone.value(), name.value());
    action.sync = true;
    return action;
  }
  if (actionName == "setLife") {
    const std::optional<PlayerSeat> seat = requiredSeat(payload);
    if (!seat.has_value() || !payload.contains("life") || !payload.at("life").is_number()) {
      return std::nullopt;
    }
    BoardAction action = setLife(seat.value(), payload.at("life").get<int>());
    action.sync = true;
    return action;
  }
  return std::nullopt; // unknown action name
}

} // namespace mtgcpp::state
