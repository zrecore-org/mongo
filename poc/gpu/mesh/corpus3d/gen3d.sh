#!/bin/bash
# Builds gen3d, generates the corpus twice (must be byte-identical), and (re)writes EXPECTED_SHA256.txt with --update; --check compares.
#   cases/ small cases (kept with the poc); the two 1e5-triangle speed meshes live on the external volume and are listed as speed/...
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ST=/Volumes/CrucialX9/Projects/mongo/storage/mesh/gap3d
mkdir -p "$ST/bin"; c++ -std=c++17 -O2 -ffp-contract=off -Wall -o "$ST/bin/gen3d" "$HERE/gen3d.cpp"
rm -rf "$HERE/cases" "$ST/speed" "$ST/gen_again" "$ST/speed2"
"$ST/bin/gen3d" "$HERE/cases" "$ST/speed" 2>/dev/null; "$ST/bin/gen3d" "$ST/gen_again" "$ST/speed2" 2>/dev/null
SHA="shasum -a 256"; command -v sha256sum >/dev/null && SHA=sha256sum
h() { (cd "$HERE/cases" && ls | LC_ALL=C sort | xargs $SHA); (cd "$ST/speed" && for f in $(ls | LC_ALL=C sort); do $SHA $f | sed 's/  /  speed\//'; done); }
h2() { (cd "$ST/gen_again" && ls | LC_ALL=C sort | xargs $SHA); (cd "$ST/speed2" && for f in $(ls | LC_ALL=C sort); do $SHA $f | sed 's/  /  speed\//'; done); }
if [ "$(h)" != "$(h2)" ]; then echo "NOT reproducible"; exit 1; fi
echo "reproducible: $(h | wc -l | tr -d ' ') files"
[ "$1" = "--update" ] && { h > "$HERE/EXPECTED_SHA256.txt"; echo "wrote EXPECTED_SHA256.txt"; }
[ "$1" = "--check" ] && { diff <(h) "$HERE/EXPECTED_SHA256.txt" && echo "matches EXPECTED_SHA256.txt"; }
rm -rf "$ST/gen_again" "$ST/speed2"; true
