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

## Pre-registered pass bars for the library evaluations (MP03, MP04)
Fixed on 2026-11-14, **before any library output was scored**. Scoring tool: `mesh_compare score` (`oracle/compare.cpp`, with `harness/pip.h` and `harness/topology.h`), self-tested by `mesh_compare selftest`
(accepts known-good results, fails seven kinds of wrong result, catches two planted bugs in the exact point-in-solid test). Every library is run on the 31 corpus cases x 3 operations (union, intersection, difference), 60 s timeout each. A change to any bar after the first scored run is recorded as a change, with the reason, and everything is re-scored.

- **Verdict per case and operation:** PASS, FAIL, REFUSED (library exit code 3: it declined the input), ACCEPTED-UNVERIFIED (the inputs are not valid solids and the library returned a closed result), TIMEOUT, CRASH.
- **Valid inputs (tag `valid`):** 0 FAIL, 0 TIMEOUT, 0 CRASH. A PASS needs all of: the result is empty or a closed, edge-manifold, non-inverted mesh with no duplicated triangle; for box pairs the exact volume equals the manifest's exact expectation or differs by at most 1e-9 relative to it (expected zero: at most 2^-52 of the larger input volume), classified exact / close / wrong, and "wrong" fails; every cell of the box-pair arrangement (per-axis midpoints of the sorted box coordinates plus an outside cell) and 5,000 exact dyadic sample points for every pair classify the same way in the result as in the boolean of A and B (points on a surface are skipped and counted). Refusals of valid inputs are counted and listed with the library's reason; more than 3 of the 24 valid cases x 3 operations refused is reported as a robustness concern for the user to weigh, not an automatic fail. Zero-area triangles in a result are reported, not failures.
- **Malformed inputs (tags `open`, `non-manifold-edge`, `self-intersecting`, `unwelded-vertices`, `zero-area-triangle`, `inverted`, `duplicate-triangle`):** acceptable outcomes are REFUSED or ACCEPTED-UNVERIFIED with a closed manifold result; a non-closed result, a crash or a timeout is a FAIL.
- **Curved pairs** have no exact expected volume, so the point tests arbitrate; where both libraries succeed their exact volumes are also compared (relative 1e-9, informational).
- **Tier 2 (determinism):** two runs of the same serial binary give byte-identical output (sha256) on every case: hard, any difference fails tier 2. Builds with and without `-ffp-contract=off` give identical bytes: reported; any difference means the product build must pin the flag.
- **G1 rule:** Manifold passes G1 if it meets the valid-input and malformed-input bars and tier 2. Geogram (permissive build, after the `nm` check shows no TetGen, Triangle or HLBFGS symbols) becomes the primary only if Manifold fails G1; if both pass, the choice uses the speed table, refusal counts and the user's view of Geogram's reduced feature set.
- **What the tool cannot see (stated now, not discovered later):** the volume check is exact only for box pairs; curved pairs are sampled, so a feature thinner than about 1/5000 of the sampled volume can be missed; arrangement cells whose midpoint is not a representable double (the one-ulp gap case) are checked by the exact volume only; a vertex moved by 2^-45 stays inside the 1e-9 volume tolerance; the point tests use closed, outward or inward oriented meshes and are meaningless for the malformed inputs, which is why those are judged by structure only.

