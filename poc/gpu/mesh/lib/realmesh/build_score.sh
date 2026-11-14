#!/bin/sh
# builds real_score into storage/mesh/mp26/bin (needs poc/gpu/build/mesh/libshewchuk_predicates.a from the mesh CMake build)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../../../../.." && pwd)"
B="$ROOT/src/third_party/boost"; INC="-I$B"; for d in "$B"/libs/*/include; do INC="$INC -I$d"; done
OUT="$ROOT/storage/mesh/mp26/bin"; mkdir -p "$OUT"; export TMPDIR="$ROOT/storage/tmp"
${CXX:-c++} -std=c++17 -O2 -ffp-contract=off $INC "$HERE/real_score.cpp" "$ROOT/poc/gpu/build/mesh/libshewchuk_predicates.a" -o "$OUT/real_score"
echo "built $OUT/real_score"
