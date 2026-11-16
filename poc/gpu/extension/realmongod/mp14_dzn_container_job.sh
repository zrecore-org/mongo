#!/bin/bash
# MP14d device path in the real fork mongod on the 4060 Ti through Mesa Dozen. NEEDS THE GPU TURN. Same container flags as the MP14c dzn run (device /dev/dxg, /usr/lib/wsl, private dzn build, VK_DRIVER_FILES, LD_LIBRARY_PATH).
# No mesa-vulkan-drivers (no lavapipe): the only Vulkan device is the dzn one. Case gpu_dzn: gpu true, gpuMinPoints 1, EXPECT_GPU=1: the device path must be used for the 60 models, results equal the oracle
# (hash ae59d9976297a67b), then a planted wrong-verdict run must be caught by the shadow check and disable the device.
set -u
# WSL2 core dumps (investigated 2026-11-16): the WSL core_pattern is a PIPE handler, which ignores RLIMIT_CORE=0; only a limit of exactly 1 byte stops it. `ulimit -c 0` must NOT be used (it would
# re-enable dumps if the container runs with --ulimit core=1:1). bash cannot express 1 byte, so prlimit does it:
prlimit --core=1:1 --pid $$ 2>/dev/null || true
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq g++ gcc python3-venv python3-pip libcurl4 libssl3 liblzma5 ca-certificates libvulkan1 vulkan-tools >/dev/null 2>&1
rm -rf /w/mp14d_dzn && mkdir -p /w/mp14d_dzn && cd /w/mp14d_dzn && tar -xzf /in/mp14f_bundle.tgz && tar -xzf /in/mp14g_run.tgz
sha256sum /in/mp14f_bundle.tgz /in/mp14g_run.tgz /m/mongod | tee inputs.sha256
vulkaninfo --summary 2>&1 | grep -E "deviceName|deviceType|driverName|apiVersion" | head -8 | tee vulkaninfo.out
(cd g5_bundle && ./build_linux.sh 2>&1 | tail -2)
python3 -m venv /w/mp14d_dzn/mp14d_pkg/venv && /w/mp14d_dzn/mp14d_pkg/venv/bin/pip -q install pymongo 2>&1 | tail -1
cd mp14d_pkg && CASES="gpu_dzn_sigterm" ./run_mp14d_real.sh /m/mongod /w/mp14d_dzn/g5_bundle/out/libmeshvol_ext.so /w/mp14d_dzn/mp14d_pkg/real60 2>&1 | tee run_mp14d_real.out
sha256sum run_mp14d_real.out run_*/mongod.log run_*/start.out run_*/test.out run_*/exit.rc ../vulkaninfo.out 2>/dev/null | tee SHA256SUMS
