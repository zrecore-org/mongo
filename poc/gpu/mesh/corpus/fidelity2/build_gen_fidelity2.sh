#!/bin/bash
# builds gen_fidelity2 into storage/mesh/mp24b/bin (-O2 -ffp-contract=off, as the other corpus generators)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../../../../.."
B="$ROOT/src/third_party/boost"; INC=(-I"$B"); for d in "$B"/libs/*/include; do INC+=("-I$d"); done
export TMPDIR="$ROOT/storage/tmp"; mkdir -p "$ROOT/storage/mesh/mp24b/bin"
c++ -std=c++17 -O2 -ffp-contract=off "${INC[@]}" "$HERE/gen_fidelity2.cpp" -o "$ROOT/storage/mesh/mp24b/bin/gen_fidelity2"
