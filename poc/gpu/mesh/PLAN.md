# 3D mesh workstream: library evaluation and GPU acceleration with CPU fallback

Approved plan (2026-11-14). The summary and decisions are in `.claude/planning/vulkan-gpu.md` ("3D mesh workstream"); this file is the
working plan for `poc/gpu/mesh/`. Standalone proofs of concept only: nothing here changes `src/`, no third-party source is committed
(libraries and meshes are fetched, pinned and checksummed into `build/`), and negative results are recorded plainly.

## Questions
1. Which permissively licensed libraries can provide SFCGAL-class operations (3D first, then 2D), and how much of SFCGAL's function list
   is covered, partly covered, or needs our own code?
2. Can a GPU speed up predicates, distance, point-in-solid, volume and candidate culling while giving exactly the CPU's answer?
3. Can the feature ship as a MongoDB extension (`src/mongo/db/extension/`) instead of core-server vendoring?

## Exactness contract (tiers)
1. GPU on == CPU only, same build. Hard.
2. Same binary and input, same bytes at any thread count (library booleans: serial build, `-ffp-contract=off`, pinned version). Hard; gates G1.
3. Library boolean output identical across x86_64 and arm64. Measured and reported, not required; if it fails, booleans are "same shape within a stated tolerance".
4. Our own predicates and measures (intersects, point-in-solid, distance, volume by exact accumulation) identical everywhere. Hard.

Vocabulary: **wrong** = silently incorrect result (must be 0); **refused** = clean error (counted, acceptable); crash or hang = failure.

## Waves and gates (stop after any wave)

| Wave | POCs | Ends at |
|---|---|---|
| 0 foundations (no GPU) | MP01 corpus + harness, MP02 oracles + planted mutants, MP06 parity matrix + license audit | Wave 0 done criteria |
| 1 | MP03 Manifold, MP04 Geogram, MP05 Embree + CPU BVH, MP09 GPU exact-sign filter, MP14a extension spike | **G1** boolean engine, **G2** GPU filter |
| 2 | MP10 LBVH + pair emission, MP07 gap-fill spikes, MP08a 2D permissive coverage, MP08b hard 2D/3D ops | **G3** parity |
| 3 | MP11 hybrid narrow phase, MP12 point-in-solid + exact measures, MP13 fallback + crossover, MP14 integration spikes | **G4** GPU value, **G5** packaging |

Optional: MP15 winding-number prefilter. MP16 (patched-library GPU boolean) is **not planned**.

## Wave 0 (started)

### MP01 corpus + harness (`corpus/`, `harness/`)
Question: can one reproducible mesh corpus feed every library, GPU kernel and oracle?
Steps: a compact binary mesh format (float64 xyz, uint32 triangles) with reader/writer; generators for sphere, torus, icosphere, grid; adversarial
pair generators with an expected outcome per case (coplanar, touching at vertex/edge/face, shared edge/vertex, slivers, nested, identical, A minus A,
open/non-manifold, self-intersecting, 1 ulp / 2^-20 / 2^-40 perturbations, huge offsets); many-small and few-large sets; a pinned fetch script for
Thingi10K (fetched only, never committed; per-model licenses vary, see `CORPUS.md`).
Pass bar: the corpus is byte-reproducible from the scripts and every case carries a tag.

### MP02 oracles and comparison (`oracle/`, `harness/`)
Question: what do we compare library and GPU answers against, without GPL code?
Steps: Shewchuk `predicates.c` (public domain, fetched, pinned) for orient2d/orient3d/incircle/insphere; exact rational recompute with
Boost.Multiprecision `cpp_rational` (already vendored under `src/third_party/boost/libs/multiprecision`) for orientation, volume, area on small cases; canonical equivalence for
booleans (exact volume, area, Euler characteristic, components, watertightness, sampled point-in-solid), never bit-for-bit across libraries;
planted mutants in the pattern of `poc/gpu/poc12_f64soft.cpp`; a Geogram cross-check once MP04 exists. CGAL is not used.
Pass bar: every planted mutant is caught; Shewchuk and the rational recompute agree on 1e7 random plus adversarial predicate inputs.

### MP06 parity matrix and license audit (`PARITY.md`, `parity.csv`, `LICENSES.md`)
Question: how much of SFCGAL v2.3.0 do the permissive options cover?
Steps: every SFCGAL function (types, predicates and measures, constructive, skeleton/alpha/visibility, transforms) mapped to Manifold, Geogram, Embree,
Clipper2, Boost.Geometry, earcut, CDT, the in-tree S2, write-ourselves (with a size estimate) or infeasible; columns for "needs exactness" and "GPU-able";
every license re-verified at a pinned tag from the project's own LICENSE file (agent claims are not evidence). Done 2026-11-14; findings: Geogram bundles restricted components (see MP04), CDT is MPL-2.0 (weak copyleft, not counted as permissive), the in-tree S2 is spherical only, Boost.Geometry is not vendored.
Pass bar: every SFCGAL function has a row; coverage percentages for 3D and 2D separately, with and without write-ourselves items.

Wave 0 is done when: the corpus is byte-reproducible, every mutant is caught, the predicate oracles agree, and every SFCGAL function has a parity row.

