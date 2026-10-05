# mtg_cpp — MTG VTT Sandbox, C++ Desktop Rewrite

A two-player Magic: The Gathering virtual tabletop, translated from the Angular/
FastAPI webapp (`../mtg_with_ste`) into a single cross-platform C++20 executable.
Graphics styled after the 1990s MicroProse *Shandalar*. The second player connects
directly to the host's app over TCP — no separate server process.

> Status: **Sprint 9 (M9.1–M9.5) + Sprint 10 (M10.1–M10.3) + Sprint 11 (M11.1–M11.3) + Sprint 12 (M12.1–M12.3)** — card models (`core/card.h`), board model +
> moves (`core/board.h`), Arena deck parser (`core/deck_parser.h`) and formatter
> (`core/arena.h`), DB integrity tool (`mtg_cpp check-cards`), local card
> database (`core/card_database.h`, incl. `search()` for name + set/collector)
> + importer (`core/importer.h`), pure deck-editor ops (`core/deck_editor.h`)
> + import-preview state machine (`core/import_preview.h`), deck repository
> (`store/deck_repository.h`) + persistent player profile
> (`store/profile_store.h`), and the SFML app shell (`ui/app.h` +
> `ui/theme.h` + `ui/layout.h` + `ui/widgets/` + `ui/screens/`): screen router,
> bundled OFL fonts (PT Serif), Button/TextInput/ListView/DeckListView/
> CardList/DeckBuilderList/TextArea/PreviewList/CardView/ZoneView/ContextMenu
> widgets, and four real screens: the **Home screen** (profile picker, deck
> vault with master-detail + per-row Delete + Edit), the **Deck Editor** (search
> the local card database, build a live deck, **import an Arena export** with a
> guarded preview, export to Arena text via the OS clipboard, save/load through
> the repository), the **Lobby** (Sprint 8): **Create Room** starts the embedded
> relay on the host and shows the shared `IP:PORT`; **Join** connects to a
> friend's `IP:PORT`; both sides pick a deck from the vault, see who is
> connected, and auto-advance to the table once both decks are known — and, new
> in Sprint 9, the **Table** (M9.1–M9.5): a fully procedural **Shandalar
> battlefield** — velvet playmat + parchment noise + gold filigree painted into
> `sf::Texture`s at runtime (`ui/theme.h` M9.1), pure slot math (`ui/layout.h`
> M9.2: arched hand strips top/bottom, two 3-column zone grids, the Stack as the
> only overlap, life rings at the corners), card/zone/context-menu widgets
> (`ui/widgets/card_view.h`, `zone_view.h`, `context_menu.h` M9.3), and
> **click + command** interaction (M9.4): click to select, then right-click
> menu or keyboard verbs — **T** tap, **C** +1/+1 counter, **K** token,
> **F** flip, **S** to Stack, **M** move-to-zone, plus editable life totals
> (M9.5, click a ring, type, Enter; the opponent's half is read-only). The
> window is resizable: a single relayout() pass re-flows chrome and screens.
> Networking core (`net/`): faithful `{event, room, from, payload}` **envelope**
> (`net/envelope.h`, strict schema validation + 1 MiB length-prefixed framing),
> the thread-safe bounded **message queue** + `ITransport` seam
> (`net/message_queue.h`, `net/transport.h`), the **embedded dumb relay**
> (`net/server.h` over a real Asio TCP listener, `net/transport.cpp`): seats 2
> players in arrival order (`player_1` host / `player_2` guest), authoritative
> `from`, room-full rejection, hand-reveal flow, verbatim
> `board_update`/`deck_selected` relay to the opponent's private channel. The
> **client** (`net/client.h`) connects to the host's `IP:PORT`; `net/address.h`
> resolves the address the host shares. **State machine (`state/`):**
> value-semantic `BoardState` + pure reducer (`state/board_state.h`, no-op
> short-circuits), the 7 syncable board-update payloads with strict validation +
> the `sync` echo-suppression marker (`state/sync.h`), shared `Deck`/`Card` ↔
> JSON (`core/card_serialization.h`), and the **room Session**
> (`state/session.h`): connect/join, choose + announce a deck (re-announced so
> late joiners learn it, and minted even when chosen before the join lands),
> learn the opponent's deck, hand-reveal consent, and the ready→table
> transition. Headless loopback tests prove both sides replay the same actions
> to byte-identical battlefields with no echo. **Sprint 10 — M10.1 runtime art
> cache (`ui/art_cache.h`):** card art is downloaded on demand over HTTPS (cpr,
> behind an injectable fetcher) in a worker thread and persisted as PNGs into
> the app-data `art_cache/` dir, keyed by scryfall id + pixel size with atomic
> writes; the table (`ui/screens/table_screen.h`) requests art for every
> face-up card, drains completed downloads each frame via `pumpArt()`, and keeps
> the procedural front texture as the fallback while a card is still
> downloading, offline, or when the cache is disabled. **Sprint 10 — M10.2 wire
> hardening:** every inbound payload is validated through non-throwing readers
> before use (`net/envelope.h` `readPayload*`, safe deck/reveal parsing), so a
> malformed frame is rejected or ignored, never allowed to crash the app; the
> relay **drops** the connection on a malformed frame (and on oversize, with the
> seat freed); a redundant `deck_selected` re-announce no longer re-mints the
> opponent's seat (which used to un-tap played cards — boards can't drift); an
> unexpected socket loss fans out to the peer as a `connection_lost` event so
> the host shutting down cleanly tears the room down on both sides; and the
> relay's accept loop starts before its io thread, so a first connection is
> never dropped under load. **Sprint 10 — M10.3 polish + soak:** per-screen
> cursor states (`ui/cursor.h` — each screen exposes a pure `cursorAt(point)`
> hit-test; the App applies Hand over clickable cards/rows/buttons, Text over
> editable fields, Arrow elsewhere); a **soak test** (`tests/state/soak_test.cpp`)
> plays 400 rapid fire-and-forget actions over loopback and asserts byte-identical
> battlefields with no drift, no echo, and no leaks at every checkpoint; and a
> full synced match is played while both tables' art caches are live, proving
> the procedural front is replaced by real art on both sides. **Sprint 12 — integration
> harness:** `App::injectEvent`/`App::pump` (a scripting seam that pushes
> synthetic mouse/key events through the exact window-loop handler, minus window
> side effects) and `src/integration.cpp` (`mtg_cpp_integration`) which drives
> the whole app — profile picker, deck editor search/add/save + import preview,
> lobby create-room → choose deck, a real loopback guest, and table
> select/Tap/Stack/life mirrored to the guest — by simulating mouse presses.
> Run it via `/tmp/todocpp/integration.sh` (builds + runs + exits non-zero on
> any failed check; no display needed). See `/tmp/todocpp/SPRINTS.md` for the
> plan.

