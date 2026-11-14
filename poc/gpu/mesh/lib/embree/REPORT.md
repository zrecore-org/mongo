# MP05 report: Embree v4.4.1 fit test and our own deterministic CPU BVH (2026-11-13)

Machine: Apple Silicon arm64 (8 hardware threads), AppleClang 21.0.0 (clang-2100.4.1.1), cmake + ninja, Release. Everything was built under
`/Volumes/CrucialX9/Projects/mongo/storage/mesh/embree/` (src, build, install, binaries, data); nothing in `poc/gpu/build`. No GPU, no CI. Nothing committed.

## Files
- `poc/gpu/mesh/lib/embree/`: `runner.cpp` (embree_runner), `build.sh`, `genpoints.cpp`, `refpip.cpp` (double reference), `nearsurface.cpp` (near-surface point generator), this report.
- `poc/gpu/mesh/cpu/`: `bvh_ref.h`, `bvh_pairs.cpp` (library + `bvh_pairs_bench` main under `-DBVH_PAIRS_MAIN`), `build_cpu.sh`.

## Part 1: Embree

### Build
`git clone https://github.com/embree/embree`; `git checkout f590db83ef6559387df7f6d8725c34fb7acf851d`; `git rev-parse HEAD` printed `f590db83ef6559387df7f6d8725c34fb7acf851d` (v4.4.1).
```
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=.../install \
  -DEMBREE_ISPC_SUPPORT=OFF -DEMBREE_TASKING_SYSTEM=INTERNAL -DEMBREE_TUTORIALS=OFF -DEMBREE_STATIC_LIB=ON \
  -DEMBREE_GEOMETRY_TRIANGLE=ON -DEMBREE_GEOMETRY_USER=ON -DEMBREE_GEOMETRY_QUAD=OFF -DEMBREE_GEOMETRY_CURVE=OFF \
  -DEMBREE_GEOMETRY_SUBDIVISION=OFF -DEMBREE_GEOMETRY_INSTANCE=OFF -DEMBREE_GEOMETRY_INSTANCE_ARRAY=OFF \
  -DEMBREE_GEOMETRY_GRID=OFF -DEMBREE_GEOMETRY_POINT=OFF -DEMBREE_FILTER_FUNCTION=ON
cmake --build build -j 3
```
Other options as left at their defaults: RAY_MASK=ON, BACKFACE_CULLING=OFF, MAX_INSTANCE_LEVEL_COUNT=1, MIN_WIDTH=OFF, `EMBREE_ISA_NEON=OFF`, `EMBREE_ISA_NEON2X=ON`, `EMBREE_MAX_ISA=NONE` (full list saved in `storage/mesh/embree/cmake_options.txt`).
Build wall time (`-j 3`, configure excluded, includes install): **1 min 42 s**. cmake found no ISPC, no TBB needed. Compiler: AppleClang 21.0.0.
What Embree actually enabled (its own `verbose=1` device banner): `Compiler: CLANG 21.0.0`, `ISA : ... AVX2 FMA3 ... NEON 2xNEON`, `Targets : SSE ... AVX2 NEON 2xNEON (supported)`, `AVX2 (compile time enabled)`, `Features: raymasks intersection_filter`, `Tasking: internal_tasking_system`.
Anomaly: on arm64 the single compiled kernel library is `libembree_avx2.a` (the "AVX2" tier is emulated over NEON via the bundled sse2neon/avx2neon headers, i.e. the 2xNEON path); the link line needs `-lembree4 -lembree_avx2 -lsimd -llexers -lmath -lsys -ltasking` (listed twice, static-archive cycle; one linker warning about duplicates, harmless).
Runner: `c++ -std=c++17 -O2` (see `build.sh`).

### Runner design
- Scene built from the **float32 conversion** of the vertices (`(float)v`), indices as is. `pip`: fixed direction (1,0.001,0.0003)/norm, one `rtcIntersect1` per point with an intersect filter that counts every hit and rejects it (so one call visits all hits along the ray, parity = count&1). Single thread, no `rtcCommitScene` time included in the query time (it is shown separately).
- `closest`: `rtcPointQuery` with a point-query callback computing the closest point on each candidate triangle in binary64 from the float32 vertices (Ericson), shrinking the query radius; output `%.17g`.
- Points: `genpoints 100000` = mt19937_64 seed 12345, uniform_real_distribution<double>(-2.5,2.5), x,y,z in that order, `%.17g`, in `storage/mesh/embree/data/points_100k.txt`.
- Reference `refpip`: plain O(n) Moller-Trumbore in binary64 over the double mesh with the same direction (rounded through float32 so both use the identical direction). It is a simple reference, **not exact** (rounded arithmetic, no tie rule for edge hits).

