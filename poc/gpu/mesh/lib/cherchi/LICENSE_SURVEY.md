# License survey: Cherchi et al., Interactive and Robust Mesh Booleans

Paths are relative to /Volumes/CrucialX9/Projects/mongo/storage/mesh/cherchi/src (a git clone; dependencies are vendored in-tree, no submodules/.gitmodules).

- Repo: https://github.com/gcherchi/InteractiveAndRobustMeshBooleans  [verified: cloned]
- Pinned commit: 7bd6c2697695f555f9a15d7e154e084d841316f0  [verified: git rev-parse HEAD]
- Link from arXiv 2205.14151 page: unverified (not fetched); repo is the README-cited reference implementation (README.md line 7).

## Licenses (from each component's own file)
| Component | Path | License as found | Verified |
|---|---|---|---|
| Booleans repo | LICENSE:1 "MIT License" (c) 2022 Cherchi, Pellacini, Attene, Livesu | MIT | verified |
| Arrangements | arrangements/LICENSE:1 "MIT License" | MIT | verified |
| Cinolib | arrangements/external/Cinolib/LICENSE:1 "MIT License" | MIT | verified |
| Shewchuk predicates (in Cinolib) | external/Cinolib/external/shewchuk_predicates/shewchuk.c header: "Placed in the public domain by Jonathan Richard Shewchuk" | public domain | verified |
| Eigen (in Cinolib) | external/Cinolib/external/eigen/COPYING.README:1 "primarily MPL2"; some files LGPL (COPYING.LGPL); `EIGEN_MPL2_ONLY` not set anywhere in the build (grep) | MPL-2.0 (with optional LGPL files) | verified |
| oneTBB | arrangements/external/oneTBB/LICENSE.txt (Apache License 2.0 text) | Apache-2.0 | verified |
| abseil-cpp | arrangements/external/abseil-cpp/LICENSE | Apache-2.0 | verified |
| parallel-hashmap | arrangements/external/parallel-hashmap/LICENSE | Apache-2.0 | verified |
| Yocto/Parallel | arrangements/external/yocto/yocto_parallel.h:2-9 LICENSE block, permission text (c) Fabio Pellacini | MIT-style | verified (full text only skimmed) |
| ImGui/GLFW/stb (Cinolib GUI) | glfw LICENSE.md: zlib-style ("provided 'as-is'") | zlib | verified (GUI only; CMake sets CINOLIB_USES_OPENGL_GLFW_IMGUI ON) |
| **Indirect_Predicates (Attene)** | arrangements/external/Indirect_Predicates/include/{indirect_predicates.h,implicit_point.h,numerics.h,memPool.h,hand_optimized_predicates.hpp} lines 12-16: "GNU Lesser General Public License ... either version 3 of the License, or (at your option) any later version" ; README.md:59-76 same; the LICENSE file in that dir is the LGPL **2.1** text (LICENSE:1-2) | **LGPL-3.0-or-later** in source headers (SPDX would be LGPL-3.0-or-later); LICENSE file text is LGPL-2.1, inconsistent | verified |

## Is Indirect_Predicates required? verified
- arrangements/code/{io_functions.h:41, fast_trimesh.h:44, triangle_soup.h:42} `#include <implicit_point.h>`; debug.h:46 and tree.h:41 `#include "indirect_predicates.h"`; CMakeLists.txt:31 puts it on the include path. The implicit-point / indirect-predicate machinery is the core of the exactness, so it cannot simply be dropped.
- Also requires `numerics.h` (Attene's NFG, same LGPL header) via those headers [unverified chain, header lists it].

## Verdict
Not fully permissive. Everything else is MIT/Apache/MPL/zlib/public domain, but Attene's Indirect_Predicates is LGPL-3.0-or-later (headers) and is mandatory. It is header-only (templates/inline), so it is compiled into the consumer; LGPL then imposes relinking/replacement obligations on whatever binary embeds it. Whether that is acceptable for a mongo-linked build is a legal call (unclear); a strict "no GPL/LGPL" requirement fails. Eigen files under LGPL could be avoided with -DEIGEN_MPL2_ONLY (verified flag exists in COPYING.README; whether Cinolib's Eigen use compiles under it is unverified).
No non-commercial clauses found in the licenses listed [verified for the above files only; abseil/oneTBB/imgui trees not exhaustively grepped].

## Technical facts
- Build system: CMake >= 3.21 (CMakeLists.txt:1) [verified]. C++20 (CMakeLists.txt:13) [verified]. Links cinolib and tbb; defines TBB_PARALLEL=1; uses -O2, -frounding-math, -mavx2 (x86 only; ENABLE_AVX2 True, CMakeLists.txt:9,132-134, so needs adjusting on Apple Silicon) [verified]. CINOLIB_USES_OPENGL_GLFW_IMGUI ON (needs OpenGL/GLFW) at line 35 [verified].
- API: code/booleans.h:104-112 take `std::vector<double>` coords and `std::vector<uint>` tris plus labels; output same. Doubles in, doubles out; no quantisation visible in the signatures [verified signatures; absence of internal quantisation unverified, results with implicit points are rounded to double on output].
- Input requirements: README.md:39 "must be manifold, watertight, self-intersections free, and well-oriented" [verified]. So open/non-manifold input is NOT supported by the boolean (the pipeline is documented as failing on it); a customArrangementPipeline exists (booleans.h:112) that may handle other cases [unverified].

## Build/run
Not attempted: the task asked to build only if all dependencies are permissive, and the LGPL component decides otherwise. Clone left at storage/mesh/cherchi/src.
