#!/bin/bash
# Compiles the Slang df64 self-test kernels into build/slang (names match the GLSL build in build/shaders).
# Usage: slang/build_df64.sh [path to slangc]; then GPU_POC_DF64_DIR=build/slang ./run.sh poc8_selftest [precise|nofma|auto]
cd "$(dirname "$0")/.." && mkdir -p build/slang
S=${1:-slangc}
$S slang/selftest.slang -target spirv -entry main -stage compute -o build/slang/selftest.spv || exit 1
$S slang/selftest.slang -target spirv -entry main -stage compute -DNOFMA -o build/slang/selftest_nofma.spv || exit 1
$S slang/df64.slang -target spirv -entry main -stage compute -o build/slang/df64_precise.spv || exit 1
$S slang/df64.slang -target spirv -entry main -stage compute -DNOFMA -o build/slang/df64_nofma.spv || exit 1
