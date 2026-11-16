#!/bin/bash
# MP14c bars (plan + amendment 1). Needs: ./build.sh ext noprobe cfgmut1..cfgmut7 host. Log: storage/extension_build/mp14c.log.
# Inputs to initialize are the text yaml-cpp's YAML::Dump produces for the server's extensionOptions map (docs/logs/mp14c_yaml_dump_probe.out).
# Vulkan is touched only on gpu: true runs with the probe-enabled libs; those run under the shared GPU lock wrapper (tools/with_gpu_lock.sh).
HERE="$(cd "$(dirname "$0")" && pwd)"; R="$(cd "$HERE/../../.." && pwd)"; OUT="$R/storage/extension_build"; cd "$OUT"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$HOME/VulkanSDK/1.3.283.0/macOS/share/vulkan/icd.d/MoltenVK_icd.json}"
LOCK="$R/poc/gpu/tools/with_gpu_lock.sh"
FAILS=0
chk() { if [ "$2" = ok ]; then echo "PASS $1"; else echo "FAIL $1"; FAILS=$((FAILS+1)); fi; }
has() { echo "$1" | grep -q -- "$2" && echo ok || echo no; }
all() { local r=ok a; for a in "$@"; do [ "$a" = ok ] || r=no; done; echo $r; }
fh() { ./fakehost options "./$1.dylib" "$2" 2>&1; }                          # no Vulkan expected (default off or probe-less lib)
fhv() { "$LOCK" ./fakehost options "./$1.dylib" "$2" 2>&1; }                 # may touch Vulkan: under the GPU lock
corpus_ok() { has "$1" "CORPUS total=[0-9]* wrong=0 exactMismatch=0 roundingWrong=0"; }
show() { echo "$1" | grep -E "OPTIONS-LOAD|VULKAN-IMAGES|PROBE"; }

echo "== bar 1: default off (text as the real YAML::Dump gives it)"
for y in "" "{}" "~" "gpu: false" $'gpu: false\ngpuRequired: false' "gpuRequired: false"; do
  o=$(fh libmeshvol_ext "$y"); show "$o"
  chk "bar1 [$(echo "$y"|tr '\n' '|')] loads, requested=false, vulkanProbes=0, no Vulkan image loaded, engine=cpu" "$(all "$(has "$o" 'OPTIONS-LOAD ok=1')" "$(has "$o" 'gpuRequested=false')" "$(has "$o" 'vulkanProbes=0')" "$(has "$o" 'VULKAN-IMAGES 0')" "$(has "$o" 'engine="cpu"')")"
done
chk "bar1 corpus exact (default off)" "$(corpus_ok "$(fh libmeshvol_ext "")")"

echo "== bar 2: gpu true, working Vulkan (hardware device on this host)"
o=$(fhv libmeshvol_ext "gpu: true"); show "$o"
chk "bar2 gpuReady=true, one probe, engine=cpu, hardware reason" "$(all "$(has "$o" 'gpuReady=true')" "$(has "$o" 'vulkanProbes=1')" "$(has "$o" 'engine="cpu"')" "$(has "$o" 'reason="hardware device present"')")"
chk "bar2 corpus exact, same oracle as default off (gate does not disturb results; not evidence of GPU use)" "$(corpus_ok "$o")"
o=$(fhv libmeshvol_ext $'gpu: true\ngpuRequired: true'); show "$o"
chk "bar2 gpuRequired true with a ready GPU loads" "$(all "$(has "$o" 'OPTIONS-LOAD ok=1')" "$(has "$o" 'gpuReady=true')")"
o=$(fhv libmeshvol_ext "{gpu: true, gpuRequired: false}"); chk "bar2 flow-style map loads, ready" "$(all "$(has "$o" 'OPTIONS-LOAD ok=1')" "$(has "$o" 'gpuReady=true')")"

echo "== bar 3: gpu true, no usable GPU"
o=$(fh libmeshvol_noprobe "gpu: true"); show "$o"
chk "bar3 loads, gpuReady=false, reason reported, no path in reason" "$(all "$(has "$o" 'OPTIONS-LOAD ok=1')" "$(has "$o" 'gpuReady=false')" "$(has "$o" 'reason="built without vulkan support"')" "$([ "$(echo "$o" | grep -c '/')" = 0 ] && echo ok || echo no)")"
chk "bar3 corpus exact" "$(corpus_ok "$o")"
o=$(fh libmeshvol_noprobe $'gpu: true\ngpuRequired: true'); echo "$o" | grep OPTIONS-LOAD
chk "bar3 gpuRequired + no GPU: initialize fails with the reason" "$(all "$(has "$o" 'OPTIONS-LOAD ok=0')" "$(has "$o" 'not ready: built without vulkan support')")"
chk "bar3 no Vulkan image after the failed load attempt" "$(has "$o" 'VULKAN-IMAGES 0')"
echo "   (loader missing / zero devices / instance failure / software-only on real stacks are exercised in the Linux container run, see RESULTS)"

