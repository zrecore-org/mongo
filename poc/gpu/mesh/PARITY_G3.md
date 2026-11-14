# G3 parity update (draft for the gate decision; written 2026-11-14)

Gate text (PLAN.md): "G3 (after MP06/07/08a/08b), parity: report coverage % for 3D and for 2D separately, with and without write-ourselves items"; suggested bars 70% without, 90% with.
Inputs: `parity.csv` / `PARITY.md` (MP06, documented-not-tested), then the tested evidence in `poc/gpu/RESULTS.md` (MP07, MP08a, MP08b, MP03, MP04, MP05, MP09, MP10, MP18 to MP22b) and the reports under `poc/gpu/mesh/lib/`.
New files: `parity_g3.csv` (123 rows, original columns plus `status_parity`, `exact_basis`, `bit_exact`, `maybe_decision`, `maybe_reason`, `status_exact` (B under decided maybe, pending MP24 as unproven), `exact_route_pw`, `evidence_level`, `evidence_g3`, `change_kind`), `parity_g3_changelog.md`, `build_parity_g3.py` (override table: the only place a status changes; also holds the `maybe` decisions), `parity_g3_compute.py`, `parity_g3_compute_output.txt`. `parity.csv` and `PARITY.md` are untouched. No GPU work, no library built, nothing committed.

## Rules used

1. A status moves up only with a tested result (file and section cited in the changelog); down where a tested result contradicts the documented claim. Library routes outside the six MP06 libraries count only if MP08b tested them (polypartition, MIT).
2. **View A (plain status)**: same definitions as PARITY.md. A function that works but is not bit-exact stays `covered`; it moves down only if tested results show it wrong or refused on cases it should handle (Manifold Minkowski, Manifold volume, Boost is_valid).
3. **View B (fidelity)**: ONE threshold for booleans, offsets and measures alike. A route meets the requirement only if it stays within 1e-12 of the operation's scale on the tested corpus (the bar MP08a declared in `lib/gap2d/DECLARED.txt`). There is no ulp special case for measures. Bit-exactness is reported separately as a third column (`bit_exact`).
   - **B extends the user's answer.** The user's "I want fidelity" (2026-11-14) was about 3D booleans. B applies it to the 2D rows as well and to the `needs_exactness = maybe` rows below. That extension is an assumption for the user to confirm or narrow, which is why B is reported as a range.
   - Covered rows needing exactness: bar met on the tested corpus -> `C`; bar untested, or pending MP24 -> `CU` (in "with", not in "without"); every tested route outside the bar -> `write-ourselves` if MP07/MP08a/MP08b sized an exact route, otherwise `unsized-exact-route` (UN, counted nowhere).
   - **`maybe` rows** (all listed with a decision and reason in `parity_g3_changelog.md`, last table, and in the csv columns `maybe_decision`, `maybe_reason`): decided yes = predicates and rows whose output topology or sign feeds a decision (distance 2D/3D, area 2D, volume, IsValid, IsSimple, tessellation, alpha shapes, equals, winding); decided no = output-only irrational values or inherently rounded results (area 3D, centroid, length, simplify, rotate, affine, signed distance, closest point). Only A-covered rows change the count: Distance 2D yes, Area 2D yes, Area 3D no, IsSimple yes, Centroid 2D no, Length 2D no, Tessellate yes, Simplify no.
   - **Pending MP24**: the three 3D boolean rows (union, intersection, difference) rest on the assembled patched-Geogram pipeline, which has not run. They are shown counted as meeting the bar and as not.
4. Formulas as PARITY.md: without = C/N; with = (C+P+W)/N (B adds CU to "with"). The baseline is recomputed first and reproduces PARITY.md exactly.

## Result (output of `parity_g3_compute.py`, saved in `parity_g3_compute_output.txt`; 84 counted rows)

