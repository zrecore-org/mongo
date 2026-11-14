#!/bin/bash
# MP21a: cross-platform check of the MP20 Geogram patch (informational, never gating). See poc/gpu/mesh/PLAN.md section MP21.
# usage: mp21_geogram_xplat.sh <stage>     stages: fetch | build-unpatched | build-patched | tools | corpus | score | report | all
# Runs the same way on the M1 (WORK defaults to storage/mesh/mp21/work on the external volume) and on ubuntu-24.04 x86_64/aarch64
# (CI sets WORK). Needs: curl, tar, patch, cmake, ninja or make, g++/c++, python3, and poc/gpu/build/third_party/shewchuk/predicates.c
# (oracle/fetch.sh) plus the vendored Boost headers (src/third_party/boost).
# Hard failures (exit 1): tarball sha256 differs from the pin, patch/patched-file hash differs from FROZEN_SHA256.txt, a restricted
# component is compiled, a generated corpus file differs from its EXPECTED_SHA256 file. Verdicts themselves never fail the script.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MESH="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$MESH/../../.." && pwd)"
WORK="${WORK:-$ROOT/storage/mesh/mp21/work}"
NPAIRS="${NPAIRS:-200}"                    # first N pairs of set 4 (x 3 ops = 600 rows)
JOBS="${JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu) )}"
export TMPDIR="$WORK/tmp"
mkdir -p "$WORK" "$TMPDIR" "$WORK/dl"
G_VER=v1.10.1
G_SHA256=24b661c13627d64120efd05217ddc6b12741e2d22698309bb2128416b439f2d1   # from lib/geogram/NEWEST_RETEST.md
G_URL=https://github.com/BrunoLevy/geogram/archive/refs/tags/$G_VER.tar.gz
# Submodules are not in GitHub's source tarballs. Commits are the ones in lib/geogram/REPORT.md section 1. The archive hashes below are
# what GitHub served on 2026-11-14 (not pinned upstream by anyone); a difference is a warning, the commit id is what pins the content.
SUBS="OpenNL|BrunoLevy/OpenNL|ae782db2db40cc40fcd7659629fc652e5eab1f9e|22bb3fbe404cf109a4b5ea97b2a40908438c33f313a628856d74ecc81dce4934
amgcl|ddemidov/amgcl|93827a00fc926d951c75f08fdb1d491912ff7065|9969cfbf56acfe7fd5e70e0630255b898cfbdbcd800dc90a4e3665e2c6f9bc75
libMeshb|LoicMarechal/libMeshb|88095d5ed04bfaf6bee27b777e7190bebdc72230|17946a0c61a0bdb9be5d012f26425b3fd89f7d46142498caa74d1dec10089b82
rply|diegonehab/rply|4296cc91b5c8c26d4e7d7aac0cee2b194ffc5800|c8b8350352c4935a5ec3cc582082b75e84e5415615c8adadc3acddbf43877409"
FROZEN="$MESH/lib/geogram/mp20/FROZEN_SHA256.txt"
PATCH="$MESH/lib/geogram/mp20/geogram_mp20.patch"
PRISTINE_CPP_SHA256=facff4cf02990f94432b8ba92725977cce8475bd9013bfa85cf778a3f3c9eb24   # pristine v1.10.1 mesh_surface_intersection.cpp (FROZEN_SHA256.txt)

say() { echo; echo "=== $* ($(date -u +%H:%M:%S))"; }
die() { echo "::error title=MP21a::$*" >&2; echo "FATAL: $*" >&2; exit 1; }
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
shacheck() { if command -v sha256sum >/dev/null; then sha256sum -c "$1"; else shasum -a 256 -c "$1"; fi; }   # run inside the dir
download() {  # url dest
  [ -s "$2" ] || curl -fsSL --retry 3 -o "$2" "$1"
}
platform() {
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) echo Linux64-gcc ;;
    Linux-aarch64|Linux-arm64) echo Linux64-gcc-aarch64 ;;
    Darwin-arm64) echo Darwin-aarch64-clang ;;
    *) die "unsupported platform $(uname -s)-$(uname -m)" ;;
  esac
}
arch_tag() { echo "$(uname -s)-$(uname -m)"; }

