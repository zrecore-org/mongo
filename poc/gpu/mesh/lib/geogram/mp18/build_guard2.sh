#!/bin/bash
# Builds geogram_guard against a Geogram build tree. usage: build_guard.sh [geogram_dir] [out]   (default: pinned v1.10.1 -> storage/mesh/mp18/geogram_guard)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../../../../../.."
G="${1:-$ROOT/storage/mesh/geogram}"; OUT="${2:-$ROOT/storage/mesh/mp18/geogram_guard2}"
B="$ROOT/src/third_party/boost"; INC=(); for d in "$B"/libs/*/include; do INC+=("-I$d"); done
export TMPDIR="$ROOT/storage/tmp"
c++ -std=c++17 -O2 -frounding-math -ffp-contract=off -DNDEBUG -I"$G/src/src/lib" -I"$G/build/src/lib" -I"$B" "${INC[@]}" \
  "$HERE/geogram_guard2.cpp" "$ROOT/poc/gpu/build/mesh/libshewchuk_predicates.a" "$G/build/lib/libgeogram.a" -lpthread -ldl -o "$OUT"
echo "built $OUT"
