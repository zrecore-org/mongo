#!/bin/bash
# Builds the oracle and the prototype (and its planted mutants proto_m1..m12) into storage/mesh/hard2d3d/bin. Boost 1.92.0 headers (multiprecision only), earcut, CDT, Clipper2 from the MP08a storage tree.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ST=/Volumes/CrucialX9/Projects/mongo/storage/mesh/hard2d3d; G=/Volumes/CrucialX9/Projects/mongo/storage/mesh/gap2d
BIN=$ST/bin; mkdir -p "$BIN"; CXX="c++ -std=c++17 -O2 -ffp-contract=off -DNDEBUG"
$CXX -I"$G/src/boost/boost_1_92_0" -o "$BIN/oracle" "$HERE/oracle.cpp"
C=$G/src/clipper2/CPP/Clipper2Lib
OBJ=$BIN/clipper_objs; mkdir -p $OBJ
for s in clipper.engine clipper.offset clipper.rectclip clipper.triangulation; do [ -f $OBJ/$s.o ] || $CXX -c -I"$C/include" "$C/src/$s.cpp" -o $OBJ/$s.o; done
for m in 0 $(seq 1 12); do $CXX -DMUT=$m -I"$C/include" -I"$G/src/earcut/include" -I"$G/src/cdt/CDT/include" -o "$BIN/proto_m$m" "$HERE/proto.cpp" $OBJ/*.o & [ $(( m % 4 )) = 3 ] && wait; done; wait
echo built
