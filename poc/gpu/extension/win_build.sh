#!/bin/bash
# MP16 step 1: cross-builds the extension (libmeshvol_ext.dll) and its test programs for Windows x86_64 with mingw-w64 (posix threads) inside a Linux container. Nothing is run.
# Usage (in a container with g++-mingw-w64-x86-64-posix, gcc-mingw-w64-x86-64-posix): win_build.sh <fork repo> <exactmesh repo> <outdir>
set -e
R="$1"; EM="$2"; OUT="$3"; E="$R/poc/gpu/extension"; mkdir -p "$OUT"
CC=x86_64-w64-mingw32-gcc-posix; CXX=x86_64-w64-mingw32-g++-posix
BOOST="-I$R/src/third_party/boost"; for d in $R/src/third_party/boost/libs/*/include; do BOOST="$BOOST -I$d"; done
mkdir -p "$OUT/inc/mongo/db/extension"; ln -sfn "$R/src/mongo/db/extension/public" "$OUT/inc/mongo/db/extension/public"
VKH="$EM/build/deps/Vulkan-Headers-1.3.283/include"; VOLK="$EM/build/deps/volk-vulkan-sdk-1.3.283.0"
COMMON="-std=c++20 -O2 -ffp-contract=off -w -I$E/stub_inc -I$OUT/inc -I$E $BOOST -I$VKH -I$VOLK"
$CC -O2 -w -I$VKH -I$VOLK -c "$VOLK/volk.c" -o "$OUT/volk.o"
$CC -O2 -w -Drandom=rand -c "$EM/build/deps/shewchuk/predicates.c" -o "$OUT/shewchuk.o"
$CXX $COMMON -DMESHVOL_VULKAN_PROBE -DMESHVOL_GPU -shared -static -static-libgcc -static-libstdc++ "$E/extension.cpp" "$OUT/volk.o" -o "$OUT/libmeshvol_ext.dll"
for m in 1 2 3 4; do $CXX $COMMON -DMESHVOL_VULKAN_PROBE -DMESHVOL_GPU -DMESHVOL_GPUMUT=$m -shared -static -static-libgcc -static-libstdc++ "$E/extension.cpp" "$OUT/volk.o" -o "$OUT/libmeshvol_gpumut$m.dll"; done
$CXX $COMMON -DMESHVOL_VULKAN_PROBE -static "$E/host.cpp" -o "$OUT/fakehost.exe" -lpsapi
for t in test_contains_core test_contains_ind; do $CXX $COMMON -I$EM -I$EM/harness -static "$E/$t.cpp" "$OUT/shewchuk.o" -o "$OUT/$t.exe"; done
$CXX $COMMON -I$EM -I$EM/harness -static "$E/real_contains.cpp" "$OUT/shewchuk.o" -o "$OUT/real_contains.exe"
$CXX $COMMON -I$EM -I$EM/harness -static "$E/real_gpu.cpp" "$OUT/shewchuk.o" "$OUT/volk.o" -o "$OUT/real_gpu.exe"
ls -la "$OUT"/*.exe "$OUT"/*.dll
