# MP06 SFCGAL parity matrix

Question: how much of SFCGAL v2.3.0 do permissively licensed libraries cover? Rows are in `parity.csv` (123 rows; 84 count toward the
percentages). Licenses are in `LICENSES.md`. Generated 2026-11-13; sources read live on that date. Nothing here was built or run: every
cell is "documented in a header or doc I read", not "tested". Behavior tests belong to MP03/MP04/MP08a.

## Method

1. **Inventory.** Rows come from SFCGAL's own API surface at tag v2.3.0: every `sfcgal_*` function in `src/capi/sfcgal_c.h` (about 250 names), the
   C++-only headers under `src/algorithm/` (`Sweep.h`, `meshToPolyhedralSurface.h`, ...), the type headers, the `CHANGELOG.md` 2.3.0 entries and the PostGIS
   SFCGAL reference page (only the `CG_*`/`ST_*` names listed there were read; entries marked `(GEOS)` or `(core)` in the `postgis_function`
   column are from general knowledge and were not verified). Functions that exist in 2D and 3D forms are split into two rows.
   Near-duplicates are folded into one row (for example `rotate_x/y/z`); the `sfcgal_api` column lists the names folded in.
2. **Coverage cells.** `covered` = the library has an API whose header or doc comment I read and whose documented behavior matches;
   `partial` = it exists with a documented restriction (closed manifolds only, clamped, wrong domain, building block only) or covers only
   some sub-cases; `no` = no counterpart in the headers/listings read; `unverified` = a plausible counterpart exists but I did not read its doc.
   For Boost.Geometry, `no` on a 3D or solid row means "no such algorithm in the `include/boost/geometry/algorithms/` directory listing at
   boost-1.92.0 and (for area) the header asserts dimension 2"; absence of an `assert_dimension` was never taken as support. A similar
   function name was never accepted as evidence (example: Geogram `compute_minkowski_sum_*` exists but its doc says "Not implemented yet", so `no`).
3. **Geogram is scored for the permissive build** (`GEOGRAM_WITH_TETGEN=OFF`, `GEOGRAM_WITH_TRIANGLE=OFF`, `GEOGRAM_WITH_HLBFGS=OFF`; all three are
   `option(... ON)` in its root `CMakeLists.txt`). See `LICENSES.md`.
4. **S2 in tree is spherical.** `S2Polygon::InitToUnion/Intersection/Difference` operate on geodesic edges on the unit sphere. It is at most
   `partial` and never by itself makes a row `covered-by-permissive`; all 3D cells are `no`.
5. **CDT is MPL-2.0 (weak copyleft)** and is not counted as permissive. The `earcut_or_cdt` column holds the best of earcut (ISC) and CDT; where only
   CDT supports a row, the notes say so and the status relies on another library.
6. **Status rule.** `covered-by-permissive`: at least one of Manifold, Geogram (permissive build), Embree, Clipper2, Boost.Geometry or earcut has a `covered` cell.
   `partial`: best cell is `partial` (needs our glue/gap code). `write-ourselves`: no usable library cell. `infeasible-in-product`: needs a research-grade
   algorithm (estimate XL, 16 person-weeks or more) that no audited library provides. `unverified`: I could not establish the status from sources read.
7. **Size scale for `own_code_estimate` (person-weeks), +-2x, not derived from SFCGAL line counts** (SFCGAL mostly wraps CGAL, so its size says nothing about ours):
   S = 1 (glue or a small function, under about 500 lines with tests); M = 3 (a self-contained algorithm, about 500 to 2000 lines, with a degenerate-case
   test set); L = 8 (a robust algorithm with a large degenerate-case surface and its own corpus); XL = 16 or more (research-grade, for example straight skeleton, alpha wrapping).
   Estimates appear only on `write-ourselves` and `partial` rows (for `partial`, the cost of closing the gap). XL estimates for `infeasible-in-product` rows are in the notes.
   Shared code is estimated once (type-system row; transform rows share one S; skeleton rows share one XL).
8. **Excluded from percentages:** groups `types` (data model), `io_utilities` (formats and plumbing) and `beyond_sfcgal` (not SFCGAL).
   Percentages use the other 84 rows. `dim` buckets: `2D`, `3D`, and `2.5D` (2D inputs with Z or 2D to 3D constructions; reported separately, also combined with 3D below).

## Formula

For a bucket with N rows, counting C = `covered-by-permissive`, P = `partial`, W = `write-ourselves`, I = `infeasible-in-product`, U = `unverified`
(U and I stay in N and count as not covered):

- **without write-ourselves** = C / N  (permissive libraries only; `partial` is not counted because closing it needs our code)
- **with write-ourselves** = (C + P + W) / N  (everything except infeasible and unverified)
- reference: (C + P) / N

## Coverage

