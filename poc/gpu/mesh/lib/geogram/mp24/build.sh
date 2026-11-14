#!/bin/sh
# Build geogram_fidelity (and the three planted mutants) against a built MP22-state libgeogram.a (GEOGRAM_DIR = the tree: <dir>/src, <dir>/build).
# usage: GEOGRAM_DIR=<tree> [OUTDIR=<dir>] [MUTANTS=1] sh build.sh      (default tree: the MP22b chain tree on the M1)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../../../.." && pwd)"
G="${GEOGRAM_DIR:-$ROOT/storage/mesh/mp22b/work/geo_chain}"
OUT="${OUTDIR:-$G/bin}"; mkdir -p "$OUT"
export TMPDIR="${TMPDIR:-$ROOT/storage/tmp}"; mkdir -p "$TMPDIR"
B="$ROOT/src/third_party/boost"; INC="-I$B"; for d in "$B"/libs/*/include; do INC="$INC -I$d"; done
OMP=""; [ "$(uname -s)" = Linux ] && OMP="-fopenmp"
build() {  # name defines
  ${CXX:-c++} -std=c++17 -O2 -frounding-math -ffp-contract=off -DNDEBUG $2 $INC -I"$G/src/src/lib" -I"$G/build/src/lib" \
    "$HERE/geogram_fidelity.cpp" "$G/build/lib/libgeogram.a" $OMP -lpthread -ldl -o "$OUT/$1"
  echo "built $OUT/$1"
}
build geogram_fidelity ""
if [ "${MUTANTS:-0}" = 1 ]; then build geogram_fidelity_mut_WRONGC -DMUT_WRONGC; build geogram_fidelity_mut_NOBACK -DMUT_NOBACK; build geogram_fidelity_mut_ROUND -DMUT_ROUND; fi
