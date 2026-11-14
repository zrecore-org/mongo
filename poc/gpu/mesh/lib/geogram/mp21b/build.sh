#!/bin/sh
# builds geogram_validated and the three planted mutants into storage/mesh/mp21b/bin
set -eu
R=/Volumes/CrucialX9/Projects/mongo; B=$R/storage/mesh/mp21b/bin; mkdir -p $B; H=$(cd "$(dirname "$0")" && pwd)
INC="-I$R/src/third_party/boost $(for d in $R/src/third_party/boost/libs/*/include; do printf -- '-I%s ' $d; done)"
for v in "" NODUP NOORIENT ONEBOUNDARY; do
  n=geogram_validated; [ -n "$v" ] && n=geogram_validated_mut_$v; D=""; [ -n "$v" ] && D=-DMUT_$v
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off $D $INC $H/geogram_validated.cpp -o $B/$n
done
# in-process variants: Geogram runner path unchanged (runner.cpp included), validator in front. G = pinned v1.10.1 build, P = MP20 patched build.
for pair in "pinned:$R/storage/mesh/geogram" "patched:$R/storage/mesh/mp20/geogram_patched"; do
  n=${pair%%:*}; G=${pair#*:}; export TMPDIR=${TMPDIR:-$R/storage/tmp}
  for v in "" NODUP NOORIENT ONEBOUNDARY; do
    o=$B/geogram_validated_inproc_$n; D=""; [ -n "$v" ] && { o=${o}_mut_$v; D=-DMUT_$v; }
    ${CXX:-c++} -std=c++17 -O2 -frounding-math -ffp-contract=off -DNDEBUG $D $INC -I"$G/src/src/lib" -I"$G/build/src/lib" \
      $H/geogram_validated_inproc.cpp "$G/build/lib/libgeogram.a" -lpthread -ldl -o $o
  done
done
