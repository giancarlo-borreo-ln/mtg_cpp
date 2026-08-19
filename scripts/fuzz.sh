#!/usr/bin/env bash
# M11.1 — structure-aware fuzz gate for mtg_cpp.
#
# Builds the two fuzz targets (deck parser + envelope/framing) in the ASan/UBSan
# build and runs each for a fixed time budget. The invocation is uniform: with
# clang the targets link libFuzzer (which parses `-max_total_time` natively);
# with GCC the shared driver main runs the same fixed-budget mutation loop.
# Any crash or UB aborts the process and fails the gate.
#
# Usage:  bash scripts/fuzz.sh [seconds] [seed]
# Env:    MTG_CPP_REPO_DIR   overrides the repo location (default below)
#         MTG_CPP_SFML_DIR   overrides the prebuilt SFML dir (default auto)

set -euo pipefail

REPO_DIR="${MTG_CPP_REPO_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SFML_DIR="${MTG_CPP_SFML_DIR:-/tmp/opencode/sfml}"
BUDGET_SECONDS="${1:-30}"
SEED="${2:-7}"

if [[ ! -d "$REPO_DIR" ]]; then
  echo "FAIL: repo dir not found at $REPO_DIR (set MTG_CPP_REPO_DIR)" >&2
  exit 2
fi
cd "$REPO_DIR"

# The project toolchain lives in a fixed location for this machine; fall back
# to whatever is on PATH otherwise.
if [[ -d /tmp/opencode/buildenv/bin ]]; then
  export PATH="/tmp/opencode/buildenv/bin:$PATH"
fi

echo "== mtg_cpp fuzz =="
echo "repo:    $REPO_DIR"
echo "budget:  ${BUDGET_SECONDS}s per target, seed $SEED"

# Configure the sanitizer build in place (idempotent; the cached FetchContent
# deps are reused). Fuzz targets build only under MTG_CPP_ENABLE_SANITIZERS.
EXTRA_OPTS=(-DMTG_CPP_ENABLE_SANITIZERS=ON)
if [[ -d "$SFML_DIR" ]]; then
  EXTRA_OPTS+=(-DMTG_CPP_SFML_DIR="$SFML_DIR")
fi
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "${EXTRA_OPTS[@]}" >/dev/null

cmake --build build --target mtg_cpp_fuzz_parser mtg_cpp_fuzz_envelope

run_target() {
  local name="$1"
  local corpus="$2"
  echo "running $name for ${BUDGET_SECONDS}s..."
  # `-max_total_time` is understood by libFuzzer and by the driver main alike.
  "./build/${name}" -max_total_time="$BUDGET_SECONDS" -seed="$SEED" "$corpus"
  echo "OK: $name finished with no crashes or UB"
}

run_target mtg_cpp_fuzz_parser src/fuzz/seed_corpus/deck_parser
run_target mtg_cpp_fuzz_envelope src/fuzz/seed_corpus/envelope

echo "== FUZZ OK: no crashes or UB over ${BUDGET_SECONDS}s per target =="
