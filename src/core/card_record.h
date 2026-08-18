// Shared per-record parsing/validation/conversion for the Scryfall bulk JSONL
// (M2.3 `check-cards` + M2.4 CardDatabase loader).
//
// Both consumers stream the same one-JSON-object-per-line artifact, so the
// record grammar lives here once: parse the line, type-check every field the
// loader reads, and convert a valid record into a typed `Card`. A record that
// passes `parseCardRecord` is guaranteed to convert cleanly via `toCard`.
#pragma once

#include "core/card.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace mtgcpp::core {

// Parse + validate one JSONL line. On success returns the validated record
// object and leaves `reason` empty; on failure returns nullopt with `reason`
// set to a short human-readable explanation.
std::optional<nlohmann::json> parseCardRecord(const std::string &line, std::string &reason);

// Convert a validated Scryfall record into the typed `Card` the loader and
// importer consume (mirrors deck_importer.py `_to_parsed_card`). Precondition:
// `record` passed `parseCardRecord`, so fields are present with the right
// types and the accessors here (.at() / value()) are bounds-safe.
Card toCard(const nlohmann::json &record);

} // namespace mtgcpp::core
