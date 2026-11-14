# MP18: Geogram corner-contact failure, scope and guard

Written by the main session from the sub-agent's report. Counts below were re-checked by me against the result TSVs (`results_touch_*.tsv`);
the root-cause statements are the sub-agent's and are marked. Plan and bars: `../../../PLAN.md` section MP18 (registered before any run).

## Step 1: scope (measured, TSV counts re-checked)
80 `touch_*` pairs, 240 rows (all three operations), exact expected volumes, 10 contact classes. Geogram v1.10.1 and `main` e6f8667a give identical verdicts: 216 PASS, 24 FAIL.
All 24 failures are vertex-vertex contact; every other contact class (vertex-on-face, vertex-on-edge, edge-edge at a point, edge-on-face, edge-edge along a segment, face-face) passes.
A sweep of 2722 placements (8166 rows) found 165 FAIL, all vertex-vertex (55 of 343 placements); the wrong result is one whole operand.

## Step 2: root cause (sub-agent, verified for these cases only)
Components sharing only a vertex are classified by a +z ray from one representative vertex; when that vertex is the contact vertex, the symbolic perturbation in `orient_3d_SOS` decides inside/outside.
An instrumented copy of the source (library unpatched) showed operand A given inclusion bit 2 (wrongly inside B). Reversing the vertex index order repaired all 24 vertex-vertex rows. Triangle-order permutation did not
move the representative vertex (falsified). Which vertex is picked is unverified; the internal CDT files were not read.

## Step 3: guard `geogram_guard` (exact predicates)
Trigger: vertex coincidence or exact bounding-box contact; exact post-check at the contact points; on failure retry with reversed vertex order; otherwise refuse.
| bar | result |
|---|---|
| 0 FAIL on `touch_*` | met: 240/240 (sweep 8166/8166; also on `main`) |
| 0 verdict changes on the existing 93 rows | **as written, not met**: 3 verdicts change, all `box_vertex_touch` FAIL to PASS, the defect being repaired. The bar contradicts "0 FAIL on touch_*"; read as "no regression" (no row changes in the other direction) |
| byte-identical on the 63 passing valid cases | met: 63/63 |
| <= 10% added time on non-touching pairs | met: median +0.47% |
| guard mutants caught | met: 3/3 (skipped axis, off-by-one, post-check off) |
Limits stated by the sub-agent: external contact between convex solids; the guard was developed against this family (two sheared pairs failed until facet-derived test points were added), so it is **not held out**; internal contact and non-convex solids untested at this stage; the 6 huge-offset failures are a different defect and unchanged.
Kill criterion (failure reaches generic overlaps): does not apply.
