#!/bin/bash
# MP14a: build everything and run every bar. Logs go to storage/extension_build/*.log. The Vulkan probe runs under the shared GPU lock.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../../.." && pwd)"
OUT="$R/storage/extension_build"
"$HERE/build.sh"
cd "$OUT"
./fakehost all . | tee all.log
for m in mut1 mut2 mut3; do ./fakehost corpus ./libmeshvol_$m.dylib | tee corpus_$m.log; done
for lib in libmeshvol_novis libmeshvol_ext; do echo "== $lib"; nm -gU $lib.dylib; ./fakehost symbol ./$lib.dylib; DYLD_FORCE_FLAT_NAMESPACE=1 ./fakehost symbol ./$lib.dylib; done | tee symbols.log
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$HOME/VulkanSDK/1.3.283.0/macOS/share/vulkan/icd.d/MoltenVK_icd.json}"
"$R/poc/gpu/tools/with_gpu_lock.sh" ./fakehost vulkan ./libmeshvol_ext.dylib | tee vulkan.log
