# MP08b: hard 2D/3D operations, time-boxed (sub-agent run; per-function scores in `summary_final.txt`; bars in `PLAN.md` MP08b; hashes in `corpus_hard/EXPECTED_SHA256*.txt` recorded before any prototype ran)

60 exact-integer cases per prototype; oracles use cpp_int/int128 only. Mutants are of the code under test; the oracles themselves were not mutation-tested.
| function | route | score |
|---|---|---|
| convex partition (Hertel-Mehlhorn on earcut) | own, 1 pw | 60 correct / 0 wrong; mutants 60, 53 (7 equivalent), 60 caught |
| convex partition | polypartition (MIT) | 60/0 with strict convexity; 48/12 if collinear merges count as removable (definition difference); its monotone partition 56/4 (4 polygons with holes return a non-monotone bridged ring; cause not diagnosed) |
| point visibility | own exact sweep, 2-3 pw | 60/0; mutants 30, 58, 57 caught |
| 2D alpha shape | own on CDT (use Geogram BDEL2d instead of MPL-2.0 CDT: not scored), 2-3 pw | 60/0; mutants 8 (ties only), 36, 50 |
| polygon repair | Clipper2 Union | 35 exact, 24 rounded (max relative area error 4.3e-4), 1 wrong (even-odd output where two rings share an edge); mutants 30, 6, 57 |
| 3D Delaunay | Geogram BDEL | 60/0; both mutants caught 60/60 |
| medial axis | Boost.Polygon voronoi (BSL-1.0), about 3 pw | sanity check only (not an exact oracle): clearance OK 60/60, topology matched 59/60; a true medial axis, not SFCGAL's skeleton-derived one (UNVERIFIED) |
## Verdicts (person-weeks with basis)
1. **Straight skeleton:** permissive candidates exist but none was scored (the sandbox denied building the cloned project): IGITUGraz/StraightSkel (MIT, C++17, double kernel) and akashskypatel (BSD-3, port of kendzi; BSD-3 text read). Integration plus hardening 4-6 pw (guess, +-2x, about 2.2k LOC); from scratch XL (16); roofs and extrusion on top 1-3 pw.
2. **Medial axis:** Boost.Polygon voronoi, about 3 pw (above).
3. **Alpha shapes:** own, 2-3 pw. 4. **Visibility:** point own 2-3 pw; segment has no prototype, about 3 pw (UNVERIFIED).
5. **Partitions:** Hertel-Mehlhorn own 1 pw; monotone via polypartition plus a hole fix 1-2 pw; Greene adds 1-2 pw. The count bound (parts <= 2r+1-h) is the sub-agent's own derivation.
6. **Repair:** Clipper2 plus an overlap-merge post-pass, 2 pw.
7. **3D alpha wrap:** no permissive implementation found; own 14-20 pw (UNVERIFIED sizing). Geogram has no alpha wrap; `Delaunay3d::insert` is protected, so Steiner insertion needs a subclass.
8. **3D Delaunay / alpha complex:** Geogram BDEL; the alpha-complex filter on top about 2 pw.
## Licences read from files at pinned commits
MIT: polypartition (b000a4a2), StraightSkel (abce60c9), StrandedKitty (d68f08ad; v1 branch MIT), Briganti, trylock/visibility. BSD-3: akashskypatel, kendzi-math. Apache-2.0: campskeleton. ISC: TriVis. BSL-1.0: Boost.Polygon. **Not usable:** polyskel LGPL-3.0; bpypolyskel and Lichtso GPL-3.0; wrapwrap AGPL-3.0; StrandedKitty v2 and konfrontend (MIT wrappers around CGAL via Wasm).
## Bars not met / unverified
Repair: 1 invalid in 60 and exact area equal on only 35/60 (Clipper2 snaps intersections to its integer grid; the rounded-OK threshold is the sub-agent's heuristic). Polypartition monotone: 4 cases flagged. No scored prototype for straight skeleton or 3D alpha wrap. UNVERIFIED: csharp-libs Apache-2.0 (GitHub API field only); polyskel/bpypolyskel/wrapwrap licences (search metadata only); surfer2 has no root LICENSE; compiled contents of StrandedKitty v2; that SFCGAL's medial axis comes from the straight skeleton.
