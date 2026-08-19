// Card/Deck <-> JSON serialization (M7.3).
#include "core/card_serialization.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace mtgcpp::core {

namespace {

// nlohmann's `.value()` throws on a type mismatch; these non-throwing readers
// keep deserialization of untrusted input (wire deck payloads, deck files)
// defensive — a malformed field degrades to empty, never to a crash.
std::string readString(const nlohmann::json &obj, const char *key) {
  if (obj.is_object() && obj.contains(key) && obj.at(key).is_string()) {
    return obj.at(key).get<std::string>();
  }
  return {};
}

} // namespace

nlohmann::json cardToJson(const Card &card) {
  nlohmann::json cmc = nullptr;
  if (card.cmc.has_value()) {
    cmc = card.cmc.value();
  }
  nlohmann::json faces = nlohmann::json::array();
  for (const ScryfallFace &face : card.card_faces) {
    faces.push_back({{"name", face.name}, {"image_uris", face.image_uris}});
  }
  return nlohmann::json{{"scryfall_id", card.scryfall_id},
                        {"name", card.name},
                        {"set_code", card.set_code},
                        {"set_name", card.set_name},
                        {"collector_number", card.collector_number},
                        {"quantity", card.quantity},
                        {"section", arenaSectionToString(card.section)},
                        {"mana_cost", card.mana_cost},
                        {"cmc", cmc},
                        {"colors", card.colors},
                        {"type_line", card.type_line},
                        {"image_uris", card.image_uris},
                        {"card_faces", faces}};
}

Card cardFromJson(const nlohmann::json &obj) {
  Card card;
  card.scryfall_id = readString(obj, "scryfall_id");
  card.name = readString(obj, "name");
  card.set_code = readString(obj, "set_code");
  card.set_name = readString(obj, "set_name");
  card.collector_number = readString(obj, "collector_number");
  if (obj.is_object() && obj.contains("quantity") && obj.at("quantity").is_number_integer()) {
    card.quantity = obj.at("quantity").get<int>();
  }
  card.mana_cost = readString(obj, "mana_cost");
  if (obj.is_object() && obj.contains("section") && obj.at("section").is_string()) {
    const std::optional<ArenaSection> section =
        arenaSectionFromString(obj.at("section").get<std::string>());
    if (section.has_value()) {
      card.section = section.value();
    }
  }
  if (obj.is_object() && obj.contains("cmc") && obj.at("cmc").is_number()) {
    card.cmc = obj.at("cmc").get<float>();
  }
  if (obj.is_object() && obj.contains("colors") && obj.at("colors").is_array()) {
    for (const nlohmann::json &color : obj.at("colors")) {
      if (color.is_string()) {
        card.colors.push_back(color.get<std::string>());
      }
    }
  }
  card.type_line = readString(obj, "type_line");
  if (obj.is_object() && obj.contains("image_uris") && obj.at("image_uris").is_object()) {
    for (const auto &entry : obj.at("image_uris").items()) {
      if (entry.value().is_string()) {
        card.image_uris.emplace(entry.key(), entry.value().get<std::string>());
      }
    }
  }
  if (obj.is_object() && obj.contains("card_faces") && obj.at("card_faces").is_array()) {
    for (const nlohmann::json &faceObj : obj.at("card_faces")) {
      if (!faceObj.is_object()) {
        continue;
      }
      ScryfallFace face;
      face.name = readString(faceObj, "name");
      if (faceObj.contains("image_uris") && faceObj.at("image_uris").is_object()) {
        for (const auto &entry : faceObj.at("image_uris").items()) {
          if (entry.value().is_string()) {
            face.image_uris.emplace(entry.key(), entry.value().get<std::string>());
          }
        }
      }
      card.card_faces.push_back(std::move(face));
    }
  }
  return card;
}

nlohmann::json deckToJson(const Deck &deck) {
  nlohmann::json cards = nlohmann::json::array();
  for (const Card &card : deck.cards) {
    cards.push_back(cardToJson(card));
  }
  nlohmann::json preview = nullptr;
  if (deck.preview_image.has_value()) {
    preview = deck.preview_image.value();
  }
  return nlohmann::json{{"id", deck.id},
                        {"name", deck.name},
                        {"format", deck.format},
                        {"total_cards", deck.total_cards},
                        {"unique_cards", deck.unique_cards},
                        {"preview_image", preview},
                        {"created_at", deck.created_at},
                        {"updated_at", deck.updated_at},
                        {"cards", cards}};
}

Deck deckFromJson(const nlohmann::json &obj) {
  Deck deck;
  deck.id = readString(obj, "id");
  deck.name = readString(obj, "name");
  deck.format = readString(obj, "format");
  if (deck.format.empty()) {
    deck.format = "Other";
  }
  if (obj.is_object() && obj.contains("total_cards") && obj.at("total_cards").is_number_integer()) {
    deck.total_cards = obj.at("total_cards").get<int>();
  }
  if (obj.is_object() && obj.contains("unique_cards") &&
      obj.at("unique_cards").is_number_integer()) {
    deck.unique_cards = obj.at("unique_cards").get<int>();
  }
  const std::string preview = readString(obj, "preview_image");
  if (!preview.empty()) {
    deck.preview_image = preview;
  }
  deck.created_at = readString(obj, "created_at");
  deck.updated_at = readString(obj, "updated_at");
  if (obj.is_object() && obj.contains("cards") && obj.at("cards").is_array()) {
    for (const nlohmann::json &cardObj : obj.at("cards")) {
      if (cardObj.is_object()) {
        deck.cards.push_back(cardFromJson(cardObj));
      }
    }
  }
  return deck;
}

} // namespace mtgcpp::core