echo "== bar 7: planted hang in the probe is bounded"
t0=$(date +%s)
o=$(MESHVOL_PROBE_LIMIT_MS=500 MESHVOL_PLANT_PROBE_HANG_MS=4000 fhv libmeshvol_ext "gpu: true"); show "$o"; t1=$(date +%s)
chk "bar7 timeout: loads, gpuReady=false, reason timed out" "$(all "$(has "$o" 'OPTIONS-LOAD ok=1')" "$(has "$o" 'gpuReady=false')" "$(has "$o" 'timed out')")"
o=$(MESHVOL_PROBE_LIMIT_MS=500 MESHVOL_PLANT_PROBE_HANG_MS=4000 fhv libmeshvol_ext $'gpu: true\ngpuRequired: true'); echo "$o" | grep OPTIONS-LOAD
chk "bar7 timeout with gpuRequired: initialize fails" "$(has "$o" 'OPTIONS-LOAD ok=0')"
echo "   (a probe that crashes in a driver is not survivable in-process; see plan)"

echo "== bar 4: strictness (inputs as YAML::Dump prints them)"
for y in "frobnicate: true" "gpu: maybe" "gpu: 1" "gpu: yes" "gpu: True" "gpu: ~" "GPU: true" $'gpu: true\ngpu: false' $'gpu: true\nnested:\n  a: 1' "[1, 2]" "gpu" "gpuRequired: true" "{gpu: maybe}" "{gpu: true, gpu: false}" "{gpu: {a: 1}}" $'gpu: true\n\tx: 1'; do
  o=$(fh libmeshvol_noprobe "$y"); echo "$o" | grep OPTIONS-LOAD
  chk "bar4 rejects [$(echo "$y"|tr '\n' '|')]" "$(has "$o" 'OPTIONS-LOAD ok=0')"
done
for y in "" "{}" "~" "gpu: false" "gpuRequired: false" $'gpu: false\ngpuRequired: false' "{gpu: false}" "gpu: true"; do
  o=$(fh libmeshvol_noprobe "$y")
  chk "bar4 accepts [$(echo "$y"|tr '\n' '|')]" "$(has "$o" 'OPTIONS-LOAD ok=1')"
done

echo "== bar 5: mutants, one designated check each (the same check must pass on the good lib above)"
m1=$(fhv libmeshvol_cfgmut1 "gpu: true");                      chk "mutant1 option ignored: caught (probes=1 and ready expected)" "$([ "$(all "$(has "$m1" 'vulkanProbes=1')" "$(has "$m1" 'gpuReady=true')")" = ok ] && echo no || echo ok)"
m2=$(fh libmeshvol_cfgmut2 "");                                chk "mutant2 default flipped: caught (probes=0 expected)" "$([ "$(has "$m2" 'vulkanProbes=0')" = ok ] && echo no || echo ok)"
m3=$(fh libmeshvol_cfgmut3 $'gpu: true\ngpuRequired: true');    chk "mutant3 gpuRequired ignored: caught (ok=0 expected)" "$([ "$(has "$m3" 'OPTIONS-LOAD ok=0')" = ok ] && echo no || echo ok)"
m4=$(fh libmeshvol_cfgmut4 "frobnicate: true");                chk "mutant4 unknown key accepted: caught (ok=0 expected)" "$([ "$(has "$m4" 'OPTIONS-LOAD ok=0')" = ok ] && echo no || echo ok)"
m5=$(fh libmeshvol_cfgmut5 $'gpu: false\ngpu: false');         chk "mutant5 duplicate accepted: caught (ok=0 expected)" "$([ "$(has "$m5" 'OPTIONS-LOAD ok=0')" = ok ] && echo no || echo ok)"
m6=$(MESHVOL_PROBE_LIMIT_MS=500 MESHVOL_PLANT_PROBE_HANG_MS=4000 fhv libmeshvol_cfgmut6 "gpu: true"); chk "mutant6 probe error swallowed: caught (ready=false expected)" "$([ "$(has "$m6" 'gpuReady=false')" = ok ] && echo no || echo ok)"
m7=$(fh libmeshvol_cfgmut7 "");                                chk "mutant7 loader opened at static init: caught by the independent image check (counter stays 0)" "$(all "$(has "$m7" 'vulkanProbes=0')" "$([ "$(has "$m7" 'VULKAN-IMAGES 0')" = ok ] && echo no || echo ok)")"
echo "FAILS $FAILS"