## Card data (Scryfall bulk export)

The app resolves decks against a local copy of the full Scryfall card database —
no runtime card API. Scryfall now ships this as a gzipped **JSONL** stream
(`default-cards-<timestamp>.jsonl.gz`, ~77 MB compressed / ~500-600 MB
decompressed), refreshed daily and discovered via `/bulk-data`.

Fetch it once (requires `curl`, `gzip`, `python3` on Linux):

```bash
scripts/fetch_card_db.sh        # download, verify, decompress to data/default-cards.jsonl
scripts/fetch_card_db.sh --refresh   # force a re-download even if up to date
# Windows:
# .\scripts\fetch_card_db.ps1
```

Provenance lands in `data/manifest.json` (fetch date, Scryfall `updated_at`,
URL, sizes, SHA-256 of both artifacts). Re-running is a no-op while the file is
current. `data/` is git-ignored. Card data is CC0; card *art* is not — the
runtime art cache (Sprint 10) is for personal play and must not be redistributed.

The first launch parses the ~600 MB JSONL into memory (~2–3 minutes; the app
says so on the terminal). That parse is written to a binary sidecar cache in the
app-data dir (`card_db.cache`), keyed to the JSONL's size + mtime, so every
later launch loads in a couple of seconds. Re-fetching the database
automatically invalidates the cache.