**View A (plain):** 2D without 44.4% (16/36), with 86.1% (31/36); 3D without 20.5% (9/44), with 88.6% (39/44); 2.5D 50.0% / 100%; 3D+2.5D 22.9% / 89.6%. Baseline (MP06): 2D 44.4 / 86.1, 3D 25.0 / 88.6.

**View B (fidelity, one 1e-12 threshold):**

| | without: 3D booleans pending MP24 counted as meeting | without: not counted | with write-ourselves (either way)* |
|---|---|---|---|
| 2D, maybe = all no | 33.3% | 33.3% | 77.8% |
| 2D, maybe = decided | 25.0% | 25.0% | 77.8% |
| 2D, maybe = all yes | 16.7% | 16.7% | 77.8% |
| 3D, maybe = all no | 20.5% | 13.6% | 88.6% |
| 3D, maybe = decided | 20.5% | 13.6% | 88.6% |
| 3D, maybe = all yes | 20.5% | 13.6% | 88.6% |

**Headline range for B:** 2D without 16.7% to 33.3%, with 77.8%; 3D without 13.6% to 20.5%, with 88.6%. (2.5D and 3D+2.5D rows are in the output file.) Pending MP24 matters only for 3D. For 3D the `maybe` policy changes nothing because every covered 3D row that decides to need exactness is either bar-meeting or pending.

**Bit-exactness (separate third column):** covered rows tested bit-exact: 2D 5/36 (13.9%), 3D 1/44 (2.3%). Tested NOT bit-exact: Area 2D, the three 2D booleans, Area 3D (1 NEAR of 64), own Centroid 3D (63 of 64 bit-exact). Everything else is untested for bit-exactness.

\* **Footnote on every B "with" figure:** it assumes own code meets the bar. MP07's own-code scores come from the corpus the code was developed on (not held out), and its own centroid missed bit-exactness on 1 of 64 cases. The 2D booleans (UN, 3 rows) are excluded from "with" because no exact route is sized; Area 2D (1 pw) is included by analogy with MP07's exact volume. Unmeasured claims: all "with" write-ourselves rows.

Summed own-code estimates (partial + write-ourselves, person-weeks): 2D 23-27 (was 34), 3D 61 (was 55), 2.5D 2. Row 29 has no estimate.

**Bars (suggested 70% without, 90% with): not met in either view, either dimension, in any scenario.** Best B case: 2D 33.3 / 77.8, 3D 20.5 / 88.6. A: 2D 44.4 / 86.1, 3D 20.5 / 88.6.

## What moved, and why the headline hardly moved

Two up (Y-monotone partition W to partial, Hertel-Mehlhorn W to covered via polypartition), three down (Volume, Minkowski sum 3D, IsValid 2D, each covered to partial), five estimate-only changes (hull 3D 1 to 3, Greene 3 to 1-2, alpha shape 3 to 2-3, point visibility 8 to 2-3, repair 1 to 2). In B, four more rows (Area 2D, Union/Intersection/Difference 2D) leave `covered` because every tested library is inexact. Details and sources: `parity_g3_changelog.md`.
MP07/MP08b mostly show that a route exists with our code (S1 to M3), which is `partial` or `write-ourselves` by the PARITY definition, so they change the effort estimates and the "with" confidence, not the "without" number. The 3D "without" figure fell because two covered rows were contradicted.

## Still the big gaps

1. **Straight skeleton family** (3 skeleton rows, extrude-skeleton, 3 roofs) plus two medial-axis rows: still `infeasible-in-product` or `unverified`. MP08b found permissive candidates (StraightSkel MIT, a kendzi port BSD-3) but could not build them, so none is scored; 4-6 pw is a guess, 16 pw from scratch. Not upgraded.
2. **3D alpha wrapping**: no permissive implementation; own 14-20 pw, sizing unverified.
3. **2D booleans under the fidelity requirement**: Clipper2, Boost and Manifold CrossSection each fail the exact oracle on some rows (3-6 of 206 for Clipper2/Boost, 41-54 plus 37 refused for Manifold); no exact route is sized anywhere (MP17 integer route hit its kill criterion for 3D cost). This is the largest new hole in B.
4. **3D "covered" rows that are one library (pending MP24)**: booleans rest on patched Geogram (our 126-line patch plus MP22, validator, re-centring), tested in pieces on corpora of contact families and the G1 corpus, on libc++/clang and libstdc++/gcc 13 only; the assembled pipeline has not run (MP24 registered).
5. **Exact solid predicates and measures in 3D** (intersects, covers, planarity, distance): the MP09 filter and MP10 pair emission exist but no row-level function was built or tested.

