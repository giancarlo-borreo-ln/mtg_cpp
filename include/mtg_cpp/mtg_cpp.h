// mtg_cpp engine — public API (Phase 1).
//
// This is the single header a consumer links against when they want the whole
// engine (the Godot GDExtension includes exactly this). It re-exports the
// engine's public surface and nothing else:
//
//   * value types:  Card, Deck, BoardCard, SeatBoard, PlayerSeat/PlayerZone
//   * data:         CardDatabase (search/load), DeckRepository, ProfileStore
//   * paths:        DataPaths (injectable data layout)
//   * board:        BoardState, BoardAction, applyAction
//   * networking:   Client, Server, Session (the room state machine)
//
// --- boundary contract -----------------------------------------------------
// Public (stable, Godot-facing): the types listed above. Their headers pull
// only the C++ standard library and each other.
// Internal (do not include from Godot bindings): net/envelope.h and the payload
// wire types (they expose nlohmann::json), net/transport.h and
// net/connection_manager.h (they expose asio). Session/Client currently still
// #include these internally, so a full third-party-free header is tracked as
// the remaining Phase-1 item (1.4); the signatures Godot uses (drain/chooseDeck/
// applyLocalAction/board/reveal) do not mention asio or nlohmann themselves.
#pragma once

#include "mtg_cpp/export.h"

#include "core/board.h"
#include "core/card.h"
#include "core/card_database.h"
#include "core/data_paths.h"
#include "state/board_state.h"
#include "state/session.h"
#include "store/deck_repository.h"
#include "store/profile_store.h"
