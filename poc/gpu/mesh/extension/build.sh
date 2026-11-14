#!/usr/bin/env bash
# MP14a build + run. Output only under storage/mesh/extension/ (external volume). No bazel, no mongod, no GPU.
# Usage: build.sh   -> build everything, run the harness normally and once under ASan+UBSan.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../../.." && pwd)"
OUT="$REPO/storage/mesh/extension"
export TMPDIR="$REPO/storage/tmp"
mkdir -p "$OUT/include/mongo/db/extension/public" "$OUT/include/mongo/util" "$TMPDIR"
# Symlinks (not copies) to the two public headers. The headers' own fallback branch
# (`#define[[MONGO_MOD_PUBLIC]]`) is invalid C++, so a 3-line shim modules.h defines MONGO_MOD_PUBLIC as empty.
ln -sf "$REPO/src/mongo/db/extension/public/api.h"          "$OUT/include/mongo/db/extension/public/api.h"
ln -sf "$REPO/src/mongo/db/extension/public/api_contract.h" "$OUT/include/mongo/db/extension/public/api_contract.h"
printf '// POC shim (NOT the server header).\n#pragma once\n#define MONGO_MOD_PUBLIC\n' > "$OUT/include/mongo/util/modules.h"

printf '_get_mongodb_extension_versions\n_get_mongodb_extension\n' > "$OUT/exports.txt"
printf '_get_mongodb_extension_versions\n_get_mongodb_extension\n_mesh_ext_test_counters\n' > "$OUT/exports_test.txt"
printf '{ global: get_mongodb_extension_versions; get_mongodb_extension; local: *; };\n' > "$OUT/exports.map"
printf '{ global: get_mongodb_extension_versions; get_mongodb_extension; mesh_ext_test_counters; local: *; };\n' > "$OUT/exports_test.map"
if [ "$(uname)" = Darwin ]; then
  EXT=dylib; SHARED="-dynamiclib"; LD_PROD="-Wl,-exported_symbols_list,$OUT/exports.txt"; LD_TEST="-Wl,-exported_symbols_list,$OUT/exports_test.txt"
else
  EXT=so; SHARED="-shared"; LD_PROD="-Wl,--version-script=$OUT/exports.map"; LD_TEST="-Wl,--version-script=$OUT/exports_test.map"
fi
CXX=${CXX:-clang++}
COMMON="-std=c++20 -Wall -Wextra -fvisibility=hidden -fPIC -I$OUT/include -I$HERE -g -O1"
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"

# production-shaped extension (exactly two exported symbols), test-hook extension (+counter export), helper, harness
$CXX $COMMON -Wno-exceptions $SHARED $LD_PROD "$HERE/mesh_extension.cpp" -o "$OUT/mesh_extension_prod.$EXT"
$CXX $COMMON -Wno-exceptions -DMESH_TEST_HOOKS $SHARED $LD_TEST "$HERE/mesh_extension.cpp" -o "$OUT/mesh_extension.$EXT"
$CXX $COMMON $SHARED "$HERE/mesh_helper.cpp" -o "$OUT/libmeshhelper.$EXT"
$CXX -std=c++20 -Wall -Wextra -Wno-unused-parameter -g -I"$OUT/include" -I"$HERE" "$HERE/fake_host.cpp" -o "$OUT/fake_host"
# ASan + UBSan variants (the extension must be instrumented too)
$CXX $COMMON $SAN -Wno-exceptions -DMESH_TEST_HOOKS $SHARED $LD_TEST "$HERE/mesh_extension.cpp" -o "$OUT/asan_mesh_extension.$EXT"
$CXX $COMMON $SAN $SHARED "$HERE/mesh_helper.cpp" -o "$OUT/asan_libmeshhelper.$EXT"
$CXX -std=c++20 -Wno-unused-parameter -g $SAN -I"$OUT/include" -I"$HERE" "$HERE/fake_host.cpp" -o "$OUT/asan_fake_host"

echo "--- exported symbols (production-shaped extension) ---"
if [ "$(uname)" = Darwin ]; then nm -gU "$OUT/mesh_extension_prod.$EXT"; else nm -D --defined-only "$OUT/mesh_extension_prod.$EXT"; fi
echo "--- dynamic dependencies (production-shaped extension) ---"
if [ "$(uname)" = Darwin ]; then otool -L "$OUT/mesh_extension_prod.$EXT"; else ldd "$OUT/mesh_extension_prod.$EXT"; fi
echo "--- headers included by the extension (clang -M, filtered) ---"
$CXX $COMMON -MM "$HERE/mesh_extension.cpp" | tr ' ' '\n' | grep -v '^\\$' | grep -E '\.h$' || true
echo "--- run: plain ---"
set +e
"$OUT/fake_host" "$OUT/mesh_extension.$EXT" "$OUT/libmeshhelper.$EXT" 2>&1 | tee "$OUT/harness_plain.txt"
LEAKS=1; [ "$(uname)" = Darwin ] && LEAKS=0   # LeakSanitizer is unsupported on macOS (verified: "detect_leaks is not supported on this platform")
echo "--- run: ASan+UBSan (detect_leaks=$LEAKS) ---"
ASAN_OPTIONS=detect_leaks=$LEAKS:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  "$OUT/asan_fake_host" "$OUT/asan_mesh_extension.$EXT" "$OUT/asan_libmeshhelper.$EXT" 2>&1 | tee "$OUT/harness_asan.txt"
