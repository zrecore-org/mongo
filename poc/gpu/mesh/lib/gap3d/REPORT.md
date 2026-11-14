# MP07: gap-fill spikes for 3D constructive, distance and measure functions (sub-agent run; the verdict table is `summary.txt`, matching the sub-agent's report; bars in `DECLARED.txt`)

Corpus `corpus3d/` (460 exact-integer cases; hashes in `EXPECTED_SHA256.txt`). Manifold 3.5.5 (pinned build): has Extrude (Z axis only), Revolve, Hull, MinkowskiSum/Difference, Slice, MinGap, area/volume/bbox; **no centroid, no closest point, no point-distance query**. Checker: independent cpp_int oracles; 7 planted mutants all fail the selftest; negative controls on revolve flagged 65 of 66 and 57 of 66.
Verdict columns: CORRECT / NEAR (<= 4 ulp) / WRONG / REFUSED / NO-API.
| function | route | correct / near / wrong / refused / no-API |
|---|---|---|
| extrude (64) | Manifold | 56 / 0 / 8 (shear vector rounded in 4 of 33; flat sheet for 4 of 4 collinear profiles) |
| | own earcut caps | 58 / 0 / 6 (T-junctions at collinear hole corners) |
| | own CDT caps | 64 / 0 / 0 |
| revolve (66) | Manifold | 63 / 0 / 3 (exact on 54 incl. through-axis, holes, 2^-30 slivers; within 16 ulp on 8; 3 of 4 zero-area profiles give a non-empty mesh) |
| tessellate planar 3D (58) | earcut, CDT | 58 / 0 / 0 each |
| convex Minkowski (56) | raw `MinkowskiSum` | 11 / 0 / 41 (+4 no-API): it is Union(A, hull), correct only when 0 is in B |
| | with B translated | 50 / 0 / 2 |
| | Hull(pairwise sums) | 54 / 0 / 2 (the 2 are 2^-30 slivers collapsed by Manifold's tolerance) |
| non-convex A, box B (24) | raw / translated | 5 / 24 correct |
| distance mesh-mesh (64) | MinGap (solid distance only); own double BVH | 60 / 4 / 0 each |
| distance point-mesh (64) | own | 61 / 1 / 2 (slivers cost 8 ulp) |
| volume (64) | Manifold / own | 54 / 5 / 5 (2.7e8 ulp at 2^30 offsets) / 64 / 0 / 0 |
| area, bbox | Manifold / own | area 63 / 1 / 0 both; bbox exact both |
| centroid | own | 63 / 1 (Manifold: no API) |
| closest point | own | 53 / 5 / 6 (Manifold: no API) |
Speed (one thread, median of 5, ms): extrude 1e5 triangles Manifold 1145 vs own 1.85; tessellate 1e5 vertices earcut 6.4, CDT 67; revolve 41; Minkowski of 98,596 sums 6.9, hull 4.8; own mesh-mesh distance 4.3 (BVH build 35); `MinGap` 601 at searchLength 120 and **did not finish in 8 minutes at 5000**; own volume/area/centroid 0.26/0.28/0.64 vs Manifold volume/area 0.25/0.22.
Route: Manifold for revolve and measures; earcut/CDT plus glue for tessellation; own code for extrude caps, distance, closest point, centroid and exact volume.
Effort (person-weeks; S=1, M=3; basis: this spike, about 980 lines): extrude S1 (45-line prototype), revolve S1, tessellate S1 (40-line glue), exact convex hull M3 (a 60-line verifier exists; a constructive exact hull is about 500-800 lines), filtered-exact distance and closest point M3 (exact oracle 70 lines, double BVH 130, about 600 total), exact volume/centroid S1 (25-line cpp_int version).
## Disclosed deviations and limits (sub-agent's, kept as written)
The generator had an octahedron orientation bug found after the first run; fixed and re-run (v1/v2/v3 hash files kept). The Minkowski criterion was relaxed to a three-stage exact proof after seeing results. **The own-code scores come from the same corpus the code was developed on** (not held out). Speed inputs are unhashed. Not covered: non-convex B in Minkowski, non-integer inputs, multi-thread and cross-platform runs.
## Bars not met
No route is exact on every class (failures per function above); no library has a centroid or closest-point route; own centroid missed bit-exactness in 1 of 64 cases.
