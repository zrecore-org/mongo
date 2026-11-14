#!/usr/bin/env python3
"""Builds parity_g3.csv and parity_g3_changelog.md from parity.csv (read only) plus the override table below.
Every override cites the tested result that backs it. Run: python3 build_parity_g3.py (from poc/gpu/mesh)."""
import csv, re

SRC, OUT, LOG = "parity.csv", "parity_g3.csv", "parity_g3_changelog.md"
R = "poc/gpu/RESULTS.md"
G3D = "poc/gpu/mesh/lib/gap3d/summary.txt + REPORT.md (RESULTS.md 'MP07: gap-fill spikes')"
G2D = "poc/gpu/mesh/lib/gap2d/results/summary.txt + REPORT.md (RESULTS.md 'MP08a')"
H = "poc/gpu/mesh/lib/hard2d3d/summary_final.txt + REPORT.md (RESULTS.md 'MP08b')"
GEO = ("RESULTS.md 'MP20' (set 4: 3150 rows, 0 FAIL), 'MP22b Linux result' (set 5: 3300 rows, 0 FAIL, 0 REFUSED on M1, "
       "Linux x86_64, Linux aarch64), 'MP21b' (validator), 'Re-centring experiment' (6/6 PASS), MP04 (exact for 2^-40 slivers)")

