# mtg_cpp — Godot shell (Phase 2)

A Godot 4 project that hosts the mtg_cpp engine as a GDExtension. All game
logic lives in the C++ engine (`mtg_cpp_engine`, built by the top-level CMake);
this directory provides the Godot project, the GDExtension bindings, and the
scenes/scripts that will become the UI (Phase 3).

## Pinned versions (todo 2.1)

| Component | Version | Why |
| --- | --- | --- |
| Godot | `4.5-stable` | matches the godot-cpp ABI below |
| godot-cpp | `godot-4.5-stable` | GDExtension ABI must match the editor/export |
| ABI files | `godot/gdextension/{extension_api.json,gdextension_interface.h}` | pinned to 4.5, committed so configure never depends on a Godot install |

The ABI files were generated from the Godot 4.5 binary
(`godot --headless --dump-extension-api`) and copied from godot-cpp; they are
the compatibility contract, so they are committed.

## Layout

```
godot/
  project.godot            # Godot project config
  mtg_cpp.gdextension      # maps the entry symbol + platform libraries
  gdextension/             # pinned extension_api.json + gdextension_interface.h
  extension/               # the C++ binding (GDExtension shared library)
    CMakeLists.txt
    src/register_types.cpp # entry point: GDREGISTER_CLASS for each Mtgcpp*
    src/mtg_cpp_bindings.* # the binding classes
  scripts/                 # autoloads + screen scripts
    palette.gd             # modern design tokens (autoload Palette)
    ui.gd                  # themed control factories (autoload UI)
    app_state.gd           # shared services + navigation (autoload AppState)
    app.gd                 # app shell: screen router + theme
    home.gd / deck_editor.gd / lobby.gd / table.gd
  scenes/                  # app.tscn + home/deck_editor/lobby/table.tscn
  tests/                   # headless smoke tests + fixtures
  assets/fonts/            # bundled PT Serif (unused since the modern redesign)
  bin/                     # build output (git-ignored)
```

## Build

Build the engine first (top-level), then the extension:

```bash
# 1. engine (produces libmtg_cpp_engine.so)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# 2. GDExtension (fetches godot-cpp, links the engine)
cmake -S godot/extension -B build-godot -G Ninja \
  -DMTG_CPP_ENGINE_LIBRARY=$PWD/build/libmtg_cpp_engine.so \
  -DMTG_CPP_ENGINE_INCLUDE_DIR=$PWD
cmake --build build-godot
```

## Run the smoke test (gate 2.9)

```bash
GODOT_BIN=/path/to/Godot_v4.5-stable_linux.x86_64 bash scripts/godot-smoke.sh
```

It prints `SMOKE OK` and exits 0 on success. The script writes the
`res://mtg_cpp.gdextension` entry into `.godot/extension_list.cfg` first (the
game runtime reads the registered-extension list from that cache; the headless
editor scan that normally populates it crashes in Godot 4.5 — a Godot bug
unrelated to this project).

## Binding surface

| Godot class | Wraps | Notes |
| --- | --- | --- |
| `MtgcppCard` | `core::Card` | read-only value view (name, scryfall_id, set, …) |
| `MtgcppDeck` | `core::Deck` | name/format/cards, plus `id` after `create` |
| `MtgcppDataPaths` | `core::DataPaths` | `from_root(root)` / `default_app()` + derived paths |
| `MtgcppCardDatabase` | `core::CardDatabase` | `search`, `size`, `load_from_file` (sync), `load_from_file_async`, `import_deck` |
| `MtgcppDeckRepository` | `core::DeckRepository` | `open(root)` + `create`/`read`/`list`/`list_summaries`/`update`/`remove` |
| `MtgcppBoardState` | `state::BoardState` | `initial()` + `life(seat)` |
| `MtgcppProfiles` | `core::playerProfiles` | static `list()` → `[{id, name}]` |
| `MtgcppProfileStore` | `core/profile_store` | static `load`/`save`/`clear` |
| `MtgcppDeckEditor` | `core/deck_editor` | pure `add_card`/`remove_card`/`set_quantity`/`totals` |
| `MtgcppImportPreview` | `core/import_preview` | `resolved`/`missing`/`select_missing`/`replace_missing`/`remove_missing`/`confirm` |
| `MtgcppArena` | `core/arena` | static `to_text` (Arena export) |
| `MtgcppSession` | `net::Server`/`Client` + `state::Session` | `create_room`/`join`/`pump`/`choose_deck`, board view (`hand`/`zone_cards`/`stack_cards`/`life`), actions (`tap`/`add_counter`/`flip`/`create_token`/`move_to_zone`/`move_to_stack`/`set_life`), hand reveal |

### Error handling (todo 2.5)

No C++ exception crosses into Godot. Fallible calls return a `Dictionary`:

```gdscript
var r: Dictionary = repo.create(deck)
if r["ok"]:
    var stored := r["deck"] as MtgcppDeck
else:
    print("failed: ", r["error"])
```

### Threading (todo 2.6 / 2.7)

`MtgcppCardDatabase.load_from_file_async` parses on a worker thread and emits
`load_finished(ok, error, loaded, rejected)` on the main thread, so the multi-
minute first parse never blocks the UI. The single-owner rule is preserved: the
worker never touches `db_`, only a private pending buffer that `_commit_loaded()`
moves into place on the main thread.

### Paths / persistence (todo 2.8)

Every binding method that takes a path accepts Godot `res://`, `user://`, or
absolute paths and globalizes them at the boundary (`to_os_path`). The engine
still deals in real filesystem paths; map the app-data root to
`MtgcppDataPaths.from_root("user://")`.
