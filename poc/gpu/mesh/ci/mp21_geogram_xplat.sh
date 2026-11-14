#!/bin/bash
# MP21a: cross-platform check of the MP20 Geogram patch (informational, never gating). See poc/gpu/mesh/PLAN.md section MP21.
# usage: mp21_geogram_xplat.sh <stage>     stages: fetch | build-unpatched | build-patched | tools | corpus | score | report | all
# MP22b stages (set 5, three builds: unpatched, MP20 patch, MP20+MP22 patches): build-chain | corpus5 | score5 | report5   (see PLAN.md section MP22 / MP22b)
# Runs the same way on the M1 (WORK defaults to storage/mesh/mp21/work on the external volume) and on ubuntu-24.04 x86_64/aarch64
# (CI sets WORK). Needs: curl, tar, patch, cmake, ninja or make, g++/c++, python3, and poc/gpu/build/third_party/shewchuk/predicates.c
# (oracle/fetch.sh) plus the vendored Boost headers (src/third_party/boost).
# MP24 stages (assembled fidelity pipeline, geogram_fidelity on G1, offset, thin, touch sets 3 and 5): build-fidelity | tools24 | corpus24 | score24 | report24 | mp24 (everything; build-chain first)
# MP24b stages (pipeline v2 = exact scale + re-centre or c = 0 or hard-limit refusal; geogram_fidelity2): build-fidelity2 | tools24b | corpus24b | score24b | report24b | mp24b (everything)
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
FROZEN22="$MESH/lib/geogram/mp22/FROZEN_SHA256.txt"
PATCH22="$MESH/lib/geogram/mp22/geogram_mp22.patch"
SET5_HASHES="$MESH/lib/geogram/mp22b/SET5_HASHES_BEFORE_RUN.txt"
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
  cmake_build "$D" "$N"
  GEOGRAM_DIR="$D" sh "$MESH/lib/geogram/build.sh"
  echo "runner sha256 $(sha "$D/bin/geogram_runner")"
}

