#!/bin/bash
# MP14d step 2 (device path, M1 / MoltenVK or any Vulkan host with a hardware device), fake host. Cases: ok, small, cpu knob, planted failure, planted wrong verdicts; 4 device-path mutants.
# Runs under the shared GPU lock. Log: storage/extension_build/mp14d_gpu.log.
HERE="$(cd "$(dirname "$0")" && pwd)"; R="$(cd "$HERE/../../.." && pwd)"; OUT="$R/storage/extension_build"; cd "$OUT"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$HOME/VulkanSDK/1.3.283.0/macOS/share/vulkan/icd.d/MoltenVK_icd.json}"
LOCK="$R/poc/gpu/tools/with_gpu_lock.sh"; FAILS=0
chk() { if [ "$2" = ok ]; then echo "PASS $1"; else echo "FAIL $1"; FAILS=$((FAILS+1)); fi; }
has() { echo "$1" | grep -q -- "$2" && echo ok || echo no; }
all() { local r=ok a; for a in "$@"; do [ "$a" = ok ] || r=no; done; echo $r; }
go() { "$LOCK" ./fakehost containsgpu "./$1.dylib" "$2" ${3:-} 2>&1; }
cnt() { echo "$1" | grep "^COUNTERS" | sed -n "$2p" | grep -o "$3=[0-9]*" | cut -d= -f2; }
o=$(go libmeshvol_ext ok);    echo "$o" | grep -E "CASE|COUNTERS|SECOND" | cut -c1-260
chk "ok: device path used (1 batch, then 2), verdicts equal the CPU core on both runs, UNCERTAIN resolved exactly" "$(all "$(has "$o" 'CASE ok.*equal CPU core: YES')" "$(has "$o" 'SECOND RUN verdicts equal CPU core: YES')" "$([ "$(cnt "$o" 1 gpuBatches)" = 1 ] && [ "$(cnt "$o" 2 gpuBatches)" = 2 ] && [ "$(cnt "$o" 1 gpuUncertain)" -gt 0 ] && echo ok)" "$(has "$o" 'engine="gpu"')")"
o=$(go libmeshvol_ext small); echo "$o" | grep -E "CASE|COUNTERS" | cut -c1-200
chk "small (below the threshold): CPU path, 0 device batches, verdicts equal" "$(all "$(has "$o" 'equal CPU core: YES')" "$([ "$(cnt "$o" 1 gpuBatches)" = 0 ] && echo ok)")"
o=$(go libmeshvol_ext cpu);   chk "engine knob cpu: 0 device batches, verdicts equal" "$(all "$(has "$o" 'equal CPU core: YES')" "$([ "$(cnt "$o" 1 gpuBatches)" = 0 ] && echo ok)")"
o=$(go libmeshvol_ext fail);  echo "$o" | grep -E "CASE|COUNTERS|SECOND" | cut -c1-260
chk "planted device failure: falls back, verdicts equal (both runs), breaker open (1 fallback, no device batch, engine cpu)" "$(all "$(has "$o" 'CASE fail.*equal CPU core: YES')" "$(has "$o" 'SECOND RUN verdicts equal CPU core: YES')" "$([ "$(cnt "$o" 2 gpuFallbacks)" = 1 ] && [ "$(cnt "$o" 2 gpuBatches)" = 0 ] && echo ok)" "$(has "$o" 'engine="cpu"')")"
o=$(go libmeshvol_ext wrong); echo "$o" | grep -E "CASE|COUNTERS|SECOND" | cut -c1-260
chk "planted wrong verdicts: shadow check catches, verdicts equal (both runs), device disabled" "$(all "$(has "$o" 'CASE wrong.*equal CPU core: YES')" "$(has "$o" 'SECOND RUN verdicts equal CPU core: YES')" "$([ "$(cnt "$o" 2 gpuShadowMismatch)" = 1 ] && [ "$(cnt "$o" 2 gpuBatches)" = 0 ] && echo ok)")"
o=$("$LOCK" ./fakehost containsgpu ./libmeshvol_ext.dylib concurrent 20000 2>&1); echo "$o" | grep -E "CASE|COUNTERS" | cut -c1-220
chk "concurrency: 6 threads x 6 aggregations on one device context, 0 mismatching runs, 36 device batches" "$(all "$(has "$o" 'verdict-mismatching runs=0')" "$([ "$(cnt "$o" 1 gpuBatches)" = 36 ] && echo ok)")"
r50=$(MP14D_REPEAT=50 "$LOCK" ./fakehost containsgpu ./libmeshvol_ext.dylib repeat 20000 2>&1 | grep -o "rssGrowth=[0-9]*" | cut -d= -f2)
r450=$(MP14D_REPEAT=450 "$LOCK" ./fakehost containsgpu ./libmeshvol_ext.dylib repeat 20000 2>&1 | grep -o "rssGrowth=[0-9]*" | cut -d= -f2)
echo "rss growth after 50 runs: $r50, after 450 runs: $r450"
chk "no leak: RSS growth between 50 and 450 device runs under 8 MB (each run allocates tens of MB of device buffers)" "$([ $((r450 - r50)) -lt 8000000 ] && echo ok)"
echo "== mutants (each designated case must show the defect)"
o=$(go libmeshvol_gpumut1 wrong); chk "mutant1 shadow check removed: caught (wrong verdicts reach the result)" "$(has "$o" 'CASE wrong.*equal CPU core: NO')"
o=$(go libmeshvol_gpumut2 fail);  chk "mutant2 breaker never opens: caught (second run falls back again)" "$([ "$(cnt "$o" 2 gpuFallbacks)" = 2 ] && echo ok || echo no)"
o=$(go libmeshvol_gpumut3 small); chk "mutant3 threshold ignored: caught (device used for 1000 points)" "$([ "$(cnt "$o" 1 gpuBatches)" -gt 0 ] && echo ok || echo no)"
o=$(go libmeshvol_gpumut4 ok);    chk "mutant4 UNCERTAIN trusted as OUT: caught (boundary verdicts wrong)" "$(has "$o" 'CASE ok.*equal CPU core: NO')"
echo "FAILS $FAILS"