# idx -> dict(status=A status, est=new estimate text, basis=exact_basis, b_est=sized exact route in pw (text) for demoted rows,
#             ev=evidence, kind=up/down/est/evidence, basis_note, est_basis)
O = {
 20: dict(basis="exact-tested", kind="evidence", ev=f"Boost intersects 192/192 CORRECT on the 192-pair exact-integer corpus. {G2D}"),
 21: dict(kind="evidence", ev="MP09 exact-sign filter (RESULTS.md 'MP09', 1.35e9 device cases, 0 wrong definite signs) and MP10 box-pair emission exist, but no row-level 3D intersects function was built or tested, so the row is unchanged."),
 24: dict(basis="inexact-tested", b_est="1", kind="evidence",
          ev=f"Boost and Clipper2 area 205 correct / 1 WRONG (sliver30), Manifold 169 correct + 37 refused (coordinates >= 2^40). {G2D}. Functionally usable (A unchanged); not exact on the corpus (B).",
          est_basis="B route = exact integer accumulation; estimate S1 taken BY ANALOGY from MP07 exact volume/centroid S1 (not measured for 2D)"),
 25: dict(basis="exact-tested", kind="evidence", ev=f"Manifold SurfaceArea 63 correct / 1 NEAR (1 ulp) / 0 wrong on 64 cases incl. 2^30 offsets. {G3D}"),
 26: dict(status="partial", est="1", kind="down",
          ev=f"Manifold volume 54 correct / 5 NEAR / 5 WRONG (max 2.68e8 ulp at 2^30 offsets); Geogram volume is a plain double sum (MP04 probe, untested). Own exact cpp_int volume 64/0/0. {G3D}",
          est_basis="MP07 REPORT.md effort line: exact volume/centroid S1 (25-line cpp_int version)"),
 29: dict(status="partial", est="", kind="down",
          ev=f"Boost is_valid 206/206 on valid inputs but answers valid for 2 of 20 invalid inputs (210_inv_dup_vertex, 220_inv_closed_by_repeat). {G2D}",
          est_basis="no MP report sizes the duplicate-vertex pre-check (glue); left unestimated, not invented"),
 36: dict(kind="evidence", ev=f"Own centroid 63 correct / 1 NEAR (1 ulp); Manifold has no centroid API (64 NOAPI). Estimate S1 unchanged. {G3D}"),
 40: dict(basis="not-needed", kind="evidence", ev=f"Manifold bbox 64/64 exact. {G3D}"),
 42: dict(basis="inexact-tested", b_est=None, kind="evidence", ev=f"Union 2D: Clipper2 188 ok / 4 invalid output, Boost 187 ok / 5 wrong (2^40 offsets), Manifold 112 ok / 41 wrong / 37 refused, of 206. No library has 0 wrong. {G2D}"),
 43: dict(basis="inexact-tested", b_est=None, kind="evidence", ev=f"Intersection 2D: Clipper2 189 ok / 3 wrong, Boost 186 / 6 wrong, Manifold 101 / 54 wrong / 37 refused, of 206. {G2D}"),
 44: dict(basis="inexact-tested", b_est=None, kind="evidence", ev=f"Difference 2D: Clipper2 189 ok / 3 invalid output, Boost 187 / 5 wrong, Manifold 100 / 55 wrong / 37 refused, of 206. {G2D}"),
 45: dict(basis="exact-thin", kind="evidence", ev=f"Primary route is patched Geogram + validator + re-centring (user decision 2026-11-14, PLAN.md). Pieces tested: {GEO}. Manifold fails 15 of 72 valid G1 rows (MP03). Assembled pipeline NOT yet run (MP24 registered), so exactness is counted only in the sensitivity figure."),
 46: dict(basis="exact-thin", kind="evidence", ev="Same as row 45 (intersection)."),
 47: dict(basis="exact-thin", kind="evidence", ev="Same as row 45 (difference)."),
 48: dict(kind="evidence", ev="MP03: Manifold refused open box, unwelded mesh, zero-area and duplicated triangle (NotManifold) but accepted a non-manifold-edge mesh and a self-intersecting mesh; Geogram refused nothing (14 of 21 accepted, 1 FAIL). MP21b validator: 18 REFUSED / 3 PASS / 0 FAIL, but no self-intersection check. Still partial, estimate unchanged (RESULTS.md MP03, MP04, MP21b)."),
 50: dict(basis="exact-tested", kind="evidence", ev=f"Boost convex hull 206/206 CORRECT (Manifold 169 + 37 refused). {G2D}"),
 51: dict(est="3", kind="est",
          ev=f"Manifold Hull of pairwise sums 54 correct / 2 WRONG (2^-30 slivers collapsed by its tolerance), so not exact. {G3D}",
          est_basis="MP07 REPORT.md effort line: exact convex hull M3 (3 pw); replaces MP06's 1"),
 52: dict(kind="evidence", ev=f"Extrude: Manifold 56/64 (Z-only, wrong on collinear profiles), own earcut caps 58/64, own CDT caps 64/64 (CDT is MPL-2.0, not counted permissive; Geogram BDEL2d unscored). S1 unchanged. {G3D}"),
 61: dict(status="partial", est="3", kind="down",
          ev=f"Raw Manifold MinkowskiSum is Union(A, hull): 11 correct / 41 WRONG / 4 NOAPI of 56 (correct only when 0 is in B); with B translated 50/2 wrong; Hull(pairwise sums) 54/2 wrong (slivers); non-convex A raw 5/24, translated 24/24. {G3D}",
          est_basis="MP07 effort line: exact convex hull M3 (route is Hull of pairwise sums); non-convex B untested"),
 62: dict(basis="exact-thin", kind="evidence", ev=f"Offset miter, 14 rectilinear cases: Boost, Clipper2, Manifold all 14/14 exact. Round joins, inward and non-rectilinear offset are NOT covered by the corpus (SFCGAL's offset uses round joins). {G2D}"),
 64: dict(basis="exact-tested", kind="evidence", ev=f"earcut 206/206 correct (2D with holes) and 58/58 on planar 3D polygons; earcut does not refuse invalid input (9 of 20 nonsense). {G2D}; {G3D}"),
 65: dict(kind="evidence", ev=f"Tessellate planar 3D: earcut and CDT 58/58 each; S1 glue (40 lines) unchanged. {G3D}"),
 66: dict(kind="evidence", ev=f"Geogram CDT_2d was NOT tested. CDT (MPL-2.0, not counted) has 0 Delaunay violations in 909 edges, earcut 216, Manifold 59. {G2D}"),
 67: dict(kind="evidence", ev="Same as row 66: Geogram route untested; earcut is not Delaunay (MP08a)."),
 68: dict(status="partial", est="1-2", kind="up",
          ev=f"polypartition (MIT, licence read in MP08b) monotone partition 56 correct / 4 WRONG (polygons with holes return a non-monotone bridged ring). {H}",
          est_basis="MP08b verdict 5: monotone via polypartition plus a hole fix, 1-2 pw; replaces MP06's 3"),
 69: dict(status="covered-by-permissive", est="", kind="up", basis="exact-tested",
          ev=f"polypartition (MIT) Hertel-Mehlhorn 60/0 on 60 exact-integer cases under strict convexity (48/12 if collinear merges count as removable: definition difference); own HM on earcut also 60/0 (1 pw). Library is outside the six audited in MP06. Output is a valid partition, not proven identical to CGAL's. {H}",
          est_basis="no own code needed; MP08b own alternative was 1 pw"),
 70: dict(est="1-2", kind="est", ev=f"Greene 'adds 1-2 pw' on top of the monotone partition (row 68). {H}", est_basis="MP08b verdict 5; replaces MP06's 3; conditional on row 68"),
 75: dict(kind="evidence", ev=f"Boost.Polygon voronoi prototype is a sanity check only (clearance 60/60, topology 59/60; no exact oracle; true medial axis, not SFCGAL's skeleton-derived one, UNVERIFIED). Row stays unverified. {H}"),
 77: dict(est="2-3", kind="est", ev=f"Own alpha shape on CDT 60/0 (mutants caught). {H}", est_basis="MP08b verdict 3: own, 2-3 pw; replaces MP06's 3"),
 79: dict(kind="evidence", ev=f"No permissive 3D alpha wrap found; own 14-20 pw (UNVERIFIED sizing); Geogram BDEL 3D Delaunay 60/0 is only a building block. Stays infeasible. {H}"),
 80: dict(est="2-3", kind="est", ev=f"Own exact point-visibility sweep 60/0 (mutants caught). {H}", est_basis="MP08b verdict 4: 2-3 pw; replaces MP06's 8"),
 82: dict(kind="evidence", ev="Simplify was explicitly NOT covered by MP08a; documented-only."),
 84: dict(est="2", kind="est",
          ev=f"Clipper2 Union as repair: 35 exact / 24 rounded (max rel. area error 4.3e-4) / 1 wrong (even-odd, two rings sharing an edge). Not exact. {H}",
          est_basis="MP08b verdict 6: Clipper2 plus an overlap-merge post-pass, 2 pw; replaces MP06's 1"),
 111: dict(kind="evidence", ev="MP05: Embree is float32; 125 of 400 points 1e-8 off the surface classified differently from the double reference (RESULTS.md 'MP05'). Excluded group; no change."),
 112: dict(kind="evidence", ev=f"Own closest point 53 correct / 5 NEAR / 6 WRONG (4.22e8 ulp on slivers); Embree rtcPointQuery unchecked. Excluded group; no change. {G3D}"),
 113: dict(kind="evidence", ev="MP10 broad phase (GPU LBVH + box pairs, 4 platforms, equal to the exact CPU reference) tested; no narrow phase. Excluded group; no change (RESULTS.md 'MP10')."),
 120: dict(kind="evidence", ev="Embree is not exact (MP05). Geogram MeshFacetsAABB::contains untested. Excluded group; no change."),
 122: dict(kind="evidence", ev="Own CPU BVH pair list equals brute force 31/31 (MP05); GPU LBVH pair lists equal the exact CPU reference on M1, lavapipe x2, T4 (MP10). Excluded group; no change."),
}

