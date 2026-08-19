# how_to_run.md — mtg_cpp

`mtg_cpp` is a single cross-platform C++20 desktop app: a two-player Magic: The
Gathering virtual tabletop styled after MicroProse's 1990s *Shandalar*. The
second player connects directly to the host's app over TCP — there is no
separate server process. Everything (card data, decks, art) is local.

---

## 1. Prerequisites

- A C++20 compiler (GCC ≥ 11, Clang ≥ 14, or MSVC 2022).
- CMake ≥ 3.24 and Ninja (recommended).
- Network access on the **first** configure (FetchContent downloads the deps).
- Linux: the X11/GL/freetype **dev** headers (see README if SFML builds from
  source); Windows: the prebuilt SFML DLLs or build SFML from source.
- For the card database: `curl`, `gzip`, `python3` (Linux) or the PowerShell
  equivalent (Windows) — only needed the first time you fetch card data.

## 2. Build

```bash
git clone <repo> mtg_cpp
cd mtg_cpp

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure   # unit suite (629 tests)
```

If SFML is not on the system and you have a prebuilt copy:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMTG_CPP_SFML_DIR=/opt/sfml
export LD_LIBRARY_PATH=/opt/sfml/lib:$LD_LIBRARY_PATH
```

Sanitizer (ASan/UBSan) and ThreadSanitizer builds:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMTG_CPP_ENABLE_SANITIZERS=ON
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMTG_CPP_ENABLE_TSAN=ON
```

## 3. Fetch the card database (first run)

The app resolves decks against a local copy of the full Scryfall bulk export
(`data/default-cards.jsonl`, ~500–600 MB decompressed). Fetch it once:

```bash
scripts/fetch_card_db.sh              # download + verify + decompress
scripts/fetch_card_db.sh --refresh    # force a re-fetch even if current
# Windows: .\scripts\fetch_card_db.ps1
```

Re-running is a no-op while the file is current. `data/` is git-ignored. Without
it the app still launches; only the Deck Editor's card search is unavailable.

## 4. Run

```bash
./build/mtg_cpp
```

- **1 Home** — pick a player profile; your deck vault appears.
- **2 Deck Editor** — search the local card database, build a deck, import an
  Arena export (paste or file), export to Arena text, save.
- **3 Lobby** — create a room (host) or join a friend's `IP:PORT`.
- **4 Table** — the battlefield (you arrive automatically once both decks are
  known).
- **Esc** quits.

### On the Table

Click a card to select it, then act via right-click menu or keys: **T** tap,
**C** +1/+1 counter, **K** token, **F** flip, **S** to Stack, **M** move to
zone. Click a gold life ring to edit a life total. Your half is interactive; the
opponent's half is read-only. Card art downloads on demand into the art cache
(procedural fallback while offline / still downloading).

## 5. Multiplayer

1. In the first instance: **3 Lobby → Create Room**. The host embeds the relay
   and shows the address to share (e.g. `192.168.1.10:7500`).
2. In the second instance: **3 Lobby**, type that `IP:PORT` into the join field,
   **Join Room**.
3. Both sides pick a deck from the vault. As soon as both decks are known the
   app switches to the Table.
4. **Leave** disconnects cleanly; the peer is told the room went back to waiting.

Two instances on the same machine work via the loopback address
(`127.0.0.1:<port>`).

### Host under NAT / across the internet

The host must be reachable at the shared `IP:PORT`:

- **LAN**: nothing to do — the host's machine is directly reachable.
- **Behind a router**: forward the host's port (default **7500**) to the host's
  LAN IP in the router's port-forwarding page. Share your **public** IP + port.
  Use a fixed port if the OS picks an ephemeral one; see `net/address.h`.
- **UPnP stub**: the transport seam (`ITransport`) is designed so a future
  central relay or NAT-punchthrough/UPnP implementation is a drop-in — none is
  built in yet, so port forwarding is currently required for cross-network play.

## 6. Where files live

| Thing | Location |
| --- | --- |
| Card database + provenance | `<install>/data/` (`default-cards.jsonl`, `manifest.json`) |
| Runtime art cache (M10.1) | `$XDG_DATA_HOME/mtg_cpp/art_cache/` (Linux) / `%APPDATA%\mtg_cpp\art_cache` (Windows) |
| Saved decks + player profile | `$XDG_DATA_HOME/mtg_cpp/decks/`, `profile.json` (Linux) / `%APPDATA%\mtg_cpp\…` (Windows) |
| Bundled fonts + icon | `<install>/assets/` |

Card *data* is CC0; card *art* is **not** — the art cache is for personal play
and must never be redistributed.

## 7. Packaging

```bash
bash scripts/package-linux.sh                      # dist/mtg_cpp-<v>-linux-x86_64.tar.gz
MTG_CPP_BUNDLE_DATA=1 bash scripts/package-linux.sh   # also bundle the card DB
powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1
```

The bundle is relocatable (runs from any directory): `bin/` (executable +
`mtg_cpp.sh` launcher), `lib/` (runtime shared libraries), `assets/`, and
optionally `data/`. On a clean machine, run `bin/mtg_cpp.sh` (Linux) or the exe
(Windows); if `data/` wasn't bundled, run the fetch script in the install
directory first.

## 8. Testing, fuzzing, sanitizers

```bash
ctest --test-dir build --output-on-failure       # unit suite
bash /tmp/todocpp/integration.sh                 # end-to-end headless driver (Sprint 12)
bash scripts/fuzz.sh 30 7                        # structure-aware fuzz, 30 s/target (M11.1)
bash scripts/ci-linux.sh clang                   # build + ASan/UBSan test + format + tidy
```

Fuzz targets: `mtg_cpp_fuzz_parser` (Arena deck parser) and
`mtg_cpp_fuzz_envelope` (network framing). With clang they link libFuzzer; with
GCC the same binary runs a fixed-budget mutation driver.

## 9. Troubleshooting

- **No card search in the Deck Editor** — `data/default-cards.jsonl` is missing;
  run `scripts/fetch_card_db.sh`.
- **Window opens but no text** — the bundled font is missing; the app degrades
  gracefully. `assets/fonts/` must sit next to the executable.
- **Guest can't connect** — check the host's firewall allows the port, and that
  the shared address is reachable (see §5 NAT).
- **Art stays procedural** — offline or a fresh download in flight; art resumes
  automatically when the network is back.
