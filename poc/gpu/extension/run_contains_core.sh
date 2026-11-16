#!/bin/bash
# MP14d step 1: builds and runs test_contains_core (CPU path of $meshContains versus the exactmesh Shewchuk-based oracle) and its two mutants.
# Reads exactmesh headers and the fetched Shewchuk predicates.c (public domain) from /Volumes/CrucialX9/Projects/exactmesh (read-only). Output: storage/extension_build.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; R="$(cd "$HERE/../../.." && pwd)"; OUT="$R/storage/extension_build"; EM=${EXACTMESH:-/Volumes/CrucialX9/Projects/exactmesh}
B=$("$HERE/boost_inc.sh"); mkdir -p "$OUT/inc/mongo/db/extension"; ln -sfn "$R/src/mongo/db/extension/public" "$OUT/inc/mongo/db/extension/public"
clang -O2 -w -c "$EM/build/deps/shewchuk/predicates.c" -o "$OUT/shewchuk.o"
CXX="clang++ -std=c++20 -O2 -w -I$EM -I$HERE -I$HERE/stub_inc -I$OUT/inc $B"
$CXX "$HERE/test_contains_core.cpp" "$OUT/shewchuk.o" -o "$OUT/test_contains_core"; "$OUT/test_contains_core"
for m in MESHCONTAINS_MUT_NO_TIEBREAK MUT_BOUNDARY_IN; do $CXX -D$m "$HERE/test_contains_core.cpp" "$OUT/shewchuk.o" -o "$OUT/test_contains_core_$m"; echo "== mutant $m (must disagree)"; if "$OUT/test_contains_core_$m" > "$OUT/mut_$m.log"; then echo "FAIL mutant not caught"; exit 1; else tail -1 "$OUT/mut_$m.log"; fi; done
echo "ALL OK"