rows = list(csv.DictReader(open(SRC)))
base_cols = list(rows[0].keys())
new_cols = ["status_parity", "exact_basis", "status_exact", "exact_route_pw", "evidence_level", "evidence_g3", "change_kind"]
out, changes = [], []
for i, r in enumerate(rows):
    o = O.get(i, {})
    old_status, old_est = r["status"], r["own_code_estimate"]
    st = o.get("status", old_status)
    est = o["est"] if "est" in o else old_est
    if st in ("covered-by-permissive",):
        basis = o.get("basis", "not-needed" if r["needs_exactness"] == "no" else "untested")
    else:
        basis = "n/a"
    if st == "covered-by-permissive":
        if basis in ("exact-tested", "not-needed"): se = "covered-by-permissive"
        elif basis in ("untested", "exact-thin"): se = "covered-exactness-unproven"
        else: se = "write-ourselves" if o.get("b_est") else "unsized-exact-route"
    else:
        se = st
    kind = o.get("kind", "none")
    level = "tested" if o.get("ev") and kind != "none" and i not in (82, 21, 66, 67, 75) else "documented"
    if i in (82, 66, 67): level = "documented"
    if st != old_status and kind not in ("up", "down"): raise SystemExit("status changed without kind at %d" % i)
    row = dict(r)
    row["status"], row["own_code_estimate"] = st, est
    row.update(status_parity=old_status, exact_basis=basis, status_exact=se,
               exact_route_pw=o.get("b_est") or "", evidence_level=level, evidence_g3=o.get("ev", ""), change_kind=kind)
    out.append(row)
    if st != old_status or est != old_est or (se != st and se != "covered-exactness-unproven" and basis not in ("n/a",)) :
        changes.append((i, r, old_status, st, old_est, est, se, basis, o))
