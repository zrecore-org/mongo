#!/bin/bash
# Builds slang/cpu_twin.cpp for {fma, nofma} x {default FP contraction, -ffp-contract=off}. Needs build/slang/df64_*.spv (build_df64.sh).
# Also builds cpu_twin_split_* (selftest.slang: twoProd and splitDouble).
# Usage: slang/build_cpu_twin.sh [slangc]   then   tools/with_gpu_lock.sh build/cpu_twin_<variant>_<contract>
cd "$(dirname "$0")/.." && S=${1:-slangc}; mkdir -p build/slangcpu
SDK=${VKSDK:-$HOME/VulkanSDK/1.3.283.0/macOS}
sed 's/void main(/void df64Main(/' slang/df64.slang > build/slangcpu/df64_cpu.slang
cp slang/df64_ops.slang build/slangcpu/
for v in fma nofma; do
  D=; [ $v = nofma ] && D=-DNOFMA
  $S build/slangcpu/df64_cpu.slang -target cpp -entry df64Main -stage compute $D -o build/slangcpu/df64_$v.cpp 2>/dev/null || exit 1
  spv=build/slang/df64_precise.spv; [ $v = nofma ] && spv=build/slang/df64_nofma.spv
  for c in default off; do
    F=; [ $c = off ] && F=-ffp-contract=off
    clang++ -O3 -std=gnu++20 $F -DVK_NO_PROTOTYPES -DTWIN_CPP="\"build/slangcpu/df64_$v.cpp\"" -DTWIN_SPV="\"$spv\"" \
      -I"$SDK/include" -I"$SDK/include/volk" -I. -o build/cpu_twin_${v}_$c slang/cpu_twin.cpp build/libvolk.a || exit 1
  done
done

sed 's/void main(/void selftestMain(/' slang/selftest.slang > build/slangcpu/selftest_cpu.slang
for v in fma nofma; do
  D=; [ $v = nofma ] && D=-DNOFMA
  $S build/slangcpu/selftest_cpu.slang -target cpp -entry selftestMain -stage compute $D -o build/slangcpu/selftest_$v.cpp 2>/dev/null || exit 1
  spv=build/slang/selftest.spv; [ $v = nofma ] && spv=build/slang/selftest_nofma.spv
  for c in default off; do
    F=; [ $c = off ] && F=-ffp-contract=off
    clang++ -O3 -std=gnu++20 $F -DVK_NO_PROTOTYPES -DTWIN_CPP="\"build/slangcpu/selftest_$v.cpp\"" -DTWIN_SPV="\"$spv\"" \
      -I"$SDK/include" -I"$SDK/include/volk" -I. -o build/cpu_twin_split_${v}_$c slang/cpu_twin_split.cpp build/libvolk.a || exit 1
  done
done
