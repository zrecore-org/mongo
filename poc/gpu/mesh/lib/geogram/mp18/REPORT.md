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

## Held-out test of guard v1 (sub-agent run, TSV counts re-checked by me; `results_heldout_v1101.tsv`, `corpus/touch/gen_touch_heldout.cpp`, Geogram v1.10.1 only)
New generator, new seeds and placements; guard unchanged. 1137 rows (379 pairs): unguarded 999 PASS / 138 FAIL; **guarded 1122 PASS / 9 FAIL / 6 REFUSED.** The guard repaired 129 of the 138 failing rows; the 63 passing valid cases stayed byte-identical.
| class | rows | unguarded FAIL | guarded FAIL / REFUSED |
|---|---|---|---|
| nested, 1 shared vertex | 24 | 15 | 0 / 0 |
| nested, shared edge; shared face | 24 + 24 | 0 | 0 / 0 |
| non-convex L/U prism, vertex-vertex | 54 | 24 | 3 / 0 |
| three components, vertex-vertex | 27 | 9 | 0 / 0 |
| random vertex-vertex (permutations + shears) | 960 | 78 | 6 / 0 |
| two contact vertices | 24 | 12 | 0 / 6 |
Silent wrong answers remain on 3 pairs (`held_nonconvex_vv_U_reflex_tet_m2`, `held_randvv_te5_bx3m_146`, `held_randvv_te5_oc3m_304`): the contact trigger fired but the post-check found no mismatch, so the miss is in the post-check's choice of test points. Two pairs are refused cleanly (`twovertex_tet_bridge_m0/_m1`). (The sub-agent's summary said "12 refused or failed rows"; the TSV has 9 + 6 = 15.)
**Verdict: guard v1 fails the held-out test.** It removes most of the failure but leaves silent wrong results, which the product cannot accept. Unguarded failures are all vertex contact (the root cause is local to it). The non-convex "one contact" expectations are hand-derived (the generator checks only a separating plane); the failing cases were not independently re-verified.

## MP18b: differential guard v2 on set 3 (sub-agent run, TSV counts re-checked by me; `results_set3_v1101.tsv`, frozen hashes in `FROZEN_GUARD2_SHA256.txt`, set 3 hashes in `corpus/touch/EXPECTED_SHA256_set3.txt`)
Guard v2 and the set 3 generator were frozen and hashed before the run (`geogram_guard2.cpp` sha256 dbdce4e0...a212 matches the recorded value). Set 3: 534 pairs, 1602 rows (nested, non-convex, three-component, two-point, three-point, icosphere contact), Geogram v1.10.1 only; curved rows judged by the point tests.
Totals: unguarded 1410 PASS / 192 FAIL; **guard v2 1260 PASS / 21 silent FAIL / 321 REFUSED.**
| class | unguarded FAIL | v2 refused | v2 silent FAIL |
|---|---|---|---|
| nested vertex | 78 of 180 | 153 | 0 |
| non-convex vertex-vertex | 21 of 270 | 54 | 0 |
| random vertex-vertex | 24 of 300 | 42 | 0 |
| three-component | 3 of 180 | 15 | 0 |
| three-point contact | 24 of 90 | 36 | 0 |
| sphere (reflect) | 3 of 246 | 3 | 0 |
| two-point contact | 39 of 120 | 18 | **21** |
| nested edge, nested face, sphere-box | 0 | 0 | 0 |
Other bars: the 63 passing valid cases stay byte-identical (the three `box_vertex_touch` rows go FAIL to REFUSED); median added time on non-touching pairs +0.03%. Mutants: "accept on 2 of 3 agreeing" caught (171 silent FAIL); **"skip the permutation run" not caught** (same 21 silent FAIL, fewer refusals), so the third vertex order adds no correctness on this evidence.
**Kill rule (registered in PLAN.md) applies on both counts:** (1) 21 silent FAIL rows = 7 two-point-contact pairs where all three vertex orders agree on the same wrong answer (24 triangles instead of 28; seen already in the held-out `tet_bridge` pairs), so order independence cannot detect that failure; (2) refuse rate on vertex-contact rows 321 of 1386 = 23.2%, above the 15% limit (the 153 nested-vertex refusals dominate).
Caveats: set 3 uses the same trigger logic as v2 with new seeds and sizes; area is checked as an exact sum of squared doubled triangle areas (the true area is irrational), the sub-agent's reading of "exact"; `main` not run on set 3.
**Conclusion: Geogram cannot be made safe by this kind of wrapper.** It fails silently in two-point contact, and refuses too often elsewhere. MP18 closes as a negative result; its usable output is the characterisation (vertex contact is the only failing contact class, root cause local to the symbolic-perturbation inside/outside ray test).
