#!/bin/bash
# Builds gen_touch into storage/mesh/mp18 (external volume) with the vendored Boost headers.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../../../../.."
B="$ROOT/src/third_party/boost"
INC=(); for d in "$B"/libs/*/include; do INC+=("-I$d"); done
export TMPDIR="$ROOT/storage/tmp"; mkdir -p "$ROOT/storage/mesh/mp18"
c++ -std=c++17 -O2 -ffp-contract=off -I"$B" "${INC[@]}" "$HERE/gen_touch.cpp" -o "$ROOT/storage/mesh/mp18/gen_touch"
