// One Scryfall bulk record: parse, type-check, and convert to a `Card`.
//
// The field checks are the contract between the raw artifact and every typed
// consumer: `check-cards` (M2.3) counts pass/fail, and the CardDatabase loader
// (M2.4) indexes each passing record. Keeping the grammar here means the two
// tools can never drift apart in what they consider a usable card.

#include "core/card_record.h"

#include <string>
#include <utility>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace mtgcpp::core {

namespace {

// A present-and-non-empty string the indexes are built on (`name`, `set`,
// `collector_number`, `id`). Sets `reason` when it is missing/empty/not text.
bool requireNonEmptyString(const json &record, const char *name, std::string &reason) {
  if (!record.contains(name)) {
    reason = std::string(name) + " is missing";
    return false;
  }
  const json &value = record.at(name);
  if (!value.is_string()) {
    reason = std::string(name) + " is not a string";
    return false;
  }
  if (value.get<std::string>().empty()) {
    reason = std::string(name) + " is empty";
    return false;
  }
  return true;
}

// A field the loader reads as text; absent is fine, but present must be text.
bool optionalString(const json &record, const char *name, std::string &reason) {
  if (record.contains(name) && !record.at(name).is_string()) {
    reason = std::string(name) + " is not a string";
    return false;
  }
  return true;
}

// `colors`: array of color-letter strings, absent or empty for lands/artifacts.
bool optionalStringArray(const json &record, const char *name, std::string &reason) {
  if (!record.contains(name)) {
    return true;
  }
  const json &value = record.at(name);
  if (!value.is_array()) {
    reason = std::string(name) + " is not an array";
    return false;
  }
  for (const json &entry : value) {
    if (!entry.is_string()) {
      reason = std::string(name) + " contains a non-string";
      return false;
    }
  }
  return true;
}

// `image_uris`: object mapping a size key to an art URL string.
bool optionalStringMap(const json &record, const char *name, std::string &reason) {
  if (!record.contains(name)) {
    return true;
  }
  const json &value = record.at(name);
  if (!value.is_object()) {
    reason = std::string(name) + " is not an object";
    return false;
  }
  for (const auto &entry : value.items()) {
    if (!entry.value().is_string()) {
      reason = std::string(name) + " contains a non-string";
      return false;
    }
  }
  return true;
}

// Validate every field the loader reads off a Scryfall record. Returns false
// and sets `reason` on the first problem found.
bool validateCardRecord(const json &record, std::string &reason) {
  if (!record.is_object()) {
    reason = "record is not a JSON object";
    return false;
  }
  if (!requireNonEmptyString(record, "id", reason) ||
      !requireNonEmptyString(record, "name", reason) ||
      !requireNonEmptyString(record, "set", reason) ||
      !requireNonEmptyString(record, "collector_number", reason) ||
      !optionalString(record, "set_name", reason) || !optionalString(record, "mana_cost", reason) ||
      !optionalString(record, "type_line", reason) ||
      !optionalStringArray(record, "colors", reason) ||
      !optionalStringMap(record, "image_uris", reason)) {
    return false;
  }

  if (record.contains("cmc") && !record.at("cmc").is_null() && !record.at("cmc").is_number()) {
    reason = "cmc is not a number or null";
    return false;
  }

  if (record.contains("card_faces")) {
    const json &faces = record.at("card_faces");
    if (!faces.is_array()) {
      reason = "card_faces is not an array";
      return false;
    }
    for (const json &face : faces) {
      if (!face.is_object()) {
        reason = "card_faces entry is not an object";
        return false;
      }
      if (!optionalString(face, "name", reason) || !optionalStringMap(face, "image_uris", reason)) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

std::optional<json> parseCardRecord(const std::string &line, std::string &reason) {
  json record;
  try {
    record = json::parse(line);
  } catch (const json::parse_error &) {
    reason = "line is not valid JSON";
    return std::nullopt;
  }
  if (!validateCardRecord(record, reason)) {
    return std::nullopt;
  }
  reason.clear();
  return record;
}

Card toCard(const json &record) {
  Card card;
  card.scryfall_id = record.at("id").get<std::string>();
  card.name = record.at("name").get<std::string>();
  card.set_code = record.at("set").get<std::string>();
  card.set_name = record.value("set_name", "");
  card.collector_number = record.at("collector_number").get<std::string>();
  card.mana_cost = record.value("mana_cost", "");
  if (record.contains("cmc") && record.at("cmc").is_number()) {
    card.cmc = record.at("cmc").get<float>();
  }
  if (record.contains("colors")) {
    for (const json &color : record.at("colors")) {
      card.colors.push_back(color.get<std::string>());
    }
  }
  card.type_line = record.value("type_line", "");
  if (record.contains("image_uris")) {
    for (const auto &entry : record.at("image_uris").items()) {
      card.image_uris.emplace(entry.key(), entry.value().get<std::string>());
    }
  }
  if (record.contains("card_faces")) {
    for (const json &face : record.at("card_faces")) {
      ScryfallFace parsedFace;
      parsedFace.name = face.value("name", "");
      if (face.contains("image_uris")) {
        for (const auto &entry : face.at("image_uris").items()) {
          parsedFace.image_uris.emplace(entry.key(), entry.value().get<std::string>());
        }
      }
      card.card_faces.push_back(std::move(parsedFace));
    }
    // Double-faced cards usually carry no top-level image_uris; use the front
    // face's art instead, exactly like deck_importer.py `_to_parsed_card`, so
    // cardImage() still resolves an image.
    if (card.image_uris.empty() && !card.card_faces.empty()) {
      card.image_uris = card.card_faces.at(0).image_uris;
    }
  }
  return card;
}

} // namespace mtgcpp::core
