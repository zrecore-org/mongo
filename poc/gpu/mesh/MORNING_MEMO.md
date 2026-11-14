# Mesh workstream: where things stand (draft written overnight 2026-11-14; updated as results arrive)

Everything below is recorded with evidence in `poc/gpu/RESULTS.md`; this is the short version and the decisions that are yours.

## Gates
| Gate | Status |
|---|---|
| G2 GPU exact-sign filter (MP09) | **Met.** Bit-identical to the CPU on the M1 (1.35e9 cases, 0 wrong signs), lavapipe x86_64, lavapipe aarch64 and T4 (2M cases, 67 M cases/s, about 4x one CPU thread; 8-thread baseline not measured yet). |
| G1 boolean engine | **Open, your call.** Candidates below. Neither library passes as registered; both have been scored on the same exact tool. |
| G3 parity, G4 GPU value, G5 extension on a real mongod | Not started (Wave 2 and 3). |

## G1 candidates (all numbers measured; see RESULTS.md)
| | Manifold serial + re-centring | Geogram, unpatched | Geogram + MP20 patch + validator |
|---|---|---|---|
| Thin features (below 1e-12 of scale) | **vanish** (epsilon is a floor; cannot be lowered) | exact | exact |
| Vertex-contact solids | correct | **wrong** (vertex contact; platform dependent: the tie is broken by pointer order) | **correct with the second patch (MP22)** on an untouched 1100-pair set: 0 FAIL, 0 REFUSED on the M1 and on Linux x86_64 and aarch64 (the first patch alone failed 30 rows on Linux) |
| Bad input | refuses cleanly (12 of 21 refused, 0 FAIL) | never refuses; one open result | validator refuses 18 of 21, 0 FAIL |
| Determinism | **same bytes serially, and byte-identical on the M1, Linux x86_64 and Linux aarch64 on the G1 corpus (81 of 81 files; tier 3 holds, measured)** | same bytes with one thread | same bytes with one thread; Linux x86_64 and aarch64 agree byte for byte, differ from the M1 (all rows still correct) |
| Licence | Apache-2.0 | BSD-3 core (TetGen, Triangle, HLBFGS off, verified) | same; patch is local, upstream issue #413 filed, no reply |
Closed alternatives: 512-bit integer plane booleans (800x slower than Shewchuk's adaptive predicate, kill criterion hit); Cherchi et al. 2022 (needs LGPL Indirect_Predicates headers, not scored); wrapping Geogram with guards (MP18/MP18b failed).

## Decisions waiting on you
1. ~~Do you need fidelity below 1e-12 of mesh scale?~~ **Answered by you: yes, "I want fidelity." The Geogram route is now the plan** (MP24 builds and scores it end to end). Original text: No: Manifold + re-centring, and the Geogram work stops. Yes: Geogram, which today needs the patch, the validator and the Linux issue resolved.
2. **LGPL:** is LGPL header-only code ever acceptable? If yes, Cherchi et al. becomes scoreable.
3. **Unwelded vertices:** refuse (what the validator does) or weld first?
4. **Upstream:** if the Geogram maintainer replies on #413, do you want a pull request with our patch?

## Update (later the same night)
- **MP10 GPU box-pair finder:** identical to the exact CPU answer and byte-identical across the M1, lavapipe x86_64, lavapipe aarch64 and the T4. On the T4 it is 2.2-27x faster than a 4-core CPU on the batched, uniform and clustered shapes, and loses on huge outputs (1-2M pairs). 1e6 x 1e6: 579 ms vs 1298 ms on 4 cores.
- **2D (MP08a):** no permissive library is exact on 2D booleans (Clipper2 3-4 wrong rows per operation, Boost.Geometry 5-6, Manifold CrossSection refuses 2^40 coordinates); triangulation is fully covered (earcut, CDT; only CDT is Delaunay).
- **Upstream (Geogram issue #413):** the filed text describes only the first mechanism. The second (rays through other bodies' contact vertices, tie broken by pointer order) should be added; I have not commented, it needs your approval.
- A fifth decision for you: **comment on #413 and offer the two patches as a pull request?**

## Update 2 (pipeline status)
- **Assembled high-fidelity pipeline (MP24b) on the M1, Linux x86_64 and Linux aarch64:** identical verdicts everywhere. The G1 corpus, the offset family (offsets to 2^50), the thin-feature family (widths to 2^-50), touch sets 3 and 5 (4902 rows, 0 refused) and 804 fresh pairs: 0 wrong results; 228 documented refusals (full-precision curved meshes far from the origin that cannot be re-centred exactly, plus 2 underflow pairs); 3 rows flagged FAIL are one pair that the diagnosis shows is a scoring-rule artifact (vertex rounding on a coarse lattice collapses triangles); the registered bar "0 FAIL" is therefore recorded as NOT MET and not changed.
- **Two more Geogram defects found and root-caused** (MP25): a hard-coded 1e6 in the inclusion ray (nested solids at coordinates of 2^20 or more), and a plain-double volume sum picking the outer shell (shapes far from the origin come back inside-out). The pipeline avoids both with exact power-of-two scaling and exact re-centring. Neither is reported upstream yet (decision 4).
- **New decision for you (6 above becomes 7):** amend the bar so that collapsed zero-area triangles caused only by vertex rounding are a counted, reported class rather than a FAIL?
