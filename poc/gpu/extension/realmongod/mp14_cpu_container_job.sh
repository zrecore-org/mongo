#!/bin/bash
# MP14d, real fork mongod, CPU-path and software-device cases (no GPU turn). ubuntu:24.04 x86_64, --ulimit core=0; /m = mongod dir (ro), /in = FileTransfers\mongo (ro), /w = volume g5-w.
# Cases: cpu_default; gpu_lavapipe (gpu: true on lavapipe = software device, stage option gpuMinPoints 1: the device path must NOT be used, results identical, counters 0).
set -u
ulimit -c 0
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq g++ gcc python3-venv python3-pip libcurl4 libssl3 liblzma5 ca-certificates libvulkan1 mesa-vulkan-drivers >/dev/null 2>&1
rm -rf /w/mp14d && mkdir -p /w/mp14d && cd /w/mp14d && tar -xzf /in/mp14f_bundle.tgz && tar -xzf /in/mp14f_run.tgz
sha256sum /in/mp14f_bundle.tgz /in/mp14f_run.tgz /m/mongod | tee inputs.sha256
(cd g5_bundle && ./build_linux.sh 2>&1 | tail -2)
python3 -m venv /w/mp14d/mp14d_pkg/venv && /w/mp14d/mp14d_pkg/venv/bin/pip -q install pymongo 2>&1 | tail -1
cd mp14d_pkg && CASES="cpu_default gpu_lavapipe" ./run_mp14d_real.sh /m/mongod /w/mp14d/g5_bundle/out/libmeshvol_ext.so /w/mp14d/mp14d_pkg/real60 2>&1 | tee run_mp14d_real.out
sha256sum run_mp14d_real.out run_*/mongod.log run_*/start.out run_*/test.out run_*/exit.rc 2>/dev/null | tee SHA256SUMS
