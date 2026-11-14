# MP25: Geogram's coordinate-magnitude defects and the exact scaling mitigation (sub-agent run on the M1; two source claims re-checked by me)

Sweeps: 1105 pairs x 3 operations = 3315 runs per build; 2211 generator files hashed before any Geogram run (`corpus/magnitude/EXPECTED_SHA256.txt`) and re-verified after; each sweep run once; exact output volume against exact expectation; an octahedron replaced the sphere (curved meshes not tested). M1, libc++, 1 Geogram thread.
| build | wrong / 3315 |
|---|---:|
| unpatched | 954 |
| MP20 | 756 |
| MP22 | 756 |
| MP22 + exact scaling | 72 |
| MP24 re-centring only | 684 |
| **MP24 re-centring + exact scaling** | **0** (0 refused, 0 open outputs) |
## Defect A: nested solids (verified by probe; the constant re-checked by me in the source)
First failure at container half-width 2^20 (k <= 19 always OK); intersection returns an empty result, union and difference the wrong volume; identical for all three operations and every ratio. The trigger is the +z distance from a component's start vertex to its enclosing surface exceeding 1e6: `compute_component_inclusion_bits*` casts a segment of fixed length `q1.z + 1e6` (`mesh_surface_intersection.cpp` lines 1981, 2027, 2029, 2073, 2075 in the pinned v1.10.1; the MP22 patch inherits it). Replacing 1e6 by 1e9 moves the failures to k >= 30, 1e30 removes them. The 2^63 overflow hypothesis (mine) is **refuted**: k = 20..29 pass with 1e9 although (2h)^3 > 2^63. Offsets up to 2^50 do not trigger A.
## Defect B: overlapping boxes far from the origin (verified by probe; the sum re-checked by me in the source)
Overlapping unit boxes at offset 2^t return the right shape with **inverted orientation** (volume = -expected) at t = 26 and every t >= 28 (72 of 153 runs, all builds). `classify()` picks the external chart by a plain-double sum `chart_volume[...] += dot(p1,cross(p2,p3)) / 6.0` on absolute coordinates (line 1777; the same sum at line 1075 in `mark_external_shell` did not matter), which cancels catastrophically at large offsets. Translating by vertex 0 inside the sum fixes the offset family at every t. Both probes together leave only the vertex-contact family failing on the unpatched library. Scaling does not fix B; re-centring does.
## Mitigation
Scale both operands by 2^-s so that max |coordinate| < 2^18 (exact unless underflow), run, scale back (exact), then the MP24 re-centring pipeline: **0 wrong on the entire sweep**. Scaling alone fixes A but not B; re-centring alone fixes B but not A; both are needed. 2^18 is a margin (any coordinate difference is at most 2^19 < 1e6; a bound of 2^19 would not be safe because spans reach 2^20). Neither mechanism involves the standard library (a constant and plain double arithmetic), but only the M1 was run.
## Not tested
Linux x86_64/aarch64 for the sweeps; curved or real meshes under scaling; underflow beyond a smallest feature of 2^-41; the threshold resolution is one power of two (apart from the 1e9 probe). The mitigation was run with the existing `mp24/bin/geogram_fidelity`, not a rebuilt one.
