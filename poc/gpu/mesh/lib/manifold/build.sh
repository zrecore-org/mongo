#!/bin/sh
# Builds manifold_runner and manifold_bench against a configured+built Manifold tree.
# usage: build.sh <build-dir: A|B>   (trees live in storage/mesh/manifold/build{A,B}; sources in .../src)
set -e
M=/Volumes/CrucialX9/Projects/mongo/storage/mesh/manifold
B=${1:-A}
HERE=$(cd "$(dirname "$0")" && pwd)
EXTRA=""; [ "$B" = A ] && EXTRA="-ffp-contract=off"
mkdir -p "$M/run$B"
for t in runner:manifold_runner bench:manifold_bench; do
  c++ -std=c++17 -O2 -DNDEBUG $EXTRA -DMANIFOLD_VERSION_STR='"3.5.5"' -I"$M/src/include" \
    "$HERE/${t%%:*}.cpp" "$M/build$B/src/libmanifold.a" "$M/build$B/_deps/clipper2-build/libClipper2.a" \
    -o "$M/run$B/${t##*:}"
done