## Dependencies

| Library   | Version | Purpose                          | Supplied by        |
| --------- | ------- | -------------------------------- | ------------------ |
| SFML      | 2.6     | 2D graphics / window / input     | prebuilt dir, system, or FetchContent |
| nlohmann/json | 3.11.3 | JSON (cards, envelopes, decks)   | FetchContent       |
| Asio      | 1.30    | TCP networking (Sprint 6)        | FetchContent       |
| cpr       | 1.10.5  | HTTPS card-art downloads (Sprint 10) | FetchContent (builds curl + OpenSSL if no system curl) |
| GoogleTest| 1.15.2  | Unit tests                       | FetchContent       |

## Building

Requires a C++20 compiler, CMake ≥ 3.24, and network access on first configure
(FetchContent downloads the sources). Ninja is optional but recommended.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/mtg_cpp            # opens the skeleton window; Esc closes it
```

### SFML on Linux without X11 dev headers

SFML builds from source only if the X11/GL/freetype **development** headers are
installed. If you only have the runtime libraries, point CMake at a prebuilt SFML:

```bash
# e.g. SFML 2.6.1 Linux prebuilt from the SFML GitHub releases page
curl -sL -o sfml.tar.gz https://github.com/SFML/SFML/releases/download/2.6.1/SFML-2.6.1-linux-gcc-64-bit.tar.gz
mkdir -p /opt/sfml && tar -xzf sfml.tar.gz -C /opt/sfml --strip-components=1

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMTG_CPP_SFML_DIR=/opt/sfml
export LD_LIBRARY_PATH=/opt/sfml/lib:$LD_LIBRARY_PATH
```

### Sanitizers (Linux)

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMTG_CPP_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

### Fuzzing (M11.1)

Structure-aware fuzz targets cover the two untrusted-input paths: the Arena
deck parser and the network envelope/framing. With clang they link libFuzzer
(`-fsanitize=fuzzer`, which provides `main`); with GCC the same target runs a
fixed-budget mutation driver over the seed corpus. `scripts/fuzz.sh` builds the
ASan/UBSan fuzzers and runs each for a fixed time budget — any crash or UB
fails the gate.

```bash
bash scripts/fuzz.sh 30 7      # 30 seconds per target, seed 7
```

The seed corpus + a fixed set of adversarial inputs are also replayed in the
unit suite (`tests/fuzz/fuzz_corpus_test.cpp`), so every CI runner asserts the
no-crash/no-UB property without running the fuzzers. Fuzzing has so far found
three real bugs, all fixed with regression tests: a signed-overflow quantity
sum, a null-pointer `memcpy` on zero-length input, and a regex stack overflow
on pathologically long card lines (now length-capped).

### ThreadSanitizer (M11.1)

`MTG_CPP_ENABLE_TSAN` builds with ThreadSanitizer (exclusive of ASan/UBSan);
the loopback/session suites run clean under it (a CI job does the same).

```bash
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMTG_CPP_ENABLE_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

## Static analysis & CI

- `.clang-tidy` — curated correctness/memory-safety checks, promoted to errors
  (see `docs/conventions.md`).
- `.clang-format` — enforced style (`clang-format --dry-run --Werror`).
- `scripts/ci-linux.sh [clang|gcc]` — build + ASan/UBSan test + format + tidy.
- `scripts/ci-windows.ps1` — MSVC Debug build + test.
- `.github/workflows/ci.yml` — Linux (GCC + Clang, sanitizers, clang-tidy, a
  fixed-budget fuzz step), a Linux TSan runner, and Windows (MSVC Debug).

## Packaging (M11.2)

The app ships as a relocatable bundle (no working-directory assumptions): the
window/taskbar icon is painted procedurally (`paintAppIcon`), and the card
database is resolved next to the executable first, then from `./data`.

