#!/bin/bash
# Builds gen_hard, generates the corpus twice (byte-identical or abort), --update writes EXPECTED_SHA256.txt, --check compares.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ST=/Volumes/CrucialX9/Projects/mongo/storage/mesh/hard2d3d
mkdir -p "$ST/bin"; c++ -std=c++17 -O2 -ffp-contract=off -Wall -o "$ST/bin/gen_hard" "$HERE/gen_hard.cpp"
rm -rf "$HERE/cases" "$ST/gen_again"; "$ST/bin/gen_hard" "$HERE/cases"; "$ST/bin/gen_hard" "$ST/gen_again"
SHA="shasum -a 256"; command -v sha256sum >/dev/null && SHA=sha256sum
h() { (cd "$1" && ls | LC_ALL=C sort | xargs $SHA); }
[ "$(h "$HERE/cases")" == "$(h "$ST/gen_again")" ] || { echo NOT reproducible; exit 1; }
echo reproducible
[ "$1" = "--update" ] && { h "$HERE/cases" > "$HERE/EXPECTED_SHA256.txt"; echo wrote EXPECTED_SHA256.txt; }
[ "$1" = "--check" ] && { diff <(h "$HERE/cases") "$HERE/EXPECTED_SHA256.txt" && echo matches; }
rm -rf "$ST/gen_again"