cmake_build() {  # dir name   configure + build libgeogram.a, check the restricted components were not compiled
  local D="$1" N="$2"
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
  for g in gen_touch gen_touch_set4 gen_touch_set5; do   # same flags as corpus/touch/build_gen_touch*.sh (-O2 -ffp-contract=off)
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
  local NAME="$1" RUN="$2" C="$3" IDS="${4:-}" S="${SCORE_DIR:-$WORK/score}"; mkdir -p "$S"
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
    ( python3 "${SCORER:-$MESH/oracle/score_library.py}" "${NAME}_$k" "$RUN" "$C" --points 5000 --cases "$(cat "$S/ids_${NAME}_$k.txt")" --outdir "$S/${NAME}_$k" > "$S/log_${NAME}_$k.log" 2>&1 ) &
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

# ---------------------------------------------------------------- MP22b ----------------------------------------------------------------
# One source tree, one full build, then two incremental rebuilds (each patch touches only mesh_surface_intersection.cpp): the runner is copied to
# $WORK/geo_unpatched, geo_mp20 (MP20 patch) and geo_mp22 (MP20 then MP22 patch, in this order) after each step.
stage_build_chain() {
  local D="$WORK/geo_chain" CPP=src/lib/geogram/mesh/mesh_surface_intersection.cpp
  say "build Geogram $G_VER: unpatched, then MP20 patch, then MP22 patch (one tree, incremental)"
  [ -s "$WORK/dl/geogram-$G_VER.tar.gz" ] || stage_fetch
  local want20 want20cpp want22; want20="$(awk '/geogram_mp20.patch/{print $1}' "$FROZEN")"; want20cpp="$(awk '/patched mesh_surface_intersection.cpp/{print $1}' "$FROZEN")"
  want22="$(awk '/geogram_mp22.patch/{print $1}' "$FROZEN22")"
  [ -n "$want20" ] && [ -n "$want20cpp" ] && [ -n "$want22" ] || die "FROZEN_SHA256 files lack a patch hash"
  [ "$(sha "$PATCH")" = "$want20" ] || die "geogram_mp20.patch sha256 differs from mp20/FROZEN_SHA256.txt"
  [ "$(sha "$PATCH22")" = "$want22" ] || die "geogram_mp22.patch sha256 differs from mp22/FROZEN_SHA256.txt"
  echo "patch hashes match the FROZEN files: mp20 $want20, mp22 $want22"
  extract_tree "$D"
  [ "$(sha "$D/src/$CPP")" = "$PRISTINE_CPP_SHA256" ] || die "pristine mesh_surface_intersection.cpp differs from the recorded v1.10.1 hash"
  cmake_build "$D" unpatched
  save_runner() {  # name
    GEOGRAM_DIR="$D" sh "$MESH/lib/geogram/build.sh"
    mkdir -p "$WORK/geo_$1/bin"; cp "$D/bin/geogram_runner" "$WORK/geo_$1/bin/geogram_runner"
    echo "runner geo_$1 sha256 $(sha "$WORK/geo_$1/bin/geogram_runner")"
  }
  save_runner unpatched
  patch -p1 -d "$D/src" < "$PATCH"
  got="$(sha "$D/src/$CPP")"; echo "MP20-patched file sha256 $got (frozen $want20cpp)"
  [ "$got" = "$want20cpp" ] || die "MP20-patched mesh_surface_intersection.cpp differs from the frozen hash"
  say "incremental rebuild with the MP20 patch"
  cmake --build "$D/build" -j "$JOBS" > "$D/build_mp20.log" 2>&1 || { tail -40 "$D/build_mp20.log"; die "geogram rebuild failed (mp20)"; }
  save_runner mp20
  patch -p1 -d "$D/src" < "$PATCH22"
  echo "MP22-patched file sha256 $(sha "$D/src/$CPP") (informational; FROZEN_SHA256.txt of mp22 records only the patch)"
  say "incremental rebuild with the MP22 patch on top"
  cmake --build "$D/build" -j "$JOBS" > "$D/build_mp22.log" 2>&1 || { tail -40 "$D/build_mp22.log"; die "geogram rebuild failed (mp22)"; }
  save_runner mp22
  [ "$(sha "$WORK/geo_unpatched/bin/geogram_runner")" != "$(sha "$WORK/geo_mp20/bin/geogram_runner")" ] || die "mp20 runner identical to unpatched (patch not compiled in)"
  [ "$(sha "$WORK/geo_mp20/bin/geogram_runner")" != "$(sha "$WORK/geo_mp22/bin/geogram_runner")" ] || die "mp22 runner identical to mp20 (patch not compiled in)"
}

stage_corpus5() {
  say "generate set 5, verify the generator source and every output file against the hashes recorded before any run"
  [ -x "$WORK/bin/gen_touch_set5" ] || stage_tools
  local H="$MESH/corpus/touch" d=set5_corpus
  want="$(awk '/gen_touch_set5.cpp/{print $1}' "$SET5_HASHES")"; [ -n "$want" ] || die "no generator hash in SET5_HASHES_BEFORE_RUN.txt"
  [ "$(sha "$H/gen_touch_set5.cpp")" = "$want" ] || die "gen_touch_set5.cpp differs from the hash recorded before the run"
  rm -rf "${WORK:?}/$d"; "$WORK/bin/gen_touch_set5" "$WORK/$d" > "$WORK/$d.gen.log" 2>&1 || { tail -5 "$WORK/$d.gen.log"; die "gen_touch_set5 failed"; }
  tail -1 "$WORK/$d.gen.log"
  if ( cd "$WORK/$d" && shacheck "$H/EXPECTED_SHA256_set5.txt" > "$WORK/$d.verify.log" 2>&1 ); then echo "$d: all $(wc -l < "$H/EXPECTED_SHA256_set5.txt") files match EXPECTED_SHA256_set5.txt"
  else echo "$d: MISMATCH:"; grep -v ': OK$' "$WORK/$d.verify.log" | head -20; die "generated set 5 differs from EXPECTED_SHA256_set5.txt (different generator output on this platform); results would not be comparable. Stopping."; fi
}

# SHARD=k/N scores only pair indices i with i % N == k (default 0/1 = everything); outputs <build>_set5.tsv (or <build>_set5_sK.tsv when N > 1).
stage_score5() {
  [ -d "$WORK/set5_corpus" ] || stage_corpus5
  local b k=0 n=1 suf=""; if [ -n "${SHARD:-}" ]; then k="${SHARD%/*}"; n="${SHARD#*/}"; fi; [ "$n" = 1 ] || suf="_s$k"
  mkdir -p "$WORK/score"; tail -n +2 "$WORK/set5_corpus/manifest.tsv" | cut -f1 | awk -v k="$k" -v n="$n" '(NR-1)%n==k' > "$WORK/score/ids_set5$suf.txt"
  echo "scoring $(wc -l < "$WORK/score/ids_set5$suf.txt") of $(( $(wc -l < "$WORK/set5_corpus/manifest.tsv") - 1 )) pairs (shard $k/$n) with $JOBS parallel jobs per build"
  for b in unpatched mp20 mp22; do
    [ -x "$WORK/geo_$b/bin/geogram_runner" ] || die "missing $WORK/geo_$b/bin/geogram_runner (build-chain stage not run)"
    say "score $b on set 5 (shard $k/$n)"; shard_score "${b}_set5$suf" "$WORK/geo_$b/bin/geogram_runner" "$WORK/set5_corpus" "$WORK/score/ids_set5$suf.txt"
    [ "$b" = mp22 ] || find "$WORK/score" -path "*/${b}_set5${suf}_*/*.msh" -delete    # only the MP22 outputs are kept and hashed
  done
}

stage_report5() {
  say "report (MP22b)"
  python3 -I "$HERE/mp22b_report.py" "$WORK" "$(arch_tag)" "$(( $(wc -l < "$WORK/set5_corpus/manifest.tsv") - 1 ))"
}

# ---------------------------------------------------------------- MP24 ----------------------------------------------------------------
# geogram_fidelity = exact validation + exact re-centring + MP22-patched Geogram + translate back, one process (lib/geogram/mp24). Built against the MP22-state tree
# $WORK/geo_chain left by build-chain. Scored with lib/geogram/mp24/score_fidelity.py (oracle/score_library.py plus a `fidelity` column), GEOGRAM_RUNNER_THREADS=1.
FROZEN24="$MESH/lib/geogram/mp24/FROZEN_SHA256.txt"
FID_DIR="$MESH/corpus/fidelity"
FID="${FID_BIN:-$WORK/geo_chain/bin/geogram_fidelity}"        # FID_BIN: a planted mutant (lib/geogram/mp24/run_mutants.sh)
SCORE24_DIR="${SCORE24_DIR:-$WORK/score24}"
FAMS24="${FAMS24:-g1:g1_corpus off:fid_corpus/offset thin:fid_corpus/thin set3:set3_corpus set5:set5_corpus}"
stage_build_fidelity() {
  say "build geogram_fidelity (sources checked against mp24/FROZEN_SHA256.txt)"
  [ -f "$WORK/geo_chain/build/lib/libgeogram.a" ] || die "no $WORK/geo_chain (build-chain stage not run)"
  ( cd "$ROOT" && grep -v '^FROZEN' "$FROZEN24" > "$WORK/frozen24.sums" && shacheck "$WORK/frozen24.sums" ) || die "a geogram_fidelity source or a frozen input differs from mp24/FROZEN_SHA256.txt"
  GEOGRAM_DIR="$WORK/geo_chain" OUTDIR="$WORK/geo_chain/bin" sh "$MESH/lib/geogram/mp24/build.sh"
  echo "geogram_fidelity sha256 $(sha "$FID")"
}
stage_tools24() {
  say "build oracle tools plus gen_corpus, gen_touch_set3 and gen_fidelity"
  stage_tools
  local B="$ROOT/src/third_party/boost"; local INC=(); for d in "$B"/libs/*/include; do INC+=("-I$d"); done
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off -I"$B" "${INC[@]}" "$MESH/corpus/gen_corpus.cpp" -o "$WORK/bin/gen_corpus"
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off -I"$B" "${INC[@]}" "$MESH/corpus/touch/gen_touch_set3.cpp" "$WORK/libshewchuk_predicates.a" -o "$WORK/bin/gen_touch_set3"
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off -I"$B" "${INC[@]}" "$FID_DIR/gen_fidelity.cpp" -o "$WORK/bin/gen_fidelity"
}
stage_corpus24() {
  say "generate G1, touch set 3, touch set 5 and the MP24 families; verify generator sources and every file against the hashes recorded before the runs"
  [ -x "$WORK/bin/gen_fidelity" ] || stage_tools24
  local want; want="$(awk '/gen_fidelity.cpp/{print $1}' "$FID_DIR/GENERATOR_SHA256.txt")"; [ -n "$want" ] || die "no generator hash in corpus/fidelity/GENERATOR_SHA256.txt"
  [ "$(sha "$FID_DIR/gen_fidelity.cpp")" = "$want" ] || die "gen_fidelity.cpp differs from the hash recorded before the run"
  rm -rf "${WORK:?}/g1_corpus"; "$WORK/bin/gen_corpus" "$WORK/g1_corpus" --many 200 > "$WORK/g1_corpus.gen.log" 2>&1 || die "gen_corpus failed"
  if ( cd "$WORK/g1_corpus" && shacheck "$MESH/corpus/EXPECTED_SHA256.txt" > "$WORK/g1_corpus.verify.log" 2>&1 ); then echo "g1_corpus: all files match corpus/EXPECTED_SHA256.txt"
  else grep -v ': OK$' "$WORK/g1_corpus.verify.log" | head; die "G1 corpus differs from corpus/EXPECTED_SHA256.txt on this platform"; fi
  rm -rf "${WORK:?}/set3_corpus"; "$WORK/bin/gen_touch_set3" "$WORK/set3_corpus" > "$WORK/set3_corpus.gen.log" 2>&1 || { tail -5 "$WORK/set3_corpus.gen.log"; die "gen_touch_set3 failed"; }
  if ( cd "$WORK/set3_corpus" && shacheck "$MESH/corpus/touch/EXPECTED_SHA256_set3.txt" > "$WORK/set3_corpus.verify.log" 2>&1 ); then echo "set3_corpus: all files match EXPECTED_SHA256_set3.txt"
  else grep -v ': OK$' "$WORK/set3_corpus.verify.log" | head; die "set 3 differs from EXPECTED_SHA256_set3.txt on this platform"; fi
  stage_corpus5
  rm -rf "${WORK:?}/fid_corpus"; "$WORK/bin/gen_fidelity" "$WORK/fid_corpus" > "$WORK/fid_corpus.gen.log" 2>&1 || die "gen_fidelity failed"; cat "$WORK/fid_corpus.gen.log"
  if ( cd "$WORK/fid_corpus" && shacheck "$FID_DIR/EXPECTED_SHA256.txt" > "$WORK/fid_corpus.verify.log" 2>&1 ) && [ "$(find "$WORK/fid_corpus" -type f | wc -l | tr -d ' ')" = "$(wc -l < "$FID_DIR/EXPECTED_SHA256.txt" | tr -d ' ')" ]; then
    echo "fid_corpus: all $(wc -l < "$FID_DIR/EXPECTED_SHA256.txt") files match corpus/fidelity/EXPECTED_SHA256.txt"
  else grep -v ': OK$' "$WORK/fid_corpus.verify.log" | head; die "MP24 families differ from corpus/fidelity/EXPECTED_SHA256.txt on this platform (different generator output); results would not be comparable. Stopping."; fi
}
stage_score24() {
  [ -x "$FID" ] || die "missing $FID (build-fidelity stage not run)"
  [ -d "$WORK/fid_corpus/offset" ] || stage_corpus24
  export SCORER="$MESH/lib/geogram/mp24/score_fidelity.py"
  rm -rf "${SCORE24_DIR:?}"; mkdir -p "$SCORE24_DIR"
  export SCORE_DIR="$SCORE24_DIR"
  local pair n c
  for pair in $FAMS24; do
    n="${pair%%:*}"; c="$WORK/${pair#*:}"; say "score geogram_fidelity: $n"
    shard_score "$n" "$FID" "$c"
  done
}
stage_report24() {
  say "report (MP24)"
  python3 -I "$HERE/mp24_report.py" "$WORK" "$(arch_tag)"
}

# ---------------------------------------------------------------- MP24b ----------------------------------------------------------------
# geogram_fidelity2 = pipeline v2 (lib/geogram/mp24b): exact scale to max|x| < 2^18, exact re-centring or c = 0 (|centre| <= T x side, T = 2^12 from the development sweep) or a hard-limit refusal.
# Frozen sources: mp24b/FROZEN_SHA256.txt (checked before the build). Fresh acceptance families: corpus/fidelity2 (gen_fidelity2.cpp, EXPECTED_SHA256.txt, GENERATOR_SHA256.txt).
# The MP24 families (G1, offset, thin, set 3, set 5) are re-run through v2. Scored with lib/geogram/mp24b/score_fidelity2.py, GEOGRAM_RUNNER_THREADS=1. Each family is scored once.
FROZEN24B="$MESH/lib/geogram/mp24b/FROZEN_SHA256.txt"
FID2_DIR="$MESH/corpus/fidelity2"
FID2="${FID2_BIN:-$WORK/geo_chain/bin/geogram_fidelity2}"      # FID2_BIN: a planted mutant (lib/geogram/mp24b/run_mutants.sh)
SCORE24B_DIR="${SCORE24B_DIR:-$WORK/score24b}"
FAMS24B="${FAMS24B:-g1:g1_corpus off:fid_corpus/offset thin:fid_corpus/thin set3:set3_corpus set5:set5_corpus f2:fid2_corpus}"
stage_build_fidelity2() {
  say "build geogram_fidelity2 (sources checked against mp24b/FROZEN_SHA256.txt)"
  [ -f "$WORK/geo_chain/build/lib/libgeogram.a" ] || die "no $WORK/geo_chain (build-chain stage not run)"
  ( cd "$ROOT" && grep -v '^FROZEN' "$FROZEN24B" > "$WORK/frozen24b.sums" && shacheck "$WORK/frozen24b.sums" ) || die "a geogram_fidelity2 source or a frozen input differs from mp24b/FROZEN_SHA256.txt"
  GEOGRAM_DIR="$WORK/geo_chain" OUTDIR="$WORK/geo_chain/bin" MUTANTS="${MUTANTS:-0}" sh "$MESH/lib/geogram/mp24b/build.sh"
  echo "geogram_fidelity2 sha256 $(sha "$FID2")"
}
stage_tools24b() {
  say "build oracle tools, the MP24 generators and gen_fidelity2"
  stage_tools24
  local B="$ROOT/src/third_party/boost"; local INC=(); for d in "$B"/libs/*/include; do INC+=("-I$d"); done
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off -I"$B" "${INC[@]}" "$FID2_DIR/gen_fidelity2.cpp" -o "$WORK/bin/gen_fidelity2"
}
stage_corpus24b() {
  say "generate G1, set 3, set 5, the MP24 families and the MP24b families; verify generator sources and every file against the hashes recorded before the runs"
  [ -x "$WORK/bin/gen_fidelity2" ] || stage_tools24b
  stage_corpus24
  local f want
  for f in gen_fidelity2.cpp build_gen_fidelity2.sh EXPECTED_SHA256.txt; do
    want="$(awk -v f="/$f" 'index($2, f) {print $1}' "$FID2_DIR/GENERATOR_SHA256.txt" | head -1)"; [ -n "$want" ] || die "no hash for $f in corpus/fidelity2/GENERATOR_SHA256.txt"
    [ "$(sha "$FID2_DIR/$f")" = "$want" ] || die "$f differs from the hash recorded before the run"
  done
  want="$(awk '/corpus\/fidelity\/gen_fidelity.cpp/{print $1}' "$FID2_DIR/GENERATOR_SHA256.txt")"; [ "$(sha "$MESH/corpus/fidelity/gen_fidelity.cpp")" = "$want" ] || die "gen_fidelity.cpp (included by gen_fidelity2) differs from the recorded hash"
  rm -rf "${WORK:?}/fid2_corpus"; "$WORK/bin/gen_fidelity2" "$WORK/fid2_corpus" > "$WORK/fid2_corpus.gen.log" 2>&1 || die "gen_fidelity2 failed"; cat "$WORK/fid2_corpus.gen.log"
  if ( cd "$WORK/fid2_corpus" && shacheck "$FID2_DIR/EXPECTED_SHA256.txt" > "$WORK/fid2_corpus.verify.log" 2>&1 ) && [ "$(find "$WORK/fid2_corpus" -type f | wc -l | tr -d ' ')" = "$(wc -l < "$FID2_DIR/EXPECTED_SHA256.txt" | tr -d ' ')" ]; then
    echo "fid2_corpus: all $(wc -l < "$FID2_DIR/EXPECTED_SHA256.txt") files match corpus/fidelity2/EXPECTED_SHA256.txt"
  else grep -v ': OK$' "$WORK/fid2_corpus.verify.log" | head; die "MP24b families differ from corpus/fidelity2/EXPECTED_SHA256.txt on this platform (different generator output); results would not be comparable. Stopping."; fi
}
stage_score24b() {
  [ -x "$FID2" ] || die "missing $FID2 (build-fidelity2 stage not run)"
  [ -d "$WORK/fid2_corpus" ] || stage_corpus24b
  export SCORER="$MESH/lib/geogram/mp24b/score_fidelity2.py"
  rm -rf "${SCORE24B_DIR:?}"; mkdir -p "$SCORE24B_DIR"
  export SCORE_DIR="$SCORE24B_DIR"
  local pair n c
  for pair in $FAMS24B; do
    n="${pair%%:*}"; c="$WORK/${pair#*:}"; say "score geogram_fidelity2: $n"
    shard_score "$n" "$FID2" "$c"
  done
}
stage_report24b() {
  say "report (MP24b)"
  python3 -I "$HERE/mp24b_report.py" "$WORK" "$(arch_tag)"
}

case "${1:-all}" in
  fetch) stage_fetch ;;
  build-unpatched) build_geogram unpatched ;;
  build-patched) build_geogram patched ;;
  tools) stage_tools ;;
  corpus) stage_corpus ;;
  score) stage_score ;;
  report) stage_report ;;
  build-chain) stage_build_chain ;;
  corpus5) stage_corpus5 ;;
  score5) stage_score5 ;;
  report5) stage_report5 ;;
  build-fidelity) stage_build_fidelity ;;
  tools24) stage_tools24 ;;
  corpus24) stage_corpus24 ;;
  score24) stage_score24 ;;
  report24) stage_report24 ;;
  build-fidelity2) stage_build_fidelity2 ;;
  tools24b) stage_tools24b ;;
  corpus24b) stage_corpus24b ;;
  score24b) stage_score24b ;;
  report24b) stage_report24b ;;
  mp24b) stage_fetch; stage_build_chain; stage_build_fidelity2; stage_tools24b; stage_corpus24b; stage_score24b; stage_report24b ;;
  mp24) stage_fetch; stage_build_chain; stage_build_fidelity; stage_tools24; stage_corpus24; stage_score24; stage_report24 ;;
  mp22b) stage_fetch; stage_build_chain; stage_tools; stage_corpus5; stage_score5; stage_report5 ;;
  all) stage_fetch; build_geogram unpatched; build_geogram patched; stage_tools; stage_corpus; stage_score; stage_report ;;
  *) die "unknown stage $1" ;;
esac
