#!/usr/bin/env bash
# Build, test, format-check and statically analyse the project.
#
# Usage:
#   scripts/ci-linux.sh [compiler=clang|gcc] [build_dir=build]
#
# Requires: cmake, ninja, a C++20 compiler, clang-format, clang-tidy.
# Set CLANG_TOOLS_BIN to the directory holding clang-format/clang-tidy if they
# are not on PATH (e.g. a pip-installed toolchain).

set -euo pipefail

compiler="${1:-clang}"
build_dir="${2:-build}"

if [ -n "${CLANG_TOOLS_BIN:-}" ]; then
  export PATH="${CLANG_TOOLS_BIN}:${PATH}"
fi

case "$compiler" in
  clang) export CC=clang CXX=clang++ ;;
  gcc)   export CC=gcc   CXX=g++ ;;
  *)     echo "error: unknown compiler '$compiler' (use clang or gcc)" >&2; exit 2 ;;
esac

echo "== configure (${compiler}, ASan/UBSan) =="
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DMTG_CPP_ENABLE_SANITIZERS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "== build =="
cmake --build "$build_dir"

echo "== test =="
ctest --test-dir "$build_dir" --output-on-failure

echo "== clang-format =="
mapfile -t sources < <(find src tests -name '*.cpp' -print | sort)
clang-format --dry-run --Werror "${sources[@]}"

if [ "$compiler" = "clang" ]; then
  echo "== clang-tidy =="
  for source in "${sources[@]}"; do
    clang-tidy -p "$build_dir" --quiet "$source"
  done
fi

echo "== ci-linux: all checks passed =="
