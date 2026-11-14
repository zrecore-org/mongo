# MP03: Manifold evaluation (build and API fit)

Status: measurements and facts only. This report does NOT decide whether Manifold passes; scoring is done later by a separate exact tool.
Date of run: 2026-11-13. Machine: Apple M1 Pro, macOS 27.0.1 (arm64). Everything built under `storage/mesh/manifold/` (external volume).

## Versions and dependencies actually used
- Compiler: `Apple clang version 21.0.0 (clang-2100.4.1.1)`, Target `arm64-apple-darwin27.0.0`; cmake 4.4.4; ninja 1.13.2.
- Manifold: `git clone https://github.com/elalish/manifold`, `git checkout 32271a1a999ce721ffaf5ab10e9746ceeb696547`; `git rev-parse HEAD` printed exactly `32271a1a999ce721ffaf5ab10e9746ceeb696547` (tag v3.5.5, `MANIFOLD_VERSION` 3.5.5 in CMakeLists.txt; the headers carry no version macro, so the runner prints a version string passed by `build.sh`).
- Clipper2 (fetched by Manifold's CMake, `MANIFOLD_USE_BUILTIN_CLIPPER2=ON` to force the fetch): commit `46f639177fe418f9689e8ddb74f08a870c71f5b4` (read back with `git -C buildA/_deps/clipper2-src rev-parse HEAD`), plus Manifold's carry-patch `cmake/patches/0001-clipper2-no-iostream.patch`. Same in build B.
- No oneTBB (MANIFOLD_PAR=OFF; the compile line carries `-DMANIFOLD_PAR=-1`), no nanobind, no googletest, no tracy downloaded.
- The linked product: `libmanifold.a` + `libClipper2.a` (static). The runner and bench are compiled separately with `c++ -std=c++17 -O2 -DNDEBUG` (A also `-ffp-contract=off`).

## Commands (exact)
```
cd /Volumes/CrucialX9/Projects/mongo/storage/mesh/manifold
git clone https://github.com/elalish/manifold src && git -C src checkout 32271a1a999ce721ffaf5ab10e9746ceeb696547
# build A (explicit -ffp-contract=off)
cmake -S src -B buildA -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DMANIFOLD_PAR=OFF -DMANIFOLD_PYBIND=OFF -DMANIFOLD_TEST=OFF -DMANIFOLD_EXPORT=OFF -DMANIFOLD_CBIND=OFF -DMANIFOLD_CROSS_SECTION=ON -DMANIFOLD_USE_BUILTIN_CLIPPER2=ON -DCMAKE_CXX_FLAGS=-ffp-contract=off
# build B (default flags)
cmake -S src -B buildB -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DMANIFOLD_PAR=OFF -DMANIFOLD_PYBIND=OFF -DMANIFOLD_TEST=OFF -DMANIFOLD_EXPORT=OFF -DMANIFOLD_CBIND=OFF -DMANIFOLD_CROSS_SECTION=ON -DMANIFOLD_USE_BUILTIN_CLIPPER2=ON
TMPDIR=/Volumes/CrucialX9/Projects/mongo/storage/tmp ninja -C buildA -j 3 ; ninja -C buildB -j 3      # 25 steps each, both succeeded
poc/gpu/mesh/lib/manifold/build.sh A ; poc/gpu/mesh/lib/manifold/build.sh B    # -> storage/mesh/manifold/run{A,B}/manifold_runner, manifold_bench
```
Corpus runs: a small Python driver (kept in the agent scratchpad, not in the repo; it only calls `manifold_runner boolean <op> <id>_A.msh <id>_B.msh out` with `timeout=60`, wall time by `time.monotonic()`, sha256 of the output) over the 31 cases x 3 ops, for runA, runA again (A2) and runB. Outputs are in `storage/mesh/manifold/out/{A,A2,B}/<case>.<op>.msh`, raw rows in `out/results.json`.
Speed: `/usr/bin/time -l runA/manifold_bench primitives/icosphere_5.msh 0.25 0.125 0.0625 5`.

## Anomalies found while building (read these first)
1. **`MANIFOLD_EXPORT` is not an option at this tag.** CMake printed: `Manually-specified variables were not used by the project: MANIFOLD_EXPORT`. It was passed as requested and is ignored. (The `-DMANIFOLD_EXPORT=OFF` in the commands above therefore did nothing.)
2. **Manifold already compiles its own sources with `-ffp-contract=off`** (`src/CMakeLists.txt:234-237`, `check_cxx_compiler_flag(-ffp-contract=off HAS_FP_CONTRACT)`). Compile line of `manifold.cpp` in build B (default flags): `... -O3 -DNDEBUG -std=c++17 -arch arm64 -fPIC -Wall ... -O3 -ffp-contract=off -fexcess-precision=standard ...`. So builds A and B differ only in the global `CMAKE_CXX_FLAGS`, which in B leaves Clipper2 (compile line `... -O3 -DNDEBUG -std=c++17 -arch arm64 -fPIC -Wall -Wextra -Wpedantic -Werror ...`, no fp-contract flag) and the runner uncontracted-by-flag. Apple clang on arm64 defaults to `-ffp-contract=on` for such code, so contraction was possible in Clipper2 in B. The A/B comparison below is therefore a test of Clipper2 and runner code only, not of Manifold's core, and the result (0 differences) has to be read that way.
3. **Silent empty result at large coordinates (status NoError).** `box_huge_offset_overlap_A.msh` (unit box at 2^40) is accepted by the constructor with `Status()==NoError`, but the object is empty. Probe output (`storage/mesh/manifold/probe`, scratch program printing library queries):
   ```
   box_huge_offset_overlap_A.msh nv=0 nt=0 tol=1.09951 eps=1.09951 vol=0 bbox=1099511627776..1099511627777 ndegen=1
   box_identical_A.msh           nv=8 nt=12 tol=1e-12 eps=1e-12 vol=1 bbox=0..1 ndegen=0
   box_shift_2^-52_B.msh         nv=8 nt=12 tol=1e-12 eps=1e-12 vol=1 bbox=2.2204460492503131e-16..1.0000000000000002 ndegen=0
   ```
   The library's tolerance for that input is 1e-12 * 2^40 = 1.0995, larger than the box edge (1), so the mesh collapses. `manifold_runner measure` on the same file prints `volume=0 area=0 genus=1 status=NoError`. Consequently both huge-offset cases return 0 triangles for union, intersection and difference with exit code 0. (Whether that counts as wrong is for the scorer; the corpus manifest expects union volume 773094113280*2^-36 for `box_huge_offset_overlap`.)
4. **Sub-tolerance perturbations are merged away.** `box_shift_2^-52` (shift 2^-52 ~ 2.2e-16 < tolerance 1e-12) difference returns 0 triangles; `box_shift_2^-40` (9.1e-13 < 1e-12) difference also returns 0 triangles (see table). Facts only; see MP02 oracle for the exact answer.
5. **Malformed inputs: only some are refused.** Refused (exit 3, `REFUSED: NotManifold`): bad_open_box, bad_unwelded, bad_degenerate_triangle, bad_duplicate_triangle. Accepted with exit 0 and a non-empty result: bad_nonmanifold_edge (`measure` of A: `volume=2 area=12 genus=-1 status=NoError`), bad_self_intersecting (`volume=2 area=12 genus=-1 status=NoError`), bad_inverted (`measure` of A: `volume=-1 area=6 genus=0 status=NoError`, a negative volume is accepted; union gives 36 triangles, intersection 12, difference 24).
6. Contract decision (ambiguity): a non-`NoError` status of the *boolean result* (not of an input) would be reported as `ERROR: result status <name>` exit 4. It never happened in these runs. An unreadable/invalid-index MSH1 file exits 3 with `REFUSED: InvalidMeshFile`; a file that cannot be opened exits 4.
7. Both builds used `ninja -j 3` and finished (25/25 steps each); build warnings were not reviewed. The runner build printed one `-Wformat` warning in bench.cpp (size_t vs unsigned long long), harmless.

## Runner contract as implemented
`runner.cpp` / `build.sh` / `bench.cpp` in this directory. `manifold_runner boolean <union|intersection|difference> A B out` and `manifold_runner measure A`, exactly as specified. stdout carries only the `measure` line; the version line `manifold version 3.5.5 (pinned commit 32271a1a...)` goes to stderr once per run. Operands are built with `Manifold(MeshGL64)` (numProp=3, double positions, uint32 triangles widened to uint64), the operation is `Manifold::Boolean(second, OpType)`, and the output is read back with `GetMeshGL64()` (positions as returned, no post-processing).
Raw samples:
```
$ manifold_runner measure primitives/icosphere_3.msh
manifold version 3.5.5 (pinned commit 32271a1a999ce721ffaf5ab10e9746ceeb696547)
volume=4.1527408170930578 area=12.506492733969928 genus=0 status=NoError
$ manifold_runner measure adversarial/bad_open_box_A.msh ; echo $?
manifold version 3.5.5 (pinned commit 32271a1a999ce721ffaf5ab10e9746ceeb696547)
REFUSED: NotManifold
3
```

## Corpus results (31 cases x 3 ops, 60 s timeout; full table below)
Totals per build (93 runs each, A, A2 = second run of A, B): exit 0: 81, exit 3: 12, exit 4: 0, timeouts: 0, stdout output: none in any run.
Sum of wall times: A 0.50 s, A2 0.55 s, B 0.58 s; the slowest single run was 0.126 s (B), 0.027 s (A). Wall time includes process start.
- A vs B: sha256 of the output file differs in **0** of 81 files written (the 12 refused runs write no file in either build; the exit codes are identical in all 93).
- A vs A2 (run-to-run, serial): **0** of 81 files differ (byte-identical); exit codes identical.
Caveat: see anomaly 2 (A vs B tests Clipper2/runner flags only). Run-to-run is one repeat, one thread count (serial build), one machine. Tier 2 multi-thread and tier 3 cross-architecture were NOT tested.

Columns: exit code, wall seconds (build A, first run), output triangle count (-1 = no file written), A==B hash (for refused runs both have no file), A==A2 hash.

| case | op | exit | sec (A) | tris | hash A==B | A==A2 | message |
|---|---|---|---|---|---|---|---|
| box_identical | union | 0 | 0.0045 | 12 | yes | yes |  |
| box_identical | intersection | 0 | 0.0057 | 12 | yes | yes |  |
| box_identical | difference | 0 | 0.0047 | 0 | yes | yes |  |
| box_face_touch_full | union | 0 | 0.004 | 20 | yes | yes |  |
| box_face_touch_full | intersection | 0 | 0.0044 | 0 | yes | yes |  |
| box_face_touch_full | difference | 0 | 0.0042 | 12 | yes | yes |  |
| box_face_touch_partial | union | 0 | 0.0037 | 32 | yes | yes |  |
| box_face_touch_partial | intersection | 0 | 0.0035 | 4 | yes | yes |  |
| box_face_touch_partial | difference | 0 | 0.0051 | 20 | yes | yes |  |
| box_edge_touch | union | 0 | 0.0041 | 24 | yes | yes |  |
| box_edge_touch | intersection | 0 | 0.0037 | 0 | yes | yes |  |
| box_edge_touch | difference | 0 | 0.0038 | 12 | yes | yes |  |
| box_vertex_touch | union | 0 | 0.0034 | 24 | yes | yes |  |
| box_vertex_touch | intersection | 0 | 0.0036 | 0 | yes | yes |  |
| box_vertex_touch | difference | 0 | 0.0035 | 12 | yes | yes |  |
| box_nested_inside | union | 0 | 0.0036 | 12 | yes | yes |  |
| box_nested_inside | intersection | 0 | 0.0038 | 12 | yes | yes |  |
| box_nested_inside | difference | 0 | 0.0034 | 24 | yes | yes |  |
| box_nested_touching_face | union | 0 | 0.0038 | 20 | yes | yes |  |
| box_nested_touching_face | intersection | 0 | 0.0037 | 12 | yes | yes |  |
| box_nested_touching_face | difference | 0 | 0.0034 | 28 | yes | yes |  |
| box_overlap_generic | union | 0 | 0.0047 | 36 | yes | yes |  |
| box_overlap_generic | intersection | 0 | 0.0059 | 12 | yes | yes |  |
| box_overlap_generic | difference | 0 | 0.0045 | 24 | yes | yes |  |
| box_on_top_of_face | union | 0 | 0.0057 | 28 | yes | yes |  |
| box_on_top_of_face | intersection | 0 | 0.0042 | 0 | yes | yes |  |
| box_on_top_of_face | difference | 0 | 0.0042 | 12 | yes | yes |  |
| box_coplanar_overlap | union | 0 | 0.0047 | 36 | yes | yes |  |
| box_coplanar_overlap | intersection | 0 | 0.0042 | 12 | yes | yes |  |
| box_coplanar_overlap | difference | 0 | 0.0039 | 20 | yes | yes |  |
| box_coplanar_chain | union | 0 | 0.0039 | 28 | yes | yes |  |
| box_coplanar_chain | intersection | 0 | 0.004 | 12 | yes | yes |  |
| box_coplanar_chain | difference | 0 | 0.004 | 12 | yes | yes |  |
| box_sliver_inside | union | 0 | 0.0039 | 12 | yes | yes |  |
| box_sliver_inside | intersection | 0 | 0.0037 | 0 | yes | yes |  |
| box_sliver_inside | difference | 0 | 0.0058 | 12 | yes | yes |  |
| box_sliver_crossing | union | 0 | 0.0046 | 12 | yes | yes |  |
| box_sliver_crossing | intersection | 0 | 0.005 | 0 | yes | yes |  |
| box_sliver_crossing | difference | 0 | 0.0053 | 12 | yes | yes |  |
| box_shift_2^-20 | union | 0 | 0.0038 | 28 | yes | yes |  |
| box_shift_2^-20 | intersection | 0 | 0.004 | 12 | yes | yes |  |
| box_shift_2^-20 | difference | 0 | 0.0036 | 12 | yes | yes |  |
| box_shift_2^-40 | union | 0 | 0.0035 | 14 | yes | yes |  |
| box_shift_2^-40 | intersection | 0 | 0.0058 | 12 | yes | yes |  |
| box_shift_2^-40 | difference | 0 | 0.0049 | 0 | yes | yes |  |
| box_shift_2^-52 | union | 0 | 0.0037 | 12 | yes | yes |  |
| box_shift_2^-52 | intersection | 0 | 0.0048 | 12 | yes | yes |  |
| box_shift_2^-52 | difference | 0 | 0.0036 | 0 | yes | yes |  |
| box_gap_one_ulp | union | 0 | 0.0036 | 24 | yes | yes |  |
| box_gap_one_ulp | intersection | 0 | 0.0049 | 0 | yes | yes |  |
| box_gap_one_ulp | difference | 0 | 0.0037 | 12 | yes | yes |  |
| box_huge_offset_overlap | union | 0 | 0.0042 | 0 | yes | yes |  |
| box_huge_offset_overlap | intersection | 0 | 0.0036 | 0 | yes | yes |  |
| box_huge_offset_overlap | difference | 0 | 0.0033 | 0 | yes | yes |  |
| box_huge_offset_one_ulp | union | 0 | 0.0051 | 0 | yes | yes |  |
| box_huge_offset_one_ulp | intersection | 0 | 0.0035 | 0 | yes | yes |  |
| box_huge_offset_one_ulp | difference | 0 | 0.0034 | 0 | yes | yes |  |
| sphere_identical | union | 0 | 0.027 | 1280 | yes | yes |  |
| sphere_identical | intersection | 0 | 0.0138 | 1280 | yes | yes |  |
| sphere_identical | difference | 0 | 0.0141 | 0 | yes | yes |  |
| sphere_shift_2^-30 | union | 0 | 0.0129 | 1544 | yes | yes |  |
| sphere_shift_2^-30 | intersection | 0 | 0.012 | 1372 | yes | yes |  |
| sphere_shift_2^-30 | difference | 0 | 0.0127 | 1436 | yes | yes |  |
| sphere_in_box_tangent | union | 0 | 0.0057 | 12 | yes | yes |  |
| sphere_in_box_tangent | intersection | 0 | 0.005 | 1280 | yes | yes |  |
| sphere_in_box_tangent | difference | 0 | 0.0069 | 0 | yes | yes |  |
| sphere_vs_star | union | 0 | 0.0129 | 1334 | yes | yes |  |
| sphere_vs_star | intersection | 0 | 0.0108 | 1466 | yes | yes |  |
| sphere_vs_star | difference | 0 | 0.0108 | 1190 | yes | yes |  |
| torus_vs_sphere | union | 0 | 0.0128 | 4900 | yes | yes |  |
| torus_vs_sphere | intersection | 0 | 0.0094 | 1084 | yes | yes |  |
| torus_vs_sphere | difference | 0 | 0.0117 | 3956 | yes | yes |  |
| bad_open_box | union | 3 | 0.0041 | -1 | yes | yes | REFUSED: NotManifold |
| bad_open_box | intersection | 3 | 0.0044 | -1 | yes | yes | REFUSED: NotManifold |
| bad_open_box | difference | 3 | 0.0041 | -1 | yes | yes | REFUSED: NotManifold |
| bad_nonmanifold_edge | union | 0 | 0.0041 | 58 | yes | yes |  |
| bad_nonmanifold_edge | intersection | 0 | 0.0042 | 24 | yes | yes |  |
| bad_nonmanifold_edge | difference | 0 | 0.0039 | 48 | yes | yes |  |
| bad_self_intersecting | union | 0 | 0.0039 | 36 | yes | yes |  |
| bad_self_intersecting | intersection | 0 | 0.0049 | 24 | yes | yes |  |
| bad_self_intersecting | difference | 0 | 0.0037 | 48 | yes | yes |  |
| bad_unwelded | union | 3 | 0.0041 | -1 | yes | yes | REFUSED: NotManifold |
| bad_unwelded | intersection | 3 | 0.004 | -1 | yes | yes | REFUSED: NotManifold |
| bad_unwelded | difference | 3 | 0.0038 | -1 | yes | yes | REFUSED: NotManifold |
| bad_degenerate_triangle | union | 3 | 0.0048 | -1 | yes | yes | REFUSED: NotManifold |
| bad_degenerate_triangle | intersection | 3 | 0.0036 | -1 | yes | yes | REFUSED: NotManifold |
| bad_degenerate_triangle | difference | 3 | 0.0033 | -1 | yes | yes | REFUSED: NotManifold |
| bad_inverted | union | 0 | 0.0045 | 36 | yes | yes |  |
| bad_inverted | intersection | 0 | 0.0041 | 12 | yes | yes |  |
| bad_inverted | difference | 0 | 0.0039 | 24 | yes | yes |  |
| bad_duplicate_triangle | union | 3 | 0.0033 | -1 | yes | yes | REFUSED: NotManifold |
| bad_duplicate_triangle | intersection | 3 | 0.0055 | -1 | yes | yes | REFUSED: NotManifold |
| bad_duplicate_triangle | difference | 3 | 0.0047 | -1 | yes | yes | REFUSED: NotManifold |
(The table above lists all 93 build-A rows.)

## Speed probe (build A, one machine, one measurement set)
union of `primitives/icosphere_5.msh` (20,480 triangles) with itself translated by (0.25, 0.125, 0.0625), 5 repetitions, each timing MeshGL64 construction of both operands + union + `GetMeshGL64()`:
```
rep 0: 0.058688 s  (construct+union+extract) status=0 tris=24956 verts=12480
rep 1: 0.050582 s  ...
rep 2: 0.050957 s  ...
rep 3: 0.057120 s  ...
rep 4: 0.055359 s  ...
        0.41 real         0.26 user         0.00 sys
            37715968  maximum resident set size
            36815256  peak memory footprint
```
(5 reps in one process, RSS 36 MB. For reference `manifold_runner measure icosphere_5` alone: `volume=4.1865249492787919 area=12.56261346805837 genus=0`, max RSS 12.5 MB.) The union result has 24,956 triangles. Not measured: the 1.3M/5.2M-triangle meshes, many-small sets, TBB build, thread scaling.

## Public API list (headers at the pinned tag)
Almost all doc comments live in the `.cpp` files next to the definitions (the headers carry only declarations), so the quoted sentence is the first sentence of the doxygen comment above the definition in `src/manifold.cpp`, `src/constructors.cpp`, `src/sdf.cpp`, `src/polygon.cpp`, `src/cross_section/cross_section.cpp`, `src/impl.cpp`. Extracted by a script and spot-checked against the files; where overloads exist the comment of one overload is shown. Headers read: `include/manifold/manifold.h`, `cross_section.h`, `polygon.h`, `mesh.h` (MeshGL/MeshGL64), `common.h`.
`enum class Manifold::Error { NoError, NonFiniteVertex, NotManifold, VertexOutOfBounds, PropertiesWrongLength, MissingPositionProperties, MergeVectorsDifferentLengths, MergeIndexOutOfBounds, TransformWrongLength, RunIndexWrongLength, FaceIDWrongLength, InvalidConstruction, ResultTooLarge, InvalidTangents, Cancelled }`. `ToString(Error)` exists only under `MANIFOLD_DEBUG` (the runner has its own name table). `enum class OpType : char { Add, Subtract, Intersect }`.

SFCGAL mapping legend (SFCGAL names as given in the task): `union3D/intersection3D/difference3D` = Boolean; `convexHull3D` = Hull; `extrude` = Extrude; `minkowskiSum3D` = MinkowskiSum; `volume` = Volume; `area3D` = SurfaceArea; `distance3D` = MinGap (note the header signature below); `tessellate/triangulate` = Triangulate. "Matches" below means only that the header has an operation of that name/role; behaviour/semantic equivalence is not claimed.

### Manifold
| Operation and signature | First sentence of doc comment | SFCGAL counterpart |
|---|---|---|
| `Manifold(const MeshGL&)`, `Manifold(const MeshGL64&)` | "Convert a MeshGL into a Manifold, retaining its properties and merging only the positions according to the merge vectors." | input path |
| `MeshGL GetMeshGL(int normalIdx=-1) const`, `MeshGL64 GetMeshGL64(int normalIdx=-1) const` | "Returns a MeshGL that is designed to easily push into a renderer, including all interleaved vertex properties that may have been input." | output path |
| `Manifold Boolean(const Manifold& second, OpType op) const` | "The central operation of this library: the Boolean combines two manifolds into another by calculating their intersections and removing the unused portions." | union3D / intersection3D / difference3D |
| `static Manifold BatchBoolean(const std::vector<Manifold>&, OpType)` | "Perform the given boolean operation on a list of Manifolds." | n-ary form |
| `operator+ / += / - / -= / ^ / ^=` | "Shorthand for Boolean Union." (also "Difference", "Intersection", and "... assignment") | shorthands |
| `std::pair<Manifold,Manifold> Split(const Manifold&) const` | "Split cuts this manifold in two using the cutter manifold." | no counterpart in the header (SFCGAL side not mapped here) |
| `SplitByPlane(vec3 normal, double originOffset) const` | "Convenient version of Split() for a half-space." | no counterpart in the header |
| `Manifold TrimByPlane(vec3 normal, double originOffset) const` | "Identical to SplitByPlane(), but calculating and returning only the first result." | no counterpart in the header |
| `Manifold Hull() const`; `static Hull(const std::vector<Manifold>&)`; `static Hull(const std::vector<vec3>& pts)` | "Compute the convex hull of this manifold." / "Compute the convex hull enveloping a set of manifolds." / "Compute the convex hull of a set of points. If the given points are fewer than 4, or they are all coplanar, an empty Manifold will be returned." | convexHull3D |
| `static Manifold Extrude(const Polygons&, double height, int nDivisions=0, double twistDegrees=0.0, vec2 scaleTop=vec2(1.0))` | "Constructs a manifold from a set of polygons by extruding them along the Z-axis." | extrude (z-axis extrusion of polygons only; no counterpart in the header for an arbitrary direction) |
| `static Manifold Revolve(const Polygons&, int circularSegments=0, double revolveDegrees=360.0)` | "Constructs a manifold from a set of polygons by revolving this cross-section around its Y-axis and then setting this as the Z-axis of the resulting manifold." | no counterpart in the header |
| `Manifold MinkowskiSum(const Manifold&) const` | "Compute the minkowski sum of this manifold with another." | minkowskiSum3D |
| `Manifold MinkowskiDifference(const Manifold&) const` | "Subtract the sweep of the other manifold across this manifold's surface." | no counterpart in the header |
| `Polygons Slice(double height=0) const` | "Returns the cross section of this object parallel to the X-Y plane at the specified Z height, defaulting to zero." | no counterpart in the header |
| `Polygons Project() const` | "Returns polygons representing the projected outline of this object onto the X-Y plane." | no counterpart in the header |
| `double Volume() const` | "Returns the volume of the manifold." | volume |
| `double SurfaceArea() const` | "Returns the surface area of the manifold." | area3D |
| `int Genus() const` | "The genus is a topological property of the manifold, representing the number of "handles"." | no counterpart in the header |
| `double MinGap(const Manifold& other, double searchLength) const` | "Returns the minimum gap between two manifolds. Returns a double between 0 and searchLength." | distance3D (bounded: the result is capped by `searchLength`, per the comment) |
| `std::vector<RayHit> RayCast(vec3 origin, vec3 endpoint) const` | "Cast a ray segment against this manifold, returning all hits sorted by distance from origin." | no counterpart in the header |
| `std::vector<Manifold> Decompose() const` | "This operation returns a vector of Manifolds that are topologically disconnected." | no counterpart in the header |
| `static Manifold Compose(const std::vector<Manifold>&)` (deprecated) | "Deprecated: Use BatchBoolean with OpType::Add instead." | - |
| `Error Status() const` | "Returns the reason for an input Mesh producing an empty Manifold." | validity (status only; see anomalies 3 and 5) |
| `bool IsEmpty()`, `NumVert()`, `NumEdge()`, `NumTri()`, `NumProp()`, `NumPropVert()` | "Does the Manifold have any triangles?" / "The number of vertices in the Manifold." / ... | counts |
| `Box BoundingBox() const` | "Returns the axis-aligned bounding box of all the Manifold's vertices." | envelope |
| `double GetTolerance() const`, `double GetEpsilon() const` | "Returns the tolerance value of this Manifold." / "Returns the epsilon value of this Manifold's vertices, which tracks the approximate rounding error over all the transforms and operations that have led to this state." | - |
| `Manifold SetTolerance(double) const` | "Return a copy of the manifold with the set tolerance value." | - |
| `Manifold Simplify(double tolerance=0) const` | "Return a copy of the manifold simplified to the given tolerance, but with its actual tolerance value unchanged." | no counterpart in the header |
| `Manifold Refine(int)`, `RefineToLength(double)`, `RefineToTolerance(double)` | "Increase the density of the mesh by splitting every edge into n pieces." / "... into pieces of roughly the input length." / "... within tolerance of the smoothly curved surface defined by the tangent vectors." | no counterpart in the header |
| `Smooth(const MeshGL&|MeshGL64&, sharpenedEdges)`, `SmoothByNormals(int)`, `SmoothOut(double, double)` | "Constructs a smooth version of the input mesh by creating tangents; this method will throw if you have supplied tangents with your mesh already." / "Smooths out the Manifold by filling in the halfedgeTangent vectors." | no counterpart in the header |
| `Translate(vec3)`, `Scale(vec3)`, `Rotate(x,y,z degrees)`, `Mirror(vec3)`, `Transform(const mat3x4&)` | "Move this Manifold in space." / "Scale this Manifold in space." / "Applies an Euler angle rotation to the manifold, This operation can be chained." / "Mirror this Manifold over the plane described by the unit form of the given normal vector." / "Transform this Manifold in space." | affine transforms (SFCGAL names not mapped here) |
| `Warp(std::function<void(vec3&)>)`, `WarpBatch(...)` | "This function does not change the topology, but allows the vertices to be moved according to any arbitrary input function." | no counterpart in the header |
| `SetProperties`, `CalculateCurvature`, `CalculateNormals` | "Create a new copy of this manifold with updated vertex properties by supplying a function ..." / "Curvature is the inverse of the radius of curvature, ..." / "Fills in vertex properties for normal vectors, calculated from the mesh geometry." | no counterpart in the header |
| `OriginalID()`, `AsOriginal()`, `static ReserveIDs(uint32_t)` | "If this mesh is an original, this returns its meshID ..." / "This removes all relations (originalID, faceID, transform) to ancestor meshes ..." / "Returns the first of n sequential new unique mesh IDs ..." | provenance |
| `static Tetrahedron()`, `Cube(vec3,bool)`, `Cylinder(h,rLow,rHigh,segments,center)`, `Sphere(r,segments)`, `LevelSet(sdf,Box,edgeLength,level,tolerance,canParallel)` | "Constructs a tetrahedron centered at the origin ..." / "Constructs a unit cube (edge lengths all one), by default in the first octant, touching the origin." / "A convenience constructor for the common case of extruding a circle." / "Constructs a geodesic sphere of a given radius." / "Constructs a level-set manifold from the input Signed-Distance Function (SDF)." | primitives |
| `WithContext(const ExecutionContext&)` | "Returns a copy of this Manifold with the given ExecutionContext attached." | - |
| `ReadOBJ(std::istream&)`, `WriteOBJ(std::ostream&)` (not under MANIFOLD_NO_IOSTREAM) | "Import a mesh from a Wavefront OBJ file." / "Export the mesh to a Wavefront OBJ file in a way that preserves the full 64-bit precision of the vertex positions, ..." | I/O |
| `MatchesTriNormals()`, `NumDegenerateTris()` | "The triangle normal vectors are saved over the course of operations rather than recalculated to avoid rounding error." / "The number of triangles that are colinear within Precision()." | testing hooks |

### Triangulation (`polygon.h`)
| Operation and signature | Doc comment | SFCGAL counterpart |
|---|---|---|
| `std::vector<ivec3> Triangulate(const Polygons&, double epsilon=-1, bool allowConvex=true)` | "Triangulates a set of &epsilon;-valid polygons. If the input is not &epsilon;-valid, the triangulation may overlap, but will always return a manifold result that matches the input edge directions." | tessellate/triangulate (2D polygons with holes; SFCGAL's 3D-polygon/TIN tessellation not checked) |
| `std::vector<ivec3> TriangulateIdx(const PolygonsIdx&, double epsilon=-1, bool allowConvex=true)` | no doc comment at the public definition (the comment text above is on the internal `TriangulateIdxHalfedges`, which says: "@param polys The set of polygons, wound CCW ... with 2D-projected positions as well as references back to the original vertices.") | tessellate/triangulate (indexed form) |

### CrossSection (`cross_section.h`, built because MANIFOLD_CROSS_SECTION=ON; 2D, Clipper2-based)
| Operation | First sentence of doc comment | SFCGAL counterpart |
|---|---|---|
| `CrossSection(const SimplePolygon&|Polygons&, FillRule=Positive)`, `CrossSection(const Rect&)`, `ToPolygons()` | "Return the contours of this CrossSection as a Polygons." (ToPolygons) | 2D I/O |
| `Boolean(second, OpType)`, `BatchBoolean(vector, OpType)`, `+ += - -= ^ ^=` | "Perform the given boolean operation between this and another CrossSection." / "Perform the given boolean operation on a list of CrossSections." | 2D union/intersection/difference (the task list names only the 3D ones) |
| `Offset(double delta, JoinType jt=Round, double miter_limit=2.0, int circularSegments=0)` | "Inflate the contours in CrossSection by the specified delta, handling corners according to the given JoinType." | no SFCGAL name given in the task for offset |
| `Hull()`, `Hull(vector<CrossSection>)`, `Hull(SimplePolygon)`, `Hull(Polygons)` | "Compute the convex hull enveloping a set of cross-sections." | 2D hull |
| `Simplify(double epsilon=1e-6)` | "Remove vertices from the contours in this CrossSection that are less than the specified distance epsilon from an imaginary line that passes through its two adjacent vertices." | - |
| `Decompose()`, `Compose(vector)` | "This operation returns a vector of CrossSections that are topologically disconnected, each containing one outline contour with zero or more holes." / "Construct a CrossSection from a vector of other CrossSections (batch boolean union)." | - |
| `Area()`, `Bounds()`, `IsEmpty()`, `NumVert()`, `NumContour()` | "Return the total area covered by complex polygons making up the CrossSection." / ... | 2D measures |
| `Translate`, `Rotate`, `Scale`, `Mirror`, `Transform`, `Warp`, `WarpBatch`, `Square(dims,center)`, `Circle(radius,segments)` | "Move this CrossSection in space." / "Applies a (Z-axis) rotation to the CrossSection, in degrees." / "Constructs a square with the given XY dimensions." / "Constructs a circle of a given radius." ... | - |

Header-level absences (searched in the three headers for these words; no hit for any of them): no 3D Minkowski difference of arbitrary shapes beyond the one listed, no `distance` for points or non-solids, no straight skeleton, medial axis, alpha shape, visibility, 3D-polygon/TIN input. Specific statements above marked "no counterpart in the header" mean exactly that and nothing about what SFCGAL does.
Note on `MinGap` (quoted comment): "Returns the minimum gap between two manifolds. Returns a double between 0 and searchLength." The header takes a mandatory `searchLength`; unbounded distance is not offered by this signature.

## What I did NOT do
- Did not decide pass/fail; did not compute exact volumes/areas of the outputs (no oracle was run; the table lists only triangle counts and hashes).
- Did not build or test the TBB (parallel) variant, 1/4/8 thread determinism, or tier 3 (x86_64 vs arm64).
- Did not run Thingi10K, many-small sets or the 1.3M/5.2M triangle meshes; only the 31 adversarial pairs and one icosphere_5 union probe.
- A vs B did not isolate Manifold's own use of FMA contraction because Manifold forces `-ffp-contract=off` itself (anomaly 2); I did not build a third variant with `-ffp-contract=on` forced onto Manifold's sources.
- Did not verify the outputs are valid manifolds, CCW outward, or equal across libraries; did not inspect the non-refused malformed-input outputs beyond triangle counts.
- Did not read PRs #1606, #1623, #1594 (named in PLAN.md for the CI determinism check).
- Did not use the GPU, CI, or edit any existing repo file; nothing was git added or committed.
- Run-to-run determinism = one repeat of one serial build on one machine.

