#!/bin/bash
# Builds run3d, selftest3d (+ mutants) and speed3d into storage/mesh/gap3d/bin. Boost headers: the MP08a pin (1.92.0); predicates.c: the file oracle/fetch.sh pins (Shewchuk, public domain).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; G2=/Volumes/CrucialX9/Projects/mongo/storage/mesh/gap2d; BIN=/Volumes/CrucialX9/Projects/mongo/storage/mesh/gap3d/bin
BOOST=$G2/src/boost/boost_1_92_0; M=/Volumes/CrucialX9/Projects/mongo/storage/mesh/manifold
PRED=/Volumes/CrucialX9/Projects/mongo/storage/mesh/mp22/root/poc/gpu/build/third_party/shewchuk/predicates.c
mkdir -p "$BIN"; CXX="c++ -std=c++17 -O2 -ffp-contract=off -DNDEBUG"
cc -O2 -ffp-contract=off -c -o "$BIN/predicates.o" "$PRED" -w
INC="-I$BOOST -I$G2/src/earcut/include -I$G2/src/cdt/CDT/include -I$M/src/include"
LIBS="$M/buildA/src/libmanifold.a $M/buildA/_deps/clipper2-build/libClipper2.a"
what=${1:-all}
if [ "$what" = all ] || [ "$what" = run ]; then $CXX -DMANIFOLD_PAR=-1 $INC -o "$BIN/run3d" "$HERE/run3d.cpp" "$BIN/predicates.o" $LIBS; fi
if [ "$what" = all ] || [ "$what" = self ]; then
  for m in 0 1 2 3 4 5 6 7; do $CXX -DMUTANT=$m -I"$BOOST" -o "$BIN/selftest3d_m$m" "$HERE/selftest3d.cpp" "$BIN/predicates.o"; done; fi
if [ "$what" = all ] || [ "$what" = speed ]; then $CXX -DMANIFOLD_PAR=-1 $INC -o "$BIN/speed3d" "$HERE/speed3d.cpp" "$BIN/predicates.o" $LIBS; fi