with open(OUT, "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=base_cols + new_cols); w.writeheader(); w.writerows(out)

# changelog
status_changes = [c for c in changes if c[2] != c[3]]
est_only = [c for c in changes if c[2] == c[3] and c[4] != c[5]]
exact_only = [c for c in changes if c[2] == c[3] and c[4] == c[5]]
ups = [c for c in status_changes if O[c[0]]["kind"] == "up"]
downs = [c for c in status_changes if O[c[0]]["kind"] == "down"]
def ref(c): return c[8].get("ev", "")
L = ["# G3 parity changelog (relative to parity.csv, MP06)", "",
     "Generated by `build_parity_g3.py` from the override table in that script; nothing here was run on a GPU and no library was built for this update. "
     "Rule applied: a status moves up only if a tested result backs it, down where a tested result contradicts the documented claim; documentation alone never upgrades. "
     "Evidence paths are relative to the repository root. `row` is the 0-based data-row index in parity.csv (header excluded).", "",
     f"Status changes: **{len(ups)} up, {len(downs)} down**. Estimate-only changes: {len(est_only)}. Rows whose status is unchanged but whose status under the fidelity requirement (B) differs from the plain one: {len(exact_only)} (second table).", "",
     "## Status changes", "",
     "| row | operation | dim | old status | new status | old est (pw) | new est (pw) | evidence | person-week basis |", "|---|---|---|---|---|---|---|---|---|"]
for c in ups + downs:
    i, r = c[0], c[1]
    L.append(f"| {i} | {r['operation']} | {r['dim']} | {c[2]} | {c[3]} | {c[4] or '-'} | {c[5] or '-'} | {ref(c)} | {c[8].get('est_basis','')} |")
L += ["", "## Estimate-only changes (status unchanged)", "",
      "| row | operation | dim | status | old est (pw) | new est (pw) | evidence | person-week basis |", "|---|---|---|---|---|---|---|---|"]
for c in est_only:
    i, r = c[0], c[1]
    L.append(f"| {i} | {r['operation']} | {r['dim']} | {c[3]} | {c[4] or '-'} | {c[5] or '-'} | {ref(c)} | {c[8].get('est_basis','')} |")
L += ["", "## Rows that are `covered` in the plain status but not exact on the tested corpus (fidelity view only; plain status unchanged)", "",
      "| row | operation | dim | status | B status | exact basis | evidence | basis for exact-route estimate |", "|---|---|---|---|---|---|---|---|"]
for c in exact_only:
    i, r = c[0], c[1]
    L.append(f"| {i} | {r['operation']} | {r['dim']} | {c[3]} | {c[6]} | {c[7]} | {ref(c)} | {c[8].get('est_basis') or 'no sized exact route in MP07/MP08a/MP08b (open)'} |")
L += ["", "## Evidence added without any status or estimate change", "",
      "| row | operation | what the tested result says |", "|---|---|---|"]
for i in sorted(O):
    if all(i != c[0] for c in changes):
        L.append(f"| {i} | {rows[i]['operation']} | {O[i]['ev']} |")
open(LOG, "w").write("\n".join(L) + "\n")
print("rows", len(out), "up", len(ups), "down", len(downs), "est-only", len(est_only), "exact-only", len(exact_only))
