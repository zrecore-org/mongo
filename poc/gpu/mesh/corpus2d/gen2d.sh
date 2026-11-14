#!/bin/bash
# Builds gen2d, generates the corpus twice (must be byte-identical), and (re)writes EXPECTED_SHA256.txt with --update.
#   cases/    small cases (committed with the poc), speed 1k set
#   $SPEED    spd100k.p2d (4 MB, kept on the external volume), listed in EXPECTED_SHA256.txt as speed/spd100k.p2d
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ST=/Volumes/CrucialX9/Projects/mongo/storage/mesh/gap2d
mkdir -p "$ST/bin" "$ST/speed" "$ST/speed2"
c++ -std=c++17 -O2 -ffp-contract=off -Wall -o "$ST/bin/gen2d" "$HERE/gen2d.cpp"
rm -rf "$HERE/cases" "$ST/speed" "$ST/gen_again" "$ST/speed2"
"$ST/bin/gen2d" "$HERE/cases" "$ST/speed"
"$ST/bin/gen2d" "$ST/gen_again" "$ST/speed2"
SHA="shasum -a 256"; command -v sha256sum >/dev/null && SHA=sha256sum
h() { (cd "$HERE/cases" && ls | LC_ALL=C sort | xargs $SHA); (cd "$ST/speed" && ls | LC_ALL=C sort | sed 's/^/speed\//;' >/dev/null; $SHA spd100k.p2d | sed 's/  /  speed\//'); }
h2() { (cd "$ST/gen_again" && ls | LC_ALL=C sort | xargs $SHA); (cd "$ST/speed2" && $SHA spd100k.p2d | sed 's/  /  speed\//'); }
if [ "$(h)" != "$(h2)" ]; then echo "NOT reproducible"; exit 1; fi
echo "reproducible: $(h | wc -l | tr -d ' ') files"
[ "$1" = "--update" ] && { h > "$HERE/EXPECTED_SHA256.txt"; echo "wrote EXPECTED_SHA256.txt"; }
[ "$1" = "--check" ] && { diff <(h) "$HERE/EXPECTED_SHA256.txt" && echo "matches EXPECTED_SHA256.txt"; }
rm -rf "$ST/gen_again" "$ST/speed2"