### Changes to the pre-registered bars (each recorded with its reason; results re-scored)
1. **2026-11-14, after the first Manifold scoring run: manifoldness of a result is judged as returned (by vertex index), not after welding coincident vertices.** Reason: my first implementation welded vertices with equal coordinates before checking edges, so the correct union of two boxes
   that touch along an edge (geometrically a non-manifold solid) was reported as "not a closed manifold" although Manifold returned a valid manifold (16 vertices, every edge shared by exactly two faces, the touching edge's endpoints duplicated). That was a defect in my bar, not in the library.
   Effect on Manifold: 4 verdicts changed (`box_edge_touch` union FAIL to PASS; the three `bad_nonmanifold_edge` results FAIL to ACCEPTED-UNVERIFIED); the other 89 verdicts are unchanged. The first-run results are kept in `lib/manifold/scoring_results_first_scoring.tsv`. The geometric (welded) property is still reported as `geomClosed`.
   Also fixed before the first run was scored: the code only applied the "non-closed result is a FAIL" rule to valid inputs, while the registered text applies it to every input; the code was changed to match the text.
2. **2026-11-14, clarification of the Geogram license check (MP04):** the registered wording ("no tetgen, triangle or HLBFGS symbols") cannot be met literally, because Geogram's own BSD files carry those names (`delaunay_tetgen.cpp`, `delaunay_triangle.cpp`, `triangle_intersection.cpp`; the first compiles to one marker symbol `_dummy_delaunay_tetgen_compiled`).
   The check that matters is: (a) no compiled file under `third_party/{tetgen,triangle,HLBFGS}`, from `compile_commands.json`; (b) zero symbols from the restricted libraries' own APIs (`tetgenio`, `tetgenbehavior`, `triangulateio`, `INIT_HLBFGS`, `HLBFGS_*`) in every artifact. Both pass (checked by me, not only by the sub-agent).
3. **2026-11-14, after the Geogram scoring run (advisor's review): structure check made symmetric.** Amendment 1 judged manifoldness by vertex index only, which accepted Manifold's representation of an edge-touching union (endpoints duplicated) but rejected Geogram's (edge shared by four faces), although the exact union of two solids that touch along an edge or at a corner is geometrically non-manifold and both representations are valid.
   A result now passes the structure check if it is a closed manifold as returned OR it has no boundary edge after identifying coincident vertices (so it may have an edge or vertex shared by more than two faces); open surfaces still fail; the volume and point tests still apply.
   Effect: Manifold unchanged (57 PASS, 15 FAIL valid; 12 refused, 6 accepted, 3 pass malformed); Geogram valid 63 PASS, 9 FAIL for both thread settings (its `box_edge_touch` union now passes), malformed 14 accepted, 6 pass, 1 FAIL. Both libraries were re-scored.

## MP17 (added 2026-11-14): exact plane-based integer boolean, cost and fidelity POC
**Question.** Can a fixed-width integer (plane-based) boolean give exact answers below Manifold's 1e-12 floor at a cost that beats the published exact engines, with no GPL or unlicensed code?
**Basis (verified by me from the survey's formulas, `lib/planebased/SURVEY.md`):** plane coefficients need b >= 9L + 18.6 bits for L-bit grid inputs (my recomputation: L=26 -> 253, L=40 -> 379, L=43 -> 406, L=53 -> 496). 256-bit (the published width) covers L <= 26 only. Code: none usable (EMBER binary only; octree-BSP and Campen-Kobbelt unreleased; the third-party port depends on an unlicensed library and is not exact). So this is a clean-room implementation from the papers' maths; I do not read or copy the unlicensed repo.
**Stated limit before any result:** snapping inputs to an L-bit grid is itself a rounding of relative size 2^-L. L=40 equals Manifold's 1e-12 class, so it does not beat it. Only L=53 (512-bit) matches binary64 mantissa resolution relative to the bounding box, and even then arbitrary binary64 (exponent range 2^2098) is not representable without bignums.
**Steps and bars (registered before running):**
1. Cost of the core predicates at widths 256/384/512 bits (plane through 3 integer points, plane-plane-plane vertex sign, point-plane side) on a fixed-width integer class written for this POC, vs Shewchuk orient3d and vs `cpp_int`. Report ns/op on M1 and the 32-bit-limb op counts (GPU suitability). Bar: report only.
2. Exactness: every result equals the Boost.Multiprecision `cpp_int` recompute on 1e7 random plus adversarial inputs at L = 26/40/53 (0 differences, hard), with planted mutants (carry drop, wrong sign) all caught. Add an **extreme class** at coordinates of +/-(2^L - 1) and other maximal-determinant inputs: random inputs almost never reach the magnitude limit, so this class is what shows the width is enough. The 9L + 18.6 bound comes from the octree-BSP paper (Sec 3.2), whose constants include octree cell planes; **I derive the bound for this POC's own predicates** and check it against the extreme class.
   **No new planes:** exactness without width growth requires that no plane is ever built from a constructed (intersection) vertex; planes come only from input triangles. Output vertices are exact rationals rounded to nearest binary64 on output, and `mesh_compare` scores the rounded mesh (so the rounding can itself fail a case; reported).
3. **Width census first (before the integer class is written):** run every previously failing valid case through `toCommonInts` (`harness/exact.h`) after the same exact re-centring as before and record the integer width L each needs. Then the rule, fixed now: cases with L at or below the chosen width are scored against the **original, unsnapped** input with `mesh_compare` and must show 0 FAIL; cases above it are reported as OUT-OF-RANGE or refused. **Nothing is snapped and then scored.** Cases counted by distinct name: the union of Manifold's 15 and Geogram's 9 valid-input failures (they share the 6 offset cases), so the census lists each name once with which library failed it.
4. Speed vs Manifold and Geogram on the same cases (honest: single thread, include the quantization step).
**Kill criteria (numbers fixed now):** if the unfiltered 512-bit integer plane-plane-plane sign costs more than 50x the time of Shewchuk's adaptive `orient3d` on the same random inputs (M1, single thread, one-time measurement), or step 3 needs more than the plane-based core plus a simple arrangement to pass, stop and record why.
**Census result (2026-11-14, `RESULTS.md`):** the 7 distinct failing names need L <= 53, so the registered width is **512-bit**; curved cases (L = 57-59) are OUT-OF-RANGE at 512.
**Outcome (2026-11-14):** steps 1-2 exactness bars met; kill criterion triggered at 800x (RESULTS.md); steps 3-4 not run. MP17 stopped as registered.
**Out of scope:** GPU kernels (step 1 only counts limb ops), octree acceleration, non-manifold repair, production API.

## Resource rules
- M1 only through `tools/with_gpu_lock.sh`. Lavapipe (x86_64 and aarch64) runs the suite in CI at no cost. T4 only at gates, through `tools/t4_ci_turn.sh`, after the lavapipe job passes; `[skip gpu]` on commits that do not need it.
- Sub-agents for CPU-only independent work (parity and license work, library build and API-fit steps, survey halves, corpus scripts). Shader work stays in the main thread. Advisor before each gate and before each wave starts.
- **Large files go to `./storage` (external volume, ignored by git), never the internal disk** (it has about 12 GiB free): Thingi10K, big generated sets, downloaded libraries and their build trees when large, and Bazel's caches (`. poc/gpu/tools/storage_env.sh`, then `bz build ...`). Small generated files stay in `poc/gpu/build/`.
- No push without asking; commit locally to `vulkan-gpu`.

## Out of scope
Anything under `src/`; vendoring a library into the repo; CGAL, SFCGAL and OCCT code or builds; a patched-library GPU boolean (MP16); GPU work for 2D in the first pass; production hardening.
