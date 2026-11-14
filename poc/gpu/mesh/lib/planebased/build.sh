#!/bin/bash
# MP17 steps 1-2 build and run script. Usage: build.sh [build|test|mutants|bench|all]   (default: all)
# Binaries and logs go to storage/mesh/planebased (external volume, git-ignored), never the internal disk.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../../../.." && pwd)"
OUT="$REPO/storage/mesh/planebased"
BOOST="$REPO/src/third_party/boost"
INC="-I $BOOST"
for d in "$BOOST"/libs/*/include; do INC="$INC -I $d"; done
CXX="c++ -std=c++17 -O2 -ffp-contract=off -pthread"
mkdir -p "$OUT"
cmd="${1:-all}"

do_build() {
  bash "$HERE/../../oracle/fetch.sh"      # fetches/verifies Shewchuk predicates.c into poc/gpu/build/third_party (never committed)
  PRED="$REPO/poc/gpu/build/third_party/shewchuk/predicates.c"
  cc -O2 -ffp-contract=off -w -c "$PRED" -o "$OUT/predicates.o"
  $CXX $INC "$HERE/planebased_check.cpp" -o "$OUT/planebased_check" &
  $CXX $INC -DFIXEDINT_CHECK "$HERE/planebased_check.cpp" -o "$OUT/planebased_check_dbg" &
  $CXX $INC "$HERE/planebased_bench.cpp" "$OUT/predicates.o" -o "$OUT/planebased_bench" &
  for m in 1 2 3 4 5 6 7 8 9 10; do
    $CXX $INC -DMUTANT=$m "$HERE/planebased_check.cpp" -o "$OUT/planebased_check_mut$m" &
  done
  wait
}
do_test() {
  "$OUT/planebased_check" 10000000 | tee "$OUT/check_1e7.log"
  "$OUT/planebased_check_dbg" 100000 | tee "$OUT/check_dbg.log"
}
do_mutants() {
  caught=0
  for m in 1 2 3 4 5 6 7 8 9 10; do
    if "$OUT/planebased_check_mut$m" mut > "$OUT/mut$m.log" 2>&1; then echo "mutant $m: NOT CAUGHT"; else echo "mutant $m: caught ($(grep -c 'diffs [1-9]' "$OUT/mut$m.log") classes with diffs; $(grep primitives "$OUT/mut$m.log" | sed 's/.*pairs per width x 3 widths: //'))"; caught=$((caught+1)); fi
  done
  echo "mutants caught: $caught of 10"
}
do_bench() { "$OUT/planebased_bench" | tee "$OUT/bench.log"; }
case "$cmd" in
  build) do_build ;;
  test) do_test ;;
  mutants) do_mutants ;;
  bench) do_bench ;;
  all) do_build; do_mutants; do_bench; do_test ;;
esac
