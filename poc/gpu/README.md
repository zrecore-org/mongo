# GPU proofs of concept (Vulkan, df64)

Standalone experiments that measure whether Vulkan compute (and double-float "df64" arithmetic) can speed up
selected MongoDB server workloads: segmented `$sum`, percentile/median, and key-prefix sorting. Nothing here
is wired into `src/`. Plan: `.claude/planning/vulkan-gpu.md`. Measurements, negative results and the go/no-go:
[RESULTS.md](RESULTS.md).

```bash
# Needs CMake, a C++20 compiler, glslc, a Vulkan loader + driver (MoltenVK on macOS, lavapipe on Linux).
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # defaults to ~/VulkanSDK/1.3.283.0/macOS
cmake --build build
mkdir -p out && ./run.sh poc3_select               # run.sh points the loader at MoltenVK unless VK_ICD_FILENAMES is set
```

`tools/with_gpu_lock.sh` is **optional**. It is a local-development convenience for one machine whose GPU is
shared with another project (`tools/with_gpu_lock.sh ./run.sh poc7_sort` runs a command while holding the
machine's GPU lock). Nothing needs it to build or run the POCs, CI does not use it except on the manual
self-hosted `apple-gpu` job, and it is not intended to be shipped or moved under `src/`. Skip it on any
machine whose GPU you do not share.

CI (`.github/workflows/gpu-poc.yml`) builds and runs the correctness checks on lavapipe. It has no GPU, so it
says nothing about speed.

## Credits and third-party software

Everything below is used as an unmodified tool, header or reference. The one exception is
`tools/with_gpu_lock.sh`, which is adapted from the `vulkan-gpu` fork of pg-strom (see the last table). Licenses are as published by each project; check them before
redistributing anything.

### The code under study

| Project | How it is used | License | Link |
|---|---|---|---|
| MongoDB server | The code being modelled. `poc2_sum.cpp` is a port of `DoubleDoubleSummation` (`src/mongo/util/summation.h`); `poc3b_percentile.cpp` and `poc6_continuous_cpu.cpp` mirror `percentile_algo_accurate.cpp` and `percentile_algo_continuous.cpp`. | SSPL-1.0 (`LICENSE-Community.txt`) | <https://github.com/mongodb/mongo> |

### Build and runtime dependencies

| Project | How it is used | License | Link |
|---|---|---|---|
| Vulkan-Headers (Khronos) | Vulkan API headers | Apache-2.0 | <https://github.com/KhronosGroup/Vulkan-Headers> |
| Vulkan-Loader (Khronos) | `libvulkan` loaded at runtime | Apache-2.0 | <https://github.com/KhronosGroup/Vulkan-Loader> |
| volk (Arseny Kapoulkine) | Vulkan function loader, built from `volk.c` (`VOLK_DIR`) | MIT | <https://github.com/zeux/volk> |
| MoltenVK (Khronos) | Vulkan on Metal; the driver for the macOS measurements | Apache-2.0 | <https://github.com/KhronosGroup/MoltenVK> |
| LunarG Vulkan SDK 1.3.283.0 | Supplies the above plus the tools below on macOS | various, mostly MIT/Apache-2.0 | <https://vulkan.lunarg.com/sdk/home> |
| shaderc (`glslc`, Google) | Compiles the GLSL compute shaders to SPIR-V | Apache-2.0 | <https://github.com/google/shaderc> |
| glslang (Khronos) | GLSL front end used by shaderc | BSD-3-Clause / Apache-2.0 / others, see upstream | <https://github.com/KhronosGroup/glslang> |
| SPIRV-Tools (Khronos) | `spirv-dis`/`spirv-as`/`spirv-val` in `tools/add_szinp.sh` | Apache-2.0 | <https://github.com/KhronosGroup/SPIRV-Tools> |
| SPIRV-Cross (Khronos) | `spirv-cross --msl`, used to inspect the Metal code MoltenVK generates (not part of the build) | Apache-2.0 | <https://github.com/KhronosGroup/SPIRV-Cross> |
| Mesa (lavapipe / llvmpipe) | CPU Vulkan driver used in CI as a second implementation | MIT | <https://gitlab.freedesktop.org/mesa/mesa>, <https://www.mesa3d.org> |
| Slang (`slangc` 2026.20, Shader Slang / Khronos) | Compiles `slang/rsel.slang` to SPIR-V for the GLSL-vs-Slang comparison only; not part of the build. Installed by the pg-strom fork's workflow, used here as an unmodified tool. | Apache-2.0 with LLVM exceptions | <https://github.com/shader-slang/slang> |
| Bazel and Bazelisk (Bazel authors / Google) | `bazel_slang/` standalone module for the Slang build-integration experiment only; not part of the CMake build | Apache-2.0 | <https://github.com/bazelbuild/bazel>, <https://github.com/bazelbuild/bazelisk> |
| NumPy | `verify_df64.py` | BSD-3-Clause | <https://numpy.org> |
| CMake | Build system | BSD-3-Clause | <https://cmake.org> |
| actions/checkout (GitHub) | CI | MIT | <https://github.com/actions/checkout> |

`verify_df64.py` also uses Python's standard `decimal` module as the exact oracle.

### Ideas, algorithms and prior work

The algorithms here were implemented for this project from the published descriptions; no code was copied.

| Work | Used for | Link |
|---|---|---|
| T. J. Dekker, "A floating-point technique for extending the available precision", Numerische Mathematik 18 (1971) | Error-free transformations behind double-double and df64 arithmetic (also Knuth's 2Sum, TAOCP vol. 2) | <https://doi.org/10.1007/BF01397083> |
| A. Thall, "Extended-Precision Floating-Point Numbers for GPU Computation" (2007) | The df64 (float-float) approach on GPUs | <https://andrewthall.org/papers/df64_qf128.pdf> |
| J. R. Shewchuk, "Adaptive Precision Floating-Point Arithmetic and Fast Robust Geometric Predicates" (1997) | Exactly rounded summation with non-overlapping partials, used as the exact reference in the ill-conditioned `$sum` test (the same algorithm as Python's `math.fsum`) | <https://people.eecs.berkeley.edu/~jrs/papers/robustr.pdf> |
| Python `math.fsum` | Reference for the partials algorithm above (CPython, PSF license) | <https://docs.python.org/3/library/math.html> |
| Integer-emulated IEEE binary64 (exact `float8` in 64-bit integers) | `shaders/f64soft.inc` (POC 12). Approach taken from the description of the pg-strom fork's `kernels_vm/f64_soft.inc` in its plan (`.claude/hw-acceleration.md`); that source file was not read, and the implementation here was written from the IEEE 754 rules and shares no code with it. The fork credits the ArrowMetal write-up for the idea; I have not read that article. | n/a | <https://github.com/zrecore-org/pg-strom>, <https://theaivibe.org/blog/metal-has-no-double-float64-on-the-apple-silicon-gpu> |
| LSD counting radix sort / radix select | `poc3_select` (MSD byte-wise radix select) and `poc7_sort` (4-bit LSD sort with per-block histograms and a global scan). These are textbook techniques; no specific implementation was followed. | n/a |

### Design inspiration from pg-strom

The lock tool described here is an optional local-development helper only (see the top of this file); it is
not part of the proofs of concept and will not ship.

| Project | What was borrowed | License | Link |
|---|---|---|---|
| `vulkan-gpu` branch of pg-strom (`zrecore-org/pg-strom`, a hard fork of KaiGai Kohei / HeteroDB's PG-Strom; the author of this repo's own fork) | The shared-GPU lock protocol in `tools/with_gpu_lock.sh` (queue and heartbeat files, same paths) is a shortened script adapted from that fork's `test/gpuserv/locked_test.sh`, which exists only in the fork, not in upstream PG-Strom (it follows the same protocol and shares some lines with it), so the two projects never run on the GPU at the same time. `.github/workflows/gpu-poc.yml` is modelled on that fork's `.github/workflows/vulkan-check.yml` (also fork-only) (lavapipe on hosted runners, self-hosted Apple Silicon for real GPU tests). The fork is based on upstream PG-Strom (KaiGai Kohei, HeteroDB, PostgreSQL License, <https://github.com/heterodb/pg-strom>), which is credited as the original project. | PostgreSQL License | <https://github.com/zrecore-org/pg-strom> |

## License of this directory

This directory is part of the MongoDB source tree and, unless a file says otherwise, is covered by the
repository's license, the Server Side Public License v1 (`LICENSE-Community.txt` at the repository root). The
files here do not yet carry per-file SPDX headers; add them before any of this moves under `src/`.

Parts of this work were written with the assistance of Claude (Anthropic), as recorded in the commit trailers.