stage_fetch() {
  say "fetch Geogram $G_VER and verify the pinned tarball sha256"
  download "$G_URL" "$WORK/dl/geogram-$G_VER.tar.gz"
  got="$(sha "$WORK/dl/geogram-$G_VER.tar.gz")"
  echo "pinned  $G_SHA256"; echo "actual  $got"
  if [ "$got" != "$G_SHA256" ]; then
    die "Geogram $G_VER archive sha256 differs from the pin in lib/geogram/NEWEST_RETEST.md (GitHub regenerates archives; the pin was NOT updated). Stopping."
  fi
  echo "tarball sha256 matches the pin"
  echo "$SUBS" | while IFS='|' read -r n repo commit want; do
    download "https://github.com/$repo/archive/$commit.tar.gz" "$WORK/dl/$n.tar.gz"
    got="$(sha "$WORK/dl/$n.tar.gz")"
    if [ "$got" = "$want" ]; then echo "submodule $n @ ${commit:0:10}: archive sha256 matches the recorded value"
    else echo "::warning title=MP21a submodule archive::$n @ $commit archive sha256 is $got, recorded $want (the commit id still pins the content)"; fi
  done
}

extract_tree() {  # dir
  local D="$1"; rm -rf "${D:?}"; mkdir -p "$D/src"
  tar xzf "$WORK/dl/geogram-$G_VER.tar.gz" -C "$D/src" --strip-components=1
  echo "$SUBS" | while IFS='|' read -r n repo commit want; do
    mkdir -p "$D/src/src/lib/geogram/third_party/$n"
    tar xzf "$WORK/dl/$n.tar.gz" -C "$D/src/src/lib/geogram/third_party/$n" --strip-components=1
  done
}

build_geogram() {  # name(unpatched|patched)
  local N="$1" D="$WORK/geo_$1" CPP=src/lib/geogram/mesh/mesh_surface_intersection.cpp
  say "build Geogram $G_VER ($N)"
  [ -s "$WORK/dl/geogram-$G_VER.tar.gz" ] || stage_fetch
  extract_tree "$D"
  if [ "$N" = patched ]; then
    want_patch="$(awk '/geogram_mp20.patch/{print $1}' "$FROZEN")"
    want_cpp="$(awk '/patched mesh_surface_intersection.cpp/{print $1}' "$FROZEN")"
    [ "$(sha "$PATCH")" = "$want_patch" ] || die "geogram_mp20.patch sha256 differs from FROZEN_SHA256.txt"
    [ "$(sha "$D/src/$CPP")" = "$PRISTINE_CPP_SHA256" ] || die "pristine mesh_surface_intersection.cpp differs from the recorded v1.10.1 hash"
    patch -p1 -d "$D/src" < "$PATCH"
    got="$(sha "$D/src/$CPP")"; echo "patched file sha256 $got (frozen $want_cpp)"
    [ "$got" = "$want_cpp" ] || die "patched mesh_surface_intersection.cpp differs from the frozen MP20 hash"
  else
    [ "$(sha "$D/src/$CPP")" = "$PRISTINE_CPP_SHA256" ] || die "pristine mesh_surface_intersection.cpp differs from the recorded v1.10.1 hash"
  fi
  local GEN=(); if command -v ninja >/dev/null; then GEN=(-G Ninja); else GEN=(-G "Unix Makefiles"); fi
  # Same options as lib/geogram/REPORT.md section 1; static library; no GUI; flags repeated explicitly (the platform file also sets them).
  cmake -S "$D/src" -B "$D/build" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DVORPALINE_PLATFORM="$(platform)" \
    -DGEOGRAM_WITH_TETGEN=OFF -DGEOGRAM_WITH_TRIANGLE=OFF -DGEOGRAM_WITH_HLBFGS=OFF \
    -DGEOGRAM_WITH_GRAPHICS=OFF -DGEOGRAM_WITH_LEGACY_NUMERICS=OFF -DGEOGRAM_WITH_LUA=OFF \
    -DGEOGRAM_WITH_TBB=OFF -DGEOGRAM_WITH_FPG=OFF -DGEOGRAM_WITH_GARGANTUA=OFF -DGEOGRAM_LIB_ONLY=ON \
    -DCMAKE_CXX_FLAGS="-frounding-math -ffp-contract=off" -DCMAKE_C_FLAGS="-frounding-math -ffp-contract=off" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 > "$D/cmake.log" 2>&1 || { tail -30 "$D/cmake.log"; die "cmake configure failed ($N)"; }
  grep -E "VORPALINE_BUILD_DYNAMIC|GEOGRAM_WITH_(TETGEN|TRIANGLE|HLBFGS|GRAPHICS):" "$D/build/CMakeCache.txt" || true
  cmake --build "$D/build" -j "$JOBS" > "$D/build.log" 2>&1 || { tail -40 "$D/build.log"; die "geogram build failed ($N)"; }
  [ -s "$D/build/lib/libgeogram.a" ] || die "no static libgeogram.a produced ($N)"
  # restricted components must not be compiled
  python3 -I - "$D/build/compile_commands.json" <<'PY' || die "restricted component compiled ($N)"
import json, re, sys
c = json.load(open(sys.argv[1])); bad = [e["file"] for e in c if re.search(r"third_party/(tetgen|triangle|hlbfgs)/", e["file"], re.I)]
print(f"compiled translation units: {len(c)} (MP20 recorded 147 on macOS); restricted: {len(bad)}")
sys.exit(1 if bad else 0)
PY
  GEOGRAM_DIR="$D" sh "$MESH/lib/geogram/build.sh"
  echo "runner sha256 $(sha "$D/bin/geogram_runner")"
}

