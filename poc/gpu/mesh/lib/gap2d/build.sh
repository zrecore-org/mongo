#!/bin/bash
# Builds the checker (and its planted-mutant variants) and every runner into storage/mesh/gap2d/bin. Boost headers from the pinned 1.92.0 tarball;
# for the checker the in-tree vendored Boost (1.88.0, multiprecision only) would also do, but the pinned one is used everywhere.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ST=/Volumes/CrucialX9/Projects/mongo/storage/mesh/gap2d; BIN=$ST/bin
BOOST=$ST/src/boost/boost_1_92_0; M=/Volumes/CrucialX9/Projects/mongo/storage/mesh/manifold
mkdir -p "$BIN"
CXX="c++ -std=c++17 -O2 -ffp-contract=off -DNDEBUG"
what=${1:-all}
if [ "$what" = all ] || [ "$what" = check ]; then
  $CXX -I"$BOOST" -o "$BIN/check2d" "$HERE/check2d.cpp"
  for m in 1 2 3 4 5 6; do $CXX -I"$BOOST" -DMUTANT=$m -o "$BIN/check2d_m$m" "$HERE/check2d.cpp"; done
fi
if [ "$what" = all ] || [ "$what" = clipper2 ]; then
  C=$ST/src/clipper2/CPP/Clipper2Lib
  $CXX -I"$C/include" -o "$BIN/run_clipper2" "$HERE/run_clipper2.cpp" "$C/src/clipper.engine.cpp" "$C/src/clipper.offset.cpp" "$C/src/clipper.rectclip.cpp" "$C/src/clipper.triangulation.cpp"
fi
if [ "$what" = all ] || [ "$what" = boost ]; then $CXX -I"$BOOST" -o "$BIN/run_boost" "$HERE/run_boost.cpp"; fi
if [ "$what" = all ] || [ "$what" = earcut ]; then $CXX -I"$ST/src/earcut/include" -o "$BIN/run_earcut" "$HERE/run_earcut.cpp"; fi
if [ "$what" = all ] || [ "$what" = cdt ]; then $CXX -I"$ST/src/cdt/CDT/include" -o "$BIN/run_cdt" "$HERE/run_cdt.cpp"; fi
if [ "$what" = all ] || [ "$what" = manifold ]; then
  $CXX -DMANIFOLD_PAR=-1 -I"$M/src/include" -o "$BIN/run_manifold" "$HERE/run_manifold.cpp" "$M/buildA/src/libmanifold.a" "$M/buildA/_deps/clipper2-build/libClipper2.a"
fi