| Bucket | N | C | P | W | I | U | without = C/N | with = (C+P+W)/N | (C+P)/N |
|---|---|---|---|---|---|---|---|---|---|
| 2D | 36 | 16 | 9 | 6 | 3 | 2 | **44.4%** | **86.1%** | 69.4% |
| 3D | 44 | 11 | 21 | 7 | 5 | 0 | **25.0%** | **88.6%** | 72.7% |
| 2.5D | 4 | 2 | 1 | 1 | 0 | 0 | 50.0% | 100% | 75.0% |
| 3D + 2.5D | 48 | 13 | 22 | 8 | 5 | 0 | 27.1% | 89.6% | 72.9% |

Against the PLAN gate G3 (suggested 70% without, 90% with): **neither bucket reaches 70% without write-ourselves** (2D 44.4%, 3D 25.0%).
With write-ourselves, 2D (86.1%) and 3D (88.6%) are close to but under 90%, and the shortfall is exactly the `infeasible-in-product` and `unverified` rows
(straight skeleton family, 3D alpha wrapping, medial axis). The strict `without` number is dragged down by 8 transform rows and 7 other small `partial` rows that need only glue
(S = 1 each); the (C+P) column shows the effect (2D 69.4%, 3D 72.7%).

Summed estimates of `partial` + `write-ourselves` rows (counted groups only, person-weeks, +-2x): 2D 34, 3D 55, 2.5D 2, total about 91. Outside the percentages: type system 3,
I/O and utilities 9, beyond-SFCGAL 20. The two XL items (straight skeleton family, 3D alpha wrapping) add 32 more if attempted.

### Per-group counts (all dims, counted groups)

| Group | rows | C | P | W | I | U |
|---|---|---|---|---|---|---|
| primitives | 6 | 4 | 2 | 0 | 0 | 0 |
| predicates_measures | 22 | 12 | 6 | 4 | 0 | 0 |
| boolean | 8 | 6 | 2 | 0 | 0 | 0 |
| constructive | 14 | 3 | 7 | 0 | 4 | 0 |
| triangulation_partition | 8 | 3 | 1 | 4 | 0 | 0 |
| skeleton_medial | 5 | 0 | 0 | 0 | 3 | 2 |
| alpha_visibility | 5 | 0 | 2 | 2 | 1 | 0 |
| simplify_repair | 5 | 1 | 3 | 1 | 0 | 0 |
| transforms | 8 | 0 | 8 | 0 | 0 | 0 |
| nurbs | 3 | 0 | 0 | 3 | 0 | 0 |
| **counted total** | **84** | **29** | **31** | **14** | **8** | **2** |
| types (excluded) | 14 | 0 | 0 | 14 | 0 | 0 |
| io_utilities (excluded) | 9 | 0 | 0 | 9 | 0 | 0 |
| beyond_sfcgal (excluded) | 16 | 9 | 4 | 1 | 0 | 2 |

Caveats: row granularity changes percentages (folding or splitting rows moves them by a few points); `covered` is a documentation claim, and the 2D booleans in
particular round to a grid or doubles where SFCGAL uses an exact kernel, so last-bit equality with SFCGAL is not claimed anywhere; the `needs_exactness` and `gpu_able`
columns are my assessment from the PLAN's tiers, not measurements.

## Biggest gaps

1. **Straight skeleton family** (straight skeleton x3, extrude straight skeleton, hipped/gable/skillion roofs; medial axis x2 unverified depends on it). 8 rows, no audited
   permissive implementation, SFCGAL gets it from CGAL (GPL). XL(16). A permissive straight-skeleton or Boost.Polygon voronoi route was not read (unverified lead).
2. **3D alpha wrapping** (CGAL Alpha_wrap_3). XL(16); needs robust 3D Delaunay plus offset refinement.
3. **Exact 3D solid-level predicates and measures** (intersects 3D, distance 3D, covers 3D, validity 3D, planarity, centroid 3D): every library offers surface queries or float32/double
   results; none offers exact solid predicates. About 15 person-weeks of own code, and this is where the GPU filter (MP09/MP11/MP12) has to land.
4. **3D construction on open or non-manifold surfaces and the glue ops** (boolean on open PolyhedralSurface/TIN, split3D, sweep, buffer3D, chamfer/fillet, surface simplification with target counts):
   Manifold refuses non-manifold input, Geogram requires closed meshes without intersections. About 26 person-weeks including a robust quadric simplifier (L, 8).
5. **2D CGAL-only algorithms** (four convex/monotone partitions, point and segment visibility, alpha and optimal alpha shapes): no permissive library; about 27 person-weeks; plus medial axis unverified.

Not gaps but worth flagging: the audited 2D booleans and offset are covered but non-exact (MP08a decides whether that is acceptable); Geogram is only usable with three restricted bundled
components switched off (`LICENSES.md`), which removes tetrahedralization, its Triangle-based Delaunay and the Newton step of `remesh_smooth`.

## Sources read (version and commit)

