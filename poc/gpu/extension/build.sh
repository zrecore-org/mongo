#!/bin/bash
# MP14a build. All outputs go to storage/extension_build (git-ignored, external volume). Usage: ./build.sh [target ...]; no args builds everything.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../../.." && pwd)"
OUT="$R/storage/extension_build"
mkdir -p "$OUT/inc/mongo/db/extension"
ln -sfn "$R/src/mongo/db/extension/public" "$OUT/inc/mongo/db/extension/public"   # public API headers by symlink; nothing under src/ is changed
BOOST=$("$HERE/boost_inc.sh")
VKH=/Volumes/CrucialX9/Projects/exactmesh/build/deps/Vulkan-Headers-1.3.283/include   # read only, pinned by exactmesh/third_party/fetch.sh
COMMON="-std=c++20 -O2 -ffp-contract=off -Wall -Wextra -Wno-unused-const-variable -Wno-unused-function -I$HERE/stub_inc -I$OUT/inc -I$HERE"
EXT="$COMMON -fPIC -fvisibility=hidden -fvisibility-inlines-hidden $BOOST -dynamiclib -Wl,-exported_symbols_list,$HERE/exports.list"
t() { [ $# -eq 0 ] || echo "$TARGETS" | grep -qw "$1"; }
TARGETS="${*:-all}"
want() { [ "$TARGETS" = all ] || echo " $TARGETS " | grep -q " $1 "; }
VOLK=/Volumes/CrucialX9/Projects/exactmesh/build/deps/volk-vulkan-sdk-1.3.283.0   # volk (MIT), read only, pinned by exactmesh/third_party/fetch.sh
if want ext;      then clang -O2 -w -fPIC -fvisibility=hidden -I$VKH -I$VOLK -c "$VOLK/volk.c" -o "$OUT/volk.o" && clang++ $EXT -DMESHVOL_VULKAN_PROBE -DMESHVOL_GPU -I$VKH -I$VOLK "$HERE/extension.cpp" "$OUT/volk.o" -o "$OUT/libmeshvol_ext.dylib" & fi
for m in 1 2 3 4; do if want gpumut$m; then clang++ $EXT -DMESHVOL_VULKAN_PROBE -DMESHVOL_GPU -DMESHVOL_GPUMUT=$m -I$VKH -I$VOLK "$HERE/extension.cpp" "$OUT/volk.o" -o "$OUT/libmeshvol_gpumut$m.dylib" & fi; done   # MP14d device-path mutants
if want mut1;     then clang++ $EXT -DMESHVOL_MUTANT=1 "$HERE/extension.cpp" -o "$OUT/libmeshvol_mut1.dylib" & fi
if want mut2;     then clang++ $EXT -DMESHVOL_MUTANT=2 "$HERE/extension.cpp" -o "$OUT/libmeshvol_mut2.dylib" & fi
if want mut3;     then clang++ $EXT -DMESHVOL_MUTANT=3 "$HERE/extension.cpp" -o "$OUT/libmeshvol_mut3.dylib" & fi
if want nocatch;  then clang++ $EXT -DNO_BOUNDARY_CATCH "$HERE/extension.cpp" -o "$OUT/libmeshvol_nocatch.dylib" & fi
for m in 1 2 4 5 6 7; do if want cfgmut$m; then clang++ $EXT -DMESHVOL_VULKAN_PROBE -I$VKH -DMESHVOL_MUTANT_CFG=$m "$HERE/extension.cpp" -o "$OUT/libmeshvol_cfgmut$m.dylib" & fi; done   # MP14c mutants
if want cfgmut3;  then clang++ $EXT -DMESHVOL_MUTANT_CFG=3 "$HERE/extension.cpp" -o "$OUT/libmeshvol_cfgmut3.dylib" & fi   # built WITHOUT the probe so that "gpuRequired ignored" is observable
if want noprobe;  then clang++ $EXT "$HERE/extension.cpp" -o "$OUT/libmeshvol_noprobe.dylib" & fi   # MP14c: built without the Vulkan probe: gpu=true must report not ready
if want novis;    then clang++ $COMMON -fPIC $BOOST -dynamiclib "$HERE/extension.cpp" -o "$OUT/libmeshvol_novis.dylib" & fi   # default visibility, no export list: symbol probe
if want bad;      then
  for v in 1 2 3 4 5 6 7; do clang++ $COMMON -fPIC -dynamiclib -DBAD_VARIANT=$v -I"$HERE" "$HERE/bad_ext.cpp" -o "$OUT/libbad_v$v.dylib" & done
fi
if want host;     then clang++ $COMMON $BOOST -pthread -Wl,-export_dynamic "$HERE/host.cpp" -o "$OUT/fakehost" & fi
wait
echo build done
