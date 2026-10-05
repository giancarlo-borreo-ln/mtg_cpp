# Engine public API (`mtg_cpp_engine`)

Phase 1 split the codebase into a single reusable shared library —
`mtg_cpp_engine` — that merges the `core`, `net` and `state` layers, with no
graphics dependency (its `NEEDED` libraries are only libc/libstdc++). This file
records where the public boundary is, so the Godot GDExtension (Phase 2) knows
exactly what to bind.

## Include the whole engine

```cpp
#include "mtg_cpp/mtg_cpp.h"   // the umbrella: public types only
```

The umbrella lives in `include/` and re-exports the public headers by their
existing `core/…`, `net/…`, `state/…`, `store/…` paths (the `src/` directory is
still the include root). A consumer links `mtg_cpp_engine` and gets nlohmann +
asio transitively.

## Public types (stable, Godot-facing)

| Header | Exposes |
| --- | --- |
| `core/card.h` | `Card`, `Deck`, `DeckSummary`, `RevealCard`, `ScryfallFace`, `PlayerProfile`, `ArenaSection`, `playerProfiles()`, `cardKey()`, `cardImage()` |
| `core/board.h` | `BoardCard`, `SeatBoard`, `PlayerSeat`, `PlayerZone` (Lands, Creatures, InstantsSorceries, Graveyard, Exile, Artifacts), `classifyZone()` |
| `core/card_database.h` | `CardDatabase` (`load`, `loadFromFile`, `search`, `findByName`, `findByPrinting`) |
| `core/data_paths.h` | `DataPaths` (injectable data layout), `defaultDataDir()` |
| `store/deck_repository.h` | `DeckRepository`, `DeckReadError`, `deriveSummary()` |
| `store/profile_store.h` | `loadPlayerId()`, `savePlayerId()`, `clearPlayerId()` |
| `state/board_state.h` | `BoardState`, `BoardAction`, `applyAction()`, action builders |
| `state/session.h` | `Session` (room state machine: `drain`, `chooseDeck`, `applyLocalAction`, `requestHandReveal`, introspection; plus the local sandbox library: `enterSandbox`, `library`, `drawCard`, `playCard`, `placeFromLibrary`) |

These headers pull only the C++ standard library and each other (no SFML, no
asio, no nlohmann in their own declarations). `CardDatabase`, `DeckRepository`
and the profile store all take an explicit directory path, so the Godot port
points them at `user://` via `DataPaths::fromRoot(...)`.

## Internal (do not bind)

| Header | Why internal |
| --- | --- |
| `net/envelope.h` | wire protocol; exposes `nlohmann::json` in `WsEnvelope` |
| `net/transport.h`, `net/connection_manager.h` | expose `asio` (`AsioTransport`, accept loop) |
| `net/message_queue.h` | threading primitive (used by `Client`/`ArtCache` internals) |
| `core/card_record.h`, `core/card_serialization.h` | JSON record grammar / deck↔JSON; nlohmann |

The umbrella `mtg_cpp/mtg_cpp.h` (and therefore `state/session.h` → `net/client.h`)
pulls none of these: `Client` is a pImpl facade (`net/client.cpp` owns the
`asio::io_context` + socket), and `session.h` only *forward-declares*
`WsEnvelope` — its `sendEnvelope` takes a pre-serialized `std::string` payload.
Including the umbrella therefore does not require `asio` or `nlohmann` headers.

## Symbol visibility

`MTG_CPP_EXPORT` (`include/mtg_cpp/export.h`) is the one knob for symbol
visibility. It is a no-op by default (desktop/debug exports everything). The
GDExtension build turns on `-DMTG_CPP_HIDDEN_VISIBILITY=ON`, which sets
`-fvisibility=hidden` + `VISIBILITY_INLINES_HIDDEN` and defines
`MTG_CPP_BUILDING_ENGINE`; the binding then annotates only the exported classes.