## Wave 1 (after Wave 0)
- **MP03 Manifold** (pinned tag; serial build is the product path, `MANIFOLD_PAR=OFF`; a TBB build only to measure what parallelism would add, since oneTBB's third-party notices flag a GPL-tagged entry for legal review): API fit (`MeshGL`), op coverage, robustness over the adversarial corpus, speed on both workload shapes, tier-2 determinism
  (1/4/8 threads, repeats, `-ffp-contract=off`), tier-3 measurement (x86_64 vs aarch64 on the lavapipe CI runners). Read PRs #1606, #1623, #1594 to see what its CI determinism check covers; test booleans on fixed coordinates separately from trig-based constructors.
- **MP04 Geogram** (exact expansion arithmetic booleans): same method plus conversion cost into its own `Mesh`, closed-mesh requirement, cross-platform determinism. **Permissive build only**: configure with `GEOGRAM_WITH_TETGEN=OFF`, `GEOGRAM_WITH_TRIANGLE=OFF`, `GEOGRAM_WITH_HLBFGS=OFF` (all ON by default; TetGen is AGPL/academic, Triangle is commercial-by-arrangement, HLBFGS is non-commercial; see `LICENSES.md`). The restricted sources arrive in the fetched archive under `build/` but are never compiled; after building, `nm`/the link map of every artifact must show no tetgen, triangle or HLBFGS symbols, and that check is part of the POC's pass bar. Expect reduced functionality (no tetrahedralization, no Triangle-based Delaunay, `remesh_smooth` falls back to Lloyd iterations).
- **MP05 Embree + own deterministic CPU BVH** (`cpu/bvh_ref.cpp`): the multi-core CPU baseline the GPU must beat, and the CPU twin for MP10.
- **MP09 GPU exact-sign filters** (`gpu/orient_filter.comp`, `gpu/orient_exact_int.comp`): orient2d/orient3d as a df64/fp32 filter with a conservative error bound including the binary64-to-df64 splitting loss
  (`precise`, no FMA contraction) returning +, -, uncertain; and an exact integer path extending `shaders/f64soft.inc`. Compared with Shewchuk.
- **MP14a extension spike** (`extension/`): a minimal extension `.so` built against the public C header only (`src/mongo/db/extension/public/api.h`, `api_contract.h`; no `mongo/base`), one transform stage that batches documents with
  BinData meshes and returns a measure per document; fake host harness; optionally a real `mongod --loadExtensions`. Questions: symbols/allocator (`bazel/transitions.bzl`), Vulkan loader `dlopen` inside an extension, batching, deadlines/`killOp`,
  error handling without exceptions, C++ over the raw C ABI vs Rust bindings.

**G1 boolean engine:** Manifold passes with 0 wrong on valid manifold input, clean refusal of non-manifold input, and tier-2 determinism (tier 3 reported only). Geogram is promoted only if Manifold fails tier 2 or shows wrong results.
**G2 GPU filter:** 0 wrong definite signs on at least 1e9 random plus all adversarial inputs; uncertain rate reported (suggested bars: under 1% random, under 10% on Thingi). If not met: fix the bound, switch to the integer path, or drop the GPU predicate stage.

## Waves 2 and 3 (details at their start; gates G3, G4, G5)
MP10 deterministic LBVH and pair emission (32-bit atomics only, count-then-write, canonical sort; the gating bar is the final answer with GPU on == CPU only); MP07 gap-fill spikes (extrude, tessellate, Minkowski, exact distance, measures);
MP08a 2D permissive coverage (Clipper2, Boost.Geometry, earcut, the in-tree S2; CDT only as a comparison, it is MPL-2.0); read the 31 Boost.Geometry parity cells that MP06 left unverified; MP08b hard 2D/3D ops (straight skeleton, medial axis, alpha shapes, visibility, polygon repair, 3D alpha wrap; most are CGAL-only in SFCGAL);
MP11 GPU pair list + filtered triangle-triangle test + CPU exact tail; MP12 point-in-solid and volume by exact accumulation (extends the exact add of `f64soft.inc` with an exact multiply); MP13 fault injection, shadow check, crossover sweep (baseline = best multi-core CPU);
MP14 BSON representation, 3D bounding-box index simulation (100% recall), runtime lifecycle, API sketch (extension stages first, core `$mesh*` expressions as the fallback).
**G3** parity (3D and 2D separately; suggested bars 70% without write-ourselves, 90% with). **G4** stop at a CPU-only product if the GPU is not at least 2x faster than the best multi-core CPU at plausible sizes; never ship the GPU if the shadow check ever disagrees.
**G5** packaging: extension viable only if it loads, batches, honours deadlines, survives a planted fault, and its language choice is workable; otherwise core vendoring or a separate service.

## Resource rules
- M1 only through `tools/with_gpu_lock.sh`. Lavapipe (x86_64 and aarch64) runs the suite in CI at no cost. T4 only at gates, through `tools/t4_ci_turn.sh`, after the lavapipe job passes; `[skip gpu]` on commits that do not need it.
- Sub-agents for CPU-only independent work (parity and license work, library build and API-fit steps, survey halves, corpus scripts). Shader work stays in the main thread. Advisor before each gate and before each wave starts.
- **Large files go to `./storage` (external volume, ignored by git), never the internal disk** (it has about 12 GiB free): Thingi10K, big generated sets, downloaded libraries and their build trees when large, and Bazel's caches (`. poc/gpu/tools/storage_env.sh`, then `bz build ...`). Small generated files stay in `poc/gpu/build/`.
- No push without asking; commit locally to `vulkan-gpu`.

## Out of scope
Anything under `src/`; vendoring a library into the repo; CGAL, SFCGAL and OCCT code or builds; a patched-library GPU boolean (MP16); GPU work for 2D in the first pass; production hardening.