### Results (100,000 random points, single thread)
| mesh | tris | pip: Embree | inside | closest: Embree | BVH build | refpip (double, O(n)) | inside (ref) | pip mismatches vs ref |
|---|---|---|---|---|---|---|---|---|
| icosphere_5 | 20,480 | 0.0033 s | 3517 | 0.878 s | 1.9 ms | 5.8 s | 3517 | 0 |
| torus_32x16 | 16,384 | 0.0057 s | 7768 | 0.312 s | 3.1 ms | 4.75 s | 7768 | 0 |

Raw: `points 100000 inside 3517 seconds 0.003343 (bvh build 0.001940)`; closest `points 100000 seconds 0.878017`.
Closest-point is much slower than ray parity because the point-query callback is a generic user callback with a float radius that starts at infinity (not tuned; no initial radius bound). Closest-point distances were not compared against an exact reference (not done).

### float32 means NOT exact: misclassification demonstrated
On the random points the answers agree (0 mismatches of 100,000; random points are almost never within float32 error of the surface). `nearsurface` generates points at triangle centroid +- delta * unit normal (200 sampled triangles x deltas {1e-3,1e-5,1e-6,1e-7,1e-8} x {outside, inside} = 2000 points per mesh) and compares to the double reference (whose own error is ~1e-16, far below those offsets, so it is trustworthy here but still not claimed exact):
| delta | icosphere_5 mismatches /400 | torus_32x16 mismatches /400 |
|---|---|---|
| 1e-3, 1e-5, 1e-6 | 0 | 0 |
| 1e-7 | 0 | 5 |
| 1e-8 | 125 | 133 |
Concrete example (icosphere_5): point `-0.53042557692711534 0.84728045621132664 0.017743612744016393`, lies 1e-8 outside a triangle centroid along the outward normal. Double reference: outside (0). Embree on the float32 mesh: **inside (1)**. Torus example: `1.7257721220771089 0.036222045481576315 -0.41819979389449863` (inside-side offset 1e-8): reference 1 (inside), Embree 0. Embree is not usable as an exactness-tier engine; it is only a speed baseline. Data: `storage/mesh/embree/data/near_*.txt`, `nemb_*.txt`, `nref_*.txt`.
Not done: ray hits exactly on an edge/vertex are not exercised (parity rule at shared edges is whatever Embree's watertight intersection and the filter give), no multi-threaded Embree query (ray queries would scale with threads; only 1 thread measured), no Embree BVH quality variants (default build quality, default scene flags).

## Part 2: own deterministic CPU BVH and box-overlap pairs

**What it is:** a candidate set. `overlapPairs` returns all (i,j) whose binary64 axis-aligned triangle boxes overlap (closed boxes). It says nothing about whether the triangles actually intersect; it is not exact geometry. Box corners are exact min/max of the binary64 coordinates (no inflation), and comparisons of doubles are exact, so the candidate set is exactly the set defined by the box predicate.
Design (also in the header): 63-bit Morton code of box centroids (21 bits/axis; quantisation = one subtract and multiply and truncate in double with `-ffp-contract=off`, then integer interleave), sort by (code, triangle index), split the sorted array at its midpoint down to leaves of <= 4 triangles, BVH over B, one traversal per A triangle with an explicit stack, js of one i sorted ascending. Multi-thread: A is cut into fixed chunks of 256 consecutive triangles, threads claim chunks atomically, each chunk writes its own slot, slots are concatenated in chunk order, so output is sorted by (i,j) and identical for any thread count. BVH build is single-threaded. `bruteForcePairs` is the plain O(nA*nB) loop.
Build: `c++ -std=c++17 -O2 -ffp-contract=off -pthread -DBVH_PAIRS_MAIN ... bvh_pairs.cpp -o storage/mesh/embree/bvh_pairs_bench`. Bench: `bvh_pairs_bench A.msh B.msh [--translate dx dy dz] [--threads-check] [--force-bf]`, `bvh_pairs_bench --set file.mshc`. sha256 is a small built-in implementation over the (i,j) uint32 pairs.

### Adversarial corpus (31 cases, `poc/gpu/build/corpus/adversarial`)
**BVH == brute force: 31 of 31** (pair counts range from 0 to 17,012; e.g. box_gap_one_ulp 0, box_identical 120, sphere_identical 17012, sphere_vs_star 5889, torus_vs_sphere 1000). For every case the 8-thread output equals the 1-thread output, and the sha256 at 1, 2, 4 and 8 threads is identical (one distinct hash per case; per-case counts in `storage/mesh/embree/data/adv_summary.txt`). Example: sphere_vs_star `sha256 466527bebf94edc2d82633c7ca33d3f070e3aa14acd1c3c1e6b19d08f6851800` at 1, 2, 4, 8 threads.
Note: some adversarial pairs give 0 box pairs (box_gap_one_ulp; box_nested_inside, where nested boxes' triangle boxes do not touch) and several share the same hash because the box layouts coincide (box_shift_2^-20/-40/-52, huge_offset_one_ulp: 80 pairs, same hash); box overlap cannot distinguish them, which is expected of a candidate set.

### Translated icosphere and large cases (A vs itself translated by (0.5,0.25,0.125))
| case | triangles each | pairs | sha256 equal at 1,2,4,8 threads | build | query 1 thread | query 8 threads | speedup | notes |
|---|---|---|---|---|---|---|---|---|
| icosphere_4 | 5,120 | 2,380 | yes | 0.3 ms | 1.0 ms | 0.3 ms | 3.0x | brute force 2,380, equal |
| icosphere_5 | 20,480 | 4,808 | yes (`2f337da1...`) | 1.2 ms | 3.1 ms | 0.7 ms | 4.4x | nA*nB = 4.19e8 > 4e8, skipped by the rule; verified with `--force-bf` (0.39 s): 4,808 equal |
| icosphere_8 (few-large) | 1,310,720 | 38,044 | yes (`f7cd3642...`) | 108.5 ms | 141.7 ms | 22.4 ms | 6.3x | peak RSS 286 MB, 1.1 s total wall |
| icosphere_9 (extra) | 5,242,880 | 76,234 | yes (`11d38233...`) | 488 ms | 561 ms | 84.7 ms | 6.6x | peak RSS 1.13 GB, 4.7 s wall |
The large inputs were generated with `gen_corpus storage/mesh/corpus_large --large --many 10` (sets/large_icosphere_8.msh 31 MB, large_icosphere_9.msh 126 MB; the generator also wrote its other corpus into that directory). Brute force was not run on icosphere_8/9 (rule: > 4e8 products). Small cases (e.g. sphere_vs_star) run slower with 8 threads than with 1 (thread startup dominates, 0.0010 s vs 0.0006 s).

### Many-small (`sets/many_small_200.mshc`, 200 meshes, 110,800 triangles, 199 consecutive pairs)
`box-overlap pairs total (BVH) 0, brute force 0, equal YES`. All 199 consecutive pairs: BVH 1 thread **5.2 ms** total; 8 threads (threads created per pair) 31.7 ms (slower: per-pair thread spawn overhead dominates at ~560 triangles per mesh; a persistent thread pool would be needed); BVH + brute-force verification 56 ms. Anomaly: the consecutive meshes in this set never overlap by box, so the pair lists are empty and the equality check is trivially true; the timing mostly measures building a BVH and one traversal that prunes at the root. A harder many-small workload (overlapping neighbours) was not run.

## What was not done
- No exactness for Embree and none claimed; the BVH list is a box-overlap candidate set.
- No Embree multi-thread or alternative BVH quality runs; no Embree closest-point accuracy check; no Embree vs own-BVH timing on the same pair-finding problem (Embree has no mesh-vs-mesh box-pair API in the way used here, so a like-for-like comparison would need a user-geometry or per-triangle box query loop; not attempted).
- BVH build is single-threaded (the dominant cost on icosphere_9 after query parallelisation); no SAH, no persistent thread pool.
- Cross-platform (x86_64) determinism of the Morton quantisation not tested; only arm64 here (the pair set does not depend on the Morton step, only speed does; the tree shape could differ).
