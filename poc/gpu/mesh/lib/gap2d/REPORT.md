# MP08a: 2D permissive coverage (sub-agent run; the verdict table below is `results/summary.txt`, whose totals I re-derived from `results/corpus.tsv` by excluding the 20 invalid-input rows; bars in `DECLARED.txt` were written before any library ran)

Corpus: `corpus2d/` (192 pairs, 14 rectilinear offset cases, 20 invalid inputs, 1e3 and 1e5 vertex speed polygons; integer-only, sequenced RNG draws, `-ffp-contract=off`, hashes in `EXPECTED_SHA256.txt` recorded before any library ran).
Pins (licence file read; paths and sha256 in the sub-agent's notes): Clipper2 2.0.1 (21ebba05, BSL-1.0), Boost 1.92.0 tarball (BSL-1.0, sha256 matches the host's), earcut v3.2.4 (ISC), CDT 2.0.2 (MPL-2.0, comparison only), Manifold CrossSection (Apache-2.0, already built); S2 in tree (Apache-2.0, old spherical API, has InitToUnion/Intersection/Difference; not built or run). 4634 runs, no crash or timeout.
Checker: exact rational oracle (area and symmetric-difference area) plus exact validity checks; all 6 planted mutants caught; corpus self-check clean.

## Valid inputs (206 rows per operation; CORRECT / WRONG(area) / WRONG_INVALID(structure) / REFUSED)
| Library | union | intersection | difference |
|---|---|---|---|
| Clipper2 | 188 / 0 / 4 / 0 | 189 / 3 / 0 / 0 | 189 / 0 / 3 / 0 |
| Boost.Geometry | 187 / 5 / 0 / 0 | 186 / 6 / 0 / 0 | 187 / 5 / 0 / 0 |
| Manifold CrossSection | 112 / 41 / 2 / 37 | 101 / 54 / 0 / 37 | 100 / 54 / 1 / 37 |
Other operations: offset (miter, 14 rectilinear) 14/14 for all three; convex hull Boost 206/206, Manifold 169/169 (37 refused); triangulation earcut 206/206, CDT 206/206, Manifold 169/169 (37 refused), Clipper2 203 with 3 wrong and a throw on the 1k polygon; Delaunay violations: CDT 0 of 909 edges, earcut 216, Manifold 59 (neither claims Delaunay); `intersects`: Boost 192/192, Clipper2 only a composition via Intersect() and misses touching pairs (36 wrong); Boost `is_valid` accepts duplicate consecutive vertices.
Causes: Boost's wrong rows are the 2^40 offsets (doubles) and one near-degenerate pair; Manifold refuses coordinates of 2^40 or more ("Values exceed permitted range") and its other wrong rows are within its 1e-8 grid but outside the declared 1e-12; Clipper2 at integer scale 1 has median relative area error 1.1e-3 (worst 20%) so callers must scale. On the 20 invalid inputs no library crashed; Boost and Clipper2 booleans never refuse and mostly return valid-looking output.

## Speed (ms, one thread, median of 5, 1e3 / 1e5 vertices; load average 5-11, treat as upper bounds)
Boolean: Boost 0.6 / 12, Clipper2 1.2 / 160, Manifold 1.2 / 155. Triangulation: earcut 0.13 / 20, CDT 1.5 / 150, Manifold 1 / 250, Clipper2 refused / 82 (1e5 result unchecked).

## Bars not met / not covered
No library has 0 wrong on any boolean; Boost and Manifold fail 1e-12 at 2^40 and on non-integer intersections; no permissive library is both Delaunay and failure-free. Not covered: simplify, centroid, distance, Minkowski, round joins, inward and non-rectilinear offset, re-centring, multi-thread and cross-architecture runs, the 1e5 results, and the 31 Boost parity cells from MP06.
