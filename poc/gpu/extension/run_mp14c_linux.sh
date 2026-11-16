#!/bin/bash
# MP14c real-stack reason strings on Linux, inside a container (aarch64 or x86_64). Usage: run_mp14c_linux.sh <repo root> <work dir>. Installs g++ and, in turn, nothing / lavapipe.
# Case A: no Vulkan loader installed: gpu true -> "vulkan loader not found", no crash (regression for the dlerror()-twice NULL string_view crash found by the cold-read test).
# Case B: libvulkan1 only (no ICD): loader opens, instance may work with zero devices: "no physical devices" (or instance failure).
# Case C: libvulkan1 + mesa-vulkan-drivers (lavapipe): "software device only".
set -u
R=$1; W=$2; FAILS=0
chk() { if [ "$2" = ok ]; then echo "PASS $1"; else echo "FAIL $1"; FAILS=$((FAILS+1)); fi; }
has() { echo "$1" | grep -q -- "$2" && echo ok || echo no; }
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null; apt-get install -y -qq g++ >/dev/null 2>&1
mkdir -p $W; bash $R/storage/lx/build_lx.sh $R $W/out >/dev/null 2>&1 || { echo "build failed"; exit 2; }
cd $W/out
run() { ./fakehost options ./libmeshvol_ext.so "$1" 2>&1; }
o=$(run "gpu: true"); echo "$o" | grep -E "OPTIONS|PROBE"
chk "A no loader: loads, no crash, reason loader not found" "$([ "$(has "$o" 'OPTIONS-LOAD ok=1')$(has "$o" 'reason="vulkan loader not found"')" = okok ] && echo ok)"
o=$(run $'gpu: true\ngpuRequired: true'); chk "A no loader + gpuRequired: initialize fails with the reason" "$([ "$(has "$o" 'OPTIONS-LOAD ok=0')$(has "$o" 'not ready: vulkan loader not found')" = okok ] && echo ok)"
apt-get install -y -qq --no-install-recommends libvulkan1 >/dev/null 2>&1
o=$(run "gpu: true"); echo "$o" | grep -E "OPTIONS|PROBE"
chk "B loader without driver: loads, gpuReady=false, reason has no path" "$([ "$(has "$o" 'OPTIONS-LOAD ok=1')$(has "$o" 'gpuReady=false')" = okok ] && [ "$(echo "$o" | grep '^PROBE config' | grep -o 'reason="[^"]*"' | grep -c '/')" = 0 ] && echo ok)"
apt-get install -y -qq mesa-vulkan-drivers >/dev/null 2>&1
o=$(run "gpu: true"); echo "$o" | grep -E "OPTIONS|PROBE"
chk "C lavapipe: gpuReady=false, reason software device only" "$([ "$(has "$o" 'gpuReady=false')$(has "$o" 'reason="software device only"')" = okok ] && echo ok)"
o=$(run $'gpu: true\ngpuRequired: true'); chk "C lavapipe + gpuRequired: initialize fails" "$(has "$o" 'OPTIONS-LOAD ok=0')"
echo "FAILS $FAILS"