```bash
bash scripts/package-linux.sh           # dist/mtg_cpp-<version>-linux-x86_64.tar.gz
MTG_CPP_BUNDLE_DATA=1 bash scripts/package-linux.sh   # also bundle the local card DB
powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1  # dist/*.zip
```

The bundle contains `bin/` (the executable + a self-contained launcher), `lib/`
(runtime shared libraries, `$ORIGIN/../lib` rpath), `assets/` (fonts + icon) and
optionally `data/`. A clean install runs `bin/mtg_cpp.sh` (Linux) or the exe
(Windows); if `data/` was not bundled, run `scripts/fetch_card_db.sh` in the
install directory first.

## Multiplayer walkthrough (Sprint 8)

The lobby needs two instances on a network that can reach each other (same
machine, or LAN machines with the host port `7500` reachable):

1. In the first instance press **3 (Lobby)**, then **Create Room**. The host
   embeds the relay and shows the address to share (e.g. `192.168.1.10:7500`).
2. In the second instance open **Lobby** and type that `IP:PORT` into the join
   field, then **Join Room**.
3. Both sides pick a deck from the vault. As soon as both decks are known the
   lobby says "Both players ready" and the app switches to the **Table**.
4. **Leave Room** disconnects and returns to the connect panel (the peer is
   told the room went back to waiting).

## Table playtest checklist (Sprint 9)

The battlefield is click + command, Shandalar-style (no drag-and-drop). With two
instances connected (or one instance creating a room + joining on loopback):

1. **Render**: the table opens on the velvet playmat with gold filigree; the
   opponent's zone grid + hand strip sit on top, yours on the bottom, the Stack
   in the middle. Resize the window — everything re-flows and stays readable.
2. **Select**: left-click one of *your* hand/zone cards — a gold frame marks it
   and its name appears in the toolbar. Left-clicking an opponent card or empty
   space clears the selection.
3. **Command menu**: right-click a selected card — the menu opens with
   state-dependent labels (Tap/Untap, Flip to back/front). Click an item to
   apply it instantly (no animation).
4. **Keyboard verbs**: with a card selected, press **T** (tap/untap, the card
   rotates 90°), **C** (+1/+1 counter badge), **K** (creates a token in the
   card's zone), **F** (flips to the ornate back/front), **S** (moves it to the
   Stack, which fans out), **M** then **1–6** (move to a zone / the Stack),
   **Esc** closes the menu or clears the selection.
5. **Read-only opponent half**: opponent cards never select or open a menu.
6. **Life totals**: click either corner ring, type a number, **Enter** commits
   (the peer sees the same total), **Esc** cancels.
7. **Hand reveal**: the toolbar's **Request Hand Reveal** asks the opponent;
   they see an Accept / Deny / Not now prompt and the result appears in the
   right-hand panel.

## Integration gate (Sprint 12)

Run the end-to-end harness that drives the whole app by simulating mouse
presses (no display needed):

```bash
bash /tmp/todocpp/integration.sh
```

It configures/builds the project and runs `mtg_cpp_integration`, which clicks
through Home (profile picker → vault), the Deck Editor (search → add → save,
and import-preview → confirm), the Lobby (create room → choose deck), then
drives a real loopback guest to a ready table and asserts select → Tap → To
Stack → life edits all land on the opponent's byte-identical mirror. Every step
prints PASS/FAIL and the script exits non-zero on any failure.

## Design rules (from the original project)

1. No animations, no transitions — visual state snaps instantly.
2. Dumb relay — no game rules on the network peer; `board_update` is relayed verbatim.
3. Rigid grid — cards snap into explicit cells; the Stack is the only overlap.
4. Preserve instance identity — moves re-parent, never clone, card instances.
5. Sync through the same action — symmetric boards, mirrored actions, no echo.
6. Memory safety — no raw owning pointers, no manual `new`/`delete`, bounds-checked
   access, RAII everywhere, sanitizer-clean CI.
