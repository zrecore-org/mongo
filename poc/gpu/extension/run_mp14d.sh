#!/bin/bash
# MP14d bars. Step 1 (CPU path): core vs exactmesh oracle + mutants, stage plumbing in the fake host, old MP14a suite unaffected.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; R="$(cd "$HERE/../../.." && pwd)"; OUT="$R/storage/extension_build"
"$HERE/build.sh" ext host
"$HERE/run_contains_core.sh" | tail -6
cd "$OUT"; ./fakehost contains ./libmeshvol_ext.dylib | tail -16
./fakehost all . | grep SUMMARY