build_predicates() {
  local P="$ROOT/poc/gpu/build/third_party/shewchuk/predicates.c"
  [ -f "$P" ] || die "predicates.c missing: run poc/gpu/mesh/oracle/fetch.sh first"
  if [ ! -f "$WORK/libshewchuk_predicates.a" ]; then
    ${CC:-cc} -O3 -DNDEBUG -ffp-contract=off -w -c "$P" -o "$WORK/predicates.o"; ar rcs "$WORK/libshewchuk_predicates.a" "$WORK/predicates.o"
  fi
}

stage_tools() {
  say "build oracle tools (mesh_compare, predicates) and the corpus generators"
  local B="$ROOT/src/third_party/boost" OUT="$ROOT/poc/gpu/build/mesh"
  local INC=(); for d in "$B"/libs/*/include; do INC+=("-I$d"); done
  mkdir -p "$OUT" "$WORK/bin"
  build_predicates
  if [ ! -x "$OUT/mesh_compare" ]; then   # on the M1 this already exists from the CMake build; CI has no Vulkan SDK, so build it directly
    ${CXX:-c++} -std=c++17 -O3 -DNDEBUG -ffp-contract=off -I"$B" "${INC[@]}" "$MESH/oracle/compare.cpp" "$WORK/libshewchuk_predicates.a" -o "$OUT/mesh_compare"
  else echo "using existing $OUT/mesh_compare"; fi
  for g in gen_touch gen_touch_set4; do   # same flags as corpus/touch/build_gen_touch*.sh (-O2 -ffp-contract=off)
    ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off -I"$B" "${INC[@]}" "$MESH/corpus/touch/$g.cpp" "$WORK/libshewchuk_predicates.a" -o "$WORK/bin/$g"
  done
}

stage_corpus() {
  say "generate touch_* and set 4, verify every file against EXPECTED_SHA256*.txt"
  [ -x "$WORK/bin/gen_touch_set4" ] || stage_tools
  local rc=0 pair g d e
  for pair in "gen_touch:touch_corpus:EXPECTED_SHA256.txt" "gen_touch_set4:set4_corpus:EXPECTED_SHA256_set4.txt"; do
    IFS=: read -r g d e <<< "$pair"
    rm -rf "${WORK:?}/${d:?}"; "$WORK/bin/$g" "$WORK/$d" > "$WORK/$d.gen.log" 2>&1 || { tail -5 "$WORK/$d.gen.log"; die "$g failed"; }
    if ( cd "$WORK/$d" && shacheck "$MESH/corpus/touch/$e" > "$WORK/$d.verify.log" 2>&1 ); then
      echo "$d: all $(wc -l < "$MESH/corpus/touch/$e") files match $e"
    else
      echo "$d: MISMATCH against $e:"; grep -v ': OK$' "$WORK/$d.verify.log" | head -20; rc=1
    fi
  done
  [ $rc = 0 ] || die "generated corpus differs from the recorded EXPECTED_SHA256 files (different generator output on this platform); results would not be comparable. Stopping."
}

shard_score() {  # name runner corpus [ids-file]   -> $WORK/score/<name>.tsv (merged), shards in $WORK/score/<name>_<k>
  local NAME="$1" RUN="$2" C="$3" IDS="${4:-}" S="$WORK/score"; mkdir -p "$S"
  export GEOGRAM_RUNNER_THREADS=1   # as MP18/MP20 run_shards.sh
  if [ -n "$IDS" ]; then cp "$IDS" "$S/ids_$NAME.all"; else tail -n +2 "$C/manifest.tsv" | cut -f1 > "$S/ids_$NAME.all"; fi
  python3 -I - "$S" "$NAME" "$JOBS" <<'PY'
import sys
s, n, k = sys.argv[1], sys.argv[2], int(sys.argv[3]); ids = open(f"{s}/ids_{n}.all").read().split()
for i in range(k): open(f"{s}/ids_{n}_{i}.txt", "w").write(",".join(ids[i::k]))
PY
  local pids=() k p
  for ((k=0; k<JOBS; k++)); do
    [ -s "$S/ids_${NAME}_$k.txt" ] || continue
    ( python3 "$MESH/oracle/score_library.py" "${NAME}_$k" "$RUN" "$C" --points 5000 --cases "$(cat "$S/ids_${NAME}_$k.txt")" --outdir "$S/${NAME}_$k" > "$S/log_${NAME}_$k.log" 2>&1 ) &
    pids+=($!)
  done
  local bad=0; for p in "${pids[@]}"; do wait "$p" || bad=1; done
  [ $bad = 0 ] || { tail -20 "$S"/log_${NAME}_*.log; die "scoring shard failed for $NAME"; }
  head -1 "$S/${NAME}_0/results.tsv" > "$S/$NAME.tsv"
  for ((k=0; k<JOBS; k++)); do if [ -f "$S/${NAME}_$k/results.tsv" ]; then tail -n +2 "$S/${NAME}_$k/results.tsv"; fi; done | LC_ALL=C sort -t$'\t' -k1,1 -k2,2 >> "$S/$NAME.tsv"
  grep -h "^summary" "$S"/log_${NAME}_*.log | sort | uniq -c | head -3 || true
}

stage_score() {
  [ -d "$WORK/set4_corpus" ] || stage_corpus
  local b
  for b in unpatched patched; do [ -x "$WORK/geo_$b/bin/geogram_runner" ] || die "missing $WORK/geo_$b/bin/geogram_runner (build stage not run)"; done
  mkdir -p "$WORK/score"; awk -F'\t' -v n="$NPAIRS" 'NR>1 && NR<=n+1 {print $1}' "$WORK/set4_corpus/manifest.tsv" > "$WORK/score/ids_set4_slice.txt"
  for b in unpatched patched; do
    say "score $b: touch_* family (all pairs)"; shard_score "${b}_touch" "$WORK/geo_$b/bin/geogram_runner" "$WORK/touch_corpus"
    say "score $b: set 4, first $NPAIRS pairs"; shard_score "${b}_set4" "$WORK/geo_$b/bin/geogram_runner" "$WORK/set4_corpus" "$WORK/score/ids_set4_slice.txt"
  done
}

stage_report() {
  say "report"
  python3 -I "$HERE/mp21_report.py" "$WORK" "$(arch_tag)" ${MP21_REF_DIR:+--ref "$MP21_REF_DIR"}
}

case "${1:-all}" in
  fetch) stage_fetch ;;
  build-unpatched) build_geogram unpatched ;;
  build-patched) build_geogram patched ;;
  tools) stage_tools ;;
  corpus) stage_corpus ;;
  score) stage_score ;;
  report) stage_report ;;
  all) stage_fetch; build_geogram unpatched; build_geogram patched; stage_tools; stage_corpus; stage_score; stage_report ;;
  *) die "unknown stage $1" ;;
esac
