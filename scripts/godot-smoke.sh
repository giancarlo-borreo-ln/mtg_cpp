#!/usr/bin/env bash
# Godot headless smoke test for the mtg_cpp GDExtension (Phase 2, gate 2.9).
#
# Prerequisites: the engine (libmtg_cpp_engine.so) and the extension
# (godot/bin/libmtg_cpp_godot.so) must already be built. See godot/README.md.
#
# Exit 0 on PASS, non-zero on FAIL (mirrors the game's exit code).

set -euo pipefail

GODOT_BIN="${GODOT_BIN:-/tmp/mtg-cpp/godot-bin/Godot_v4.5-stable_linux.x86_64}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../godot" && pwd)"

if [ ! -x "$GODOT_BIN" ]; then
  echo "error: Godot binary not found at $GODOT_BIN (set GODOT_BIN)" >&2
  exit 2
fi

# The game runtime reads the registered-extension list from the generated
# .godot cache; on a fresh checkout the editor scan that populates it is
# skipped (and the headless editor scan crashes in Godot 4.5 — a Godot bug
# unrelated to this project), so write the deterministic entry directly.
#
# Import resources first (fonts) headlessly. Godot 4.5's headless import
# crashes at exit (another Godot bug) but still completes the import, so its
# exit code is ignored.
"$GODOT_BIN" --headless --path "$PROJECT_DIR" --import >/dev/null 2>&1 || true

mkdir -p "$PROJECT_DIR/.godot"
printf 'res://mtg_cpp.gdextension\n' > "$PROJECT_DIR/.godot/extension_list.cfg"

# 1. Binding smoke (engine <-> GDScript).
"$GODOT_BIN" --headless --path "$PROJECT_DIR" res://tests/smoke_test.tscn
binding_status=$?
if [ "$binding_status" -ne 0 ]; then
  exit "$binding_status"
fi

# 2. UI smoke (app shell + Home navigation).
"$GODOT_BIN" --headless --path "$PROJECT_DIR" res://tests/ui_smoke_test.tscn
ui_status=$?
if [ "$ui_status" -ne 0 ]; then
  exit "$ui_status"
fi

# 3. Table smoke (card faces, tap tween, drag & drop, resize relayout).
"$GODOT_BIN" --headless --path "$PROJECT_DIR" res://tests/table_smoke_test.tscn
table_status=$?
if [ "$table_status" -ne 0 ]; then
  exit "$table_status"
fi

# 4. Networking smoke (loopback host + guest, board sync).
exec "$GODOT_BIN" --headless --path "$PROJECT_DIR" res://tests/net_smoke_test.tscn
