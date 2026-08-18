// Board-update wire payloads, ported from the webapp's state/board-sync.ts
// (M7.2).
//
// Boards are symmetric across both clients (identical card ids per seat), so a
// single battlefield change is sent as a minimal JSON payload and the opponent
// replays the exact same action. Only the 7 battlefield mutations sync:
// moveCardToZone / moveCardToStack / tapCard / addCounter / flipCard /
// createToken / setLife. Everything else (decks, hands, stack push/clear) is
// either announced separately or local-only.
//
// The `sync` marker: an action rebuilt from the wire carries `sync = true` so
// the receiving session applies it locally but never re-sends it (no echo).
#pragma once

#include "state/board_state.h"

#include <nlohmann/json_fwd.hpp>
#include <optional>

namespace mtgcpp::state {

// Serialize a local board action into its minimal wire payload. Returns
// nullopt when the action is not one of the 7 syncable mutations.
std::optional<nlohmann::json> toBoardUpdatePayload(const BoardAction &action);

// Rebuild the SAME board action from an inbound `board_update` payload, with
// `sync = true` set. Returns nullopt when the payload is malformed (unknown
// action, invalid seat/zone, missing/empty fields, non-numeric life) — the
// relay never inspects payloads, so every inbound message is validated here.
std::optional<BoardAction> boardUpdateToAction(const nlohmann::json &payload);

} // namespace mtgcpp::state
