# G3 parity update (draft for the gate decision; written 2026-11-14)

Gate text (PLAN.md): "G3 (after MP06/07/08a/08b), parity: report coverage % for 3D and for 2D separately, with and without write-ourselves items"; suggested bars 70% without, 90% with.
Inputs: `parity.csv` / `PARITY.md` (MP06, documented-not-tested), then the tested evidence in `poc/gpu/RESULTS.md` (MP07, MP08a, MP08b, MP03, MP04, MP05, MP09, MP10, MP18 to MP22b) and the reports under `poc/gpu/mesh/lib/`.
New files: `parity_g3.csv` (123 rows, original columns plus `status_parity`, `exact_basis`, `status_exact`, `exact_route_pw`, `evidence_level`, `evidence_g3`, `change_kind`), `parity_g3_changelog.md`, `build_parity_g3.py` (override table: the only place a status changes), `parity_g3_compute.py`, `parity_g3_compute_output.txt`. `parity.csv` and `PARITY.md` are untouched. No GPU work, no library built, nothing committed.

## Rules used

1. A status moves up only with a tested result (file and section cited in the changelog); down where a tested result contradicts the documented claim. Library routes outside the six MP06 libraries count only if MP08b tested them (polypartition, MIT).
2. **View A (plain status)**: same definitions as PARITY.md. A function that works but is not bit-exact stays `covered`; it moves down only if tested results show it wrong or refused on cases it should handle (Manifold Minkowski, Manifold volume, Boost is_valid).
3. **View B (user's fidelity decision of 2026-11-14)**: a row with `needs_exactness` yes or maybe counts as `covered` (in "without") only if the covering route is exact on the tested corpus (`exact_basis` = `exact-tested`; numeric measures pass at no WRONG and at most 4 ulp). Rows with `no` need nothing. Rows covered but never tested for exactness, or whose route is only tested in pieces, become `covered-exactness-unproven` (CU): not in "without", still in "with". Rows where every tested route is inexact become `write-ourselves` if MP07/MP08a/MP08b sized an exact route, otherwise `unsized-exact-route` (counted nowhere, like infeasible).
4. Formulas as PARITY.md: without = C/N; with = (C+P+W)/N (B adds CU to "with"). The baseline is recomputed first and reproduces PARITY.md exactly.

## Result (output of `parity_g3_compute.py`, 84 counted rows)

| | N | without, A | with, A | without, B | with, B |
|---|---|---|---|---|---|
| 2D | 36 | **44.4%** (16) | **86.1%** (31) | **13.9%** (5) | **77.8%** (28) |
| 3D | 44 | **20.5%** (9) | **88.6%** (39) | **13.6%** (6) | **88.6%** (39) |
| 2.5D | 4 | 50.0% | 100% | 0.0% | 100% |
| 3D + 2.5D | 48 | 22.9% | 89.6% | 12.5% | 89.6% |

Baseline (MP06): 2D 44.4 / 86.1, 3D 25.0 / 88.6. Summed own-code estimates (partial + write-ourselves, person-weeks): 2D 23-27 (was 34), 3D 61 (was 55); 2.5D 2. Row 29 has no estimate.

Bars: **no bar is met in either view, in either dimension.** A: 2D 44.4 / 86.1, 3D 20.5 / 88.6. B: 2D 13.9 / 77.8, 3D 13.6 / 88.6.

Sensitivities (same script): counting the rows whose exactness is only partly established as exact (the three 3D booleans, pending the MP24 assembled pipeline, and 2D offset) gives B without 2D 16.7%, 3D 20.5%. Treating only `needs_exactness = yes` (not `maybe`) as needing exactness gives B without 2D 30.6%, 3D 13.6%. Neither reaches 70%.

## What moved, and why the headline hardly moved

Two up (Y-monotone partition W to partial, Hertel-Mehlhorn W to covered via polypartition), three down (Volume, Minkowski sum 3D, IsValid 2D, each covered to partial), five estimate-only changes (hull 3D 1 to 3, Greene 3 to 1-2, alpha shape 3 to 2-3, point visibility 8 to 2-3, repair 1 to 2). In B, four more rows (Area 2D, Union/Intersection/Difference 2D) leave `covered` because every tested library is inexact. Details and sources: `parity_g3_changelog.md`.
MP07/MP08b mostly show that a route exists with our code (S1 to M3), which is `partial` or `write-ourselves` by the PARITY definition, so they change the effort estimates and the "with" confidence, not the "without" number. The 3D "without" figure fell because two covered rows were contradicted.

## Still the big gaps

1. **Straight skeleton family** (3 skeleton rows, extrude-skeleton, 3 roofs) plus two medial-axis rows: still `infeasible-in-product` or `unverified`. MP08b found permissive candidates (StraightSkel MIT, a kendzi port BSD-3) but could not build them, so none is scored; 4-6 pw is a guess, 16 pw from scratch. Not upgraded.
2. **3D alpha wrapping**: no permissive implementation; own 14-20 pw, sizing unverified.
3. **2D booleans under the fidelity requirement**: Clipper2, Boost and Manifold CrossSection each fail the exact oracle on some rows (3-6 of 206 for Clipper2/Boost, 41-54 plus 37 refused for Manifold); no exact route is sized anywhere (MP17 integer route hit its kill criterion for 3D cost). This is the largest new hole in B.
4. **3D "covered" rows that are one library**: booleans rest on patched Geogram (our 126-line patch plus MP22, validator, re-centring), tested in pieces on corpora of contact families and the G1 corpus, on libc++/clang and libstdc++/gcc 13 only; the assembled pipeline has not run (MP24 registered).
5. **Exact solid predicates and measures in 3D** (intersects, covers, planarity, distance): the MP09 filter and MP10 pair emission exist but no row-level function was built or tested.

## Unverified or untested (stated plainly)

- 12 rows count as `covered` in A with no tested evidence at all for exactness (listed in the script output): Distance 2D, Covers 2D, IsSimple, Centroid 2D, Length 2D, Simplify 2D (MP08a explicitly did not cover them), constrained Delaunay and CG_Triangulate (Geogram CDT_2d never tested; the tested CDT is MPL-2.0 and not counted), plus the three 3D booleans and offset, tested only partly.
- 31 Boost.Geometry parity cells from MP06 are still unread (MP08a says so). Primitives, envelope 2D, transforms, line substring, orientation, NURBS and the type rows have no test; their status is still the MP06 documentation claim.
- 2D offset is tested only for 14 rectilinear miter cases; SFCGAL offsets with round joins.
- Own-code scores (MP07, MP08b) come from the corpus the code was developed on, not held out; all corpora are exact-integer inputs (non-integer inputs untested); single thread; the MP08b oracles were not mutation-tested.
- The polypartition upgrade rests on a "valid strict-convex partition" check, not on identity with CGAL's partition; the 48/12 result under the collinear definition is unresolved.
- Medial axis: Boost.Polygon voronoi prototype is a sanity check only, so both medial-axis rows stay `unverified`.
- Geogram is correct on the corpora only with the MP20 and MP22 patches; MP20 alone has 30 FAIL on Linux (MP22b). Other compilers, standard libraries and Windows are untested.
- Person-week estimates are the reports' (+-2x, S/M/L scale); the Area 2D exact route (1 pw) is by analogy with MP07's exact volume and is not measured. The IsValid 2D duplicate-vertex pre-check has no estimate.
- `needs_exactness` and `gpu_able` are MP06's assessments, not measurements.
