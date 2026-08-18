// Card/Deck <-> JSON serialization (M7.3).
#include "core/card_serialization.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace mtgcpp::core {

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
  card.scryfall_id = obj.value("scryfall_id", "");
  card.name = obj.value("name", "");
  card.set_code = obj.value("set_code", "");
  card.set_name = obj.value("set_name", "");
  card.collector_number = obj.value("collector_number", "");
  card.quantity = obj.value("quantity", 1);
  card.mana_cost = obj.value("mana_cost", "");
  if (obj.contains("section") && obj.at("section").is_string()) {
    const std::optional<ArenaSection> section =
        arenaSectionFromString(obj.at("section").get<std::string>());
    if (section.has_value()) {
      card.section = section.value();
    }
  }
  if (obj.contains("cmc") && obj.at("cmc").is_number()) {
    card.cmc = obj.at("cmc").get<float>();
  }
  if (obj.contains("colors")) {
    for (const nlohmann::json &color : obj.at("colors")) {
      card.colors.push_back(color.get<std::string>());
    }
  }
  card.type_line = obj.value("type_line", "");
  if (obj.contains("image_uris")) {
    for (const auto &entry : obj.at("image_uris").items()) {
      card.image_uris.emplace(entry.key(), entry.value().get<std::string>());
    }
  }
  if (obj.contains("card_faces")) {
    for (const nlohmann::json &faceObj : obj.at("card_faces")) {
      ScryfallFace face;
      face.name = faceObj.value("name", "");
      if (faceObj.contains("image_uris")) {
        for (const auto &entry : faceObj.at("image_uris").items()) {
          face.image_uris.emplace(entry.key(), entry.value().get<std::string>());
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
  deck.id = obj.value("id", "");
  deck.name = obj.value("name", "");
  deck.format = obj.value("format", "Other");
  deck.total_cards = obj.value("total_cards", 0);
  deck.unique_cards = obj.value("unique_cards", 0);
  if (obj.contains("preview_image") && obj.at("preview_image").is_string()) {
    deck.preview_image = obj.at("preview_image").get<std::string>();
  }
  deck.created_at = obj.value("created_at", "");
  deck.updated_at = obj.value("updated_at", "");
  if (obj.contains("cards")) {
    for (const nlohmann::json &cardObj : obj.at("cards")) {
      deck.cards.push_back(cardFromJson(cardObj));
    }
  }
  return deck;
}

} // namespace mtgcpp::core