## Options for the G3 decision (listed neutrally; the user decides, or asks legal)

The plan leaves the subset to the user. Facts for each option, no recommendation:
- **Option 1, full SFCGAL surface as scope.** Includes the straight-skeleton family, medial axis and 3D alpha wrapping (10 rows, no scored route; MP08b guesses 4-6 pw for a skeleton integration, 14-20 pw for 3D alpha wrap, 16 pw from scratch). Neither bar is reachable on the current evidence.
- **Option 2, only rows whose route is tested.** The rows in view B counted as `C` (2D 6 to 12 of 36, 3D 6 to 9 of 44 depending on the scenario, from the output file). Small, and several 3D rows are primitives and bounding boxes.
- **Option 3, rows covered or with a sized own route (the "with" set).** 2D 28 of 36 and 3D 39 of 44 under B, at about 23-27 pw (2D) and 61 pw (3D) of own code, assuming that own code meets the 1e-12 bar (unproven, footnote above).
- **Option 4, relax or narrow the fidelity requirement** (for example to 3D booleans only, which is what the user answered). This moves the B range toward view A.
- **Option 5, ask legal first.** Components excluded for licence reasons would change the available routes: CDT is MPL-2.0 (exact CDT caps for extrusion 64/64 and the Delaunay property, MP07/MP08a), the Cherchi/Attene indirect-predicate headers are LGPL-3.0 (not scored), and Geogram's bundled TetGen/Triangle/HLBFGS are switched off. No number here assumes any of them.

## Unverified or untested (stated plainly)

- 11 rows count as `covered` in A with the 1e-12 bar untested or pending (listed in the script output): Distance 2D, Covers 2D, IsSimple, Centroid 2D, Length 2D, Simplify 2D (MP08a explicitly did not cover them), constrained Delaunay and CG_Triangulate (Geogram CDT_2d never tested; the tested CDT is MPL-2.0 and not counted), and the three 3D booleans (pending MP24: the assembled pipeline has not run).
- 31 Boost.Geometry parity cells from MP06 are still unread (MP08a says so). Primitives, envelope 2D, transforms, line substring, orientation, NURBS and the type rows have no test; their status is still the MP06 documentation claim.
- 2D offset is tested only for 14 rectilinear miter cases; SFCGAL offsets with round joins. B counts it as meeting the bar on that corpus only; this is the thinnest `C` in B (without it 2D B-without is lower by 1 row, 2.8 points).
- Own-code scores (MP07, MP08b) come from the corpus the code was developed on, not held out; all corpora are exact-integer inputs (non-integer inputs untested); single thread; the MP08b oracles were not mutation-tested.
- The polypartition upgrade rests on a "valid strict-convex partition" check, not on identity with CGAL's partition; the 48/12 result under the collinear definition is unresolved.
- Medial axis: Boost.Polygon voronoi prototype is a sanity check only, so both medial-axis rows stay `unverified`.
- Geogram is correct on the corpora only with the MP20 and MP22 patches; MP20 alone has 30 FAIL on Linux (MP22b). Other compilers, standard libraries and Windows are untested.
- Person-week estimates are the reports' (+-2x, S/M/L scale); the Area 2D exact route (1 pw) is by analogy with MP07's exact volume and is not measured. The IsValid 2D duplicate-vertex pre-check has no estimate.
- `needs_exactness` and `gpu_able` are MP06's assessments, not measurements.