| Tag in notes | Source | Version / commit | Files read |
|---|---|---|---|
| SF | SFCGAL | v2.3.0, 137203dae01faab4a1287934f9457e925e3b4383 (CHANGELOG dated 2026-06-12) | `src/capi/sfcgal_c.h`, `src/algorithm/*.h`, `CHANGELOG.md`, `README.md`, `LICENSE`; `roofGeneration.cpp` includes, `offset.h`, `buffer3D.h`, `Sweep.h`, `simplification.h` doc comments |
| PG | PostGIS SFCGAL reference | https://postgis.net/docs/reference_sfcgal.html, fetched 2026-11-13 (server Last-Modified 2026-10-09) | function-name list only |
| M1 | Manifold `include/manifold/manifold.h` | v3.5.5, 32271a1a999ce721ffaf5ab10e9746ceeb696547 (tagged 2026-11-13, the day of this audit; v3.5.4 is ce50d78021d64507f89e8c9fc2c2e51018117857) | full header |
| M2 | Manifold `cross_section.h` | same | full header |
| M3 | Manifold `polygon.h` | same | full header |
| M4 | Manifold `src/manifold.cpp` doc comments (MinkowskiSum, MinGap, RayCast, Hull, Slice, Project, Simplify, SplitByPlane, Volume) | same | doc comments |
| G1 | Geogram `mesh/mesh_surface_intersection.h` (booleans, `mesh_union/intersection/difference`) | v1.10.1, bbcacba146b357926b5524111f56cc70c42182a7 | header |
| G2..G15 | Geogram `mesh_minkowski.h` (G2), `mesh_convex_hull.h` (G3), `mesh_decimate.h` (G4), `mesh_AABB.h` (G5), `mesh_fill_holes.h` (G6), `mesh_repair.h` (G7), `mesh_remesh.h` (G8), `mesh_geometry.h` (G9), `delaunay/CDT_2d.h` (G10), `mesh_distance.h` (G11), `mesh_tetrahedralize.h` (G12), wiki BooleanOps (G13), `mesh_smoothing.h` (G14), `voronoi/CVT.cpp` (G15) | same | headers; G13 = https://github.com/BrunoLevy/geogram/wiki/BooleanOps fetched 2026-11-13 |
| E1..E3 | Embree `include/embree4/rtcore_scene.h` (E1), `rtcore_geometry.h` (E2), `rtcore_builder.h` (E3), `rtcore_common.h`, `rtcore_ray.h` | v4.4.1, f590db83ef6559387df7f6d8725c34fb7acf851d | headers |
| C1..C3 | Clipper2 `clipper.h` (C1), `clipper.minkowski.h` (C2), `clipper.triangulation.h` (C3, BETA), `clipper.core.h`, `clipper.offset.h` | Clipper2_2.0.1, 21ebba05db8894f0c7217ad35ea518080f324946. (Manifold builds against commit 46f639177fe418f9689e8ddb74f08a870c71f5b4 plus `cmake/patches/0001-clipper2-no-iostream.patch`, not this tag.) | headers |
| B1, B2, B3, B4 | Boost.Geometry `doc/reference/algorithms/*.qbk` (B1), `algorithms/area.hpp` (B2), `strategies/cartesian/distance_pythagoras.hpp` (B3), `union.hpp`/`difference.hpp` (B4), directory listing of `include/boost/geometry/algorithms/` | boost-1.92.0, 90c27ef0bf9d1700f553b85f890f43461eced529 (in-tree Boost is 1.88.0 without `libs/geometry`) | as listed |
| EC1 | earcut.hpp `README.md`, `include/mapbox/earcut.hpp` | v3.2.4, 92acda9e2ed96ff1f910cda35de444727e9da7b0 | README, header |
| CDT1 | CDT `CDT/include/Triangulation.h`, `predicates.h` | 2.0.2, b4413ee44affb489099ed4d6ec65e94788cb6708 | headers |
| S1 | In-tree S2 `src/third_party/s2/s2polygon.h` | `README.third_party.md` and `sbom.json` list google/s2geometry a25c502bda9d7e0274b9e2b7825fbddf13cc0306 (2015-04-10); git log shows the files imported 2012-10-24 (325b42be26c, SERVER-2874); repo HEAD 911f03a1783 | header |

The two S2 provenance facts disagree (an SBOM commit from 2015 versus an import in 2012): the SBOM sha cannot be the original import source. There is no import script or VERSION file under `src/third_party/s2`.

## Cells left unverified

Cell value `unverified` in `parity.csv` (33 cells): Boost.Geometry for generic or N-dimensional operations I did not read (intersects 3D, distance 3D, centroid 3D, envelope 2D/3D, boundary,
the seven translate/rotate/scale/affine rows, WKT/WKB, the utility rows, and 12 type rows) and `earcut_or_cdt` for the two envelope/type rows. Rows with `status=unverified`:
approximate medial axis, projected medial axis, convex decomposition, voxelization. Library API for external "beyond SFCGAL" candidates (meshoptimizer, V-HACD, CoACD, OpenVDB) was not read,
only their licenses (`LICENSES.md`).
