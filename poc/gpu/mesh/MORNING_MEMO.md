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
| Vertex-contact solids | correct | **wrong** (vertex contact only; platform dependent) | correct on the M1 (set 4: 3150 rows, 0 FAIL); **6 rows fail on Linux** (two "touches three others" pairs), investigation running |
| Bad input | refuses cleanly (12 of 21 refused, 0 FAIL) | never refuses; one open result | validator refuses 18 of 21, 0 FAIL |
| Determinism | same bytes serially | same bytes with one thread | same bytes with one thread; Linux x86_64 and aarch64 agree with each other, differ from the M1 |
| Licence | Apache-2.0 | BSD-3 core (TetGen, Triangle, HLBFGS off, verified) | same; patch is local, upstream issue #413 filed, no reply |
Closed alternatives: 512-bit integer plane booleans (800x slower than Shewchuk's adaptive predicate, kill criterion hit); Cherchi et al. 2022 (needs LGPL Indirect_Predicates headers, not scored); wrapping Geogram with guards (MP18/MP18b failed).

## Decisions waiting on you
1. **Do you need fidelity below 1e-12 of mesh scale?** No: Manifold + re-centring, and the Geogram work stops. Yes: Geogram, which today needs the patch, the validator and the Linux issue resolved.
2. **LGPL:** is LGPL header-only code ever acceptable? If yes, Cherchi et al. becomes scoreable.
3. **Unwelded vertices:** refuse (what the validator does) or weld first?
4. **Upstream:** if the Geogram maintainer replies on #413, do you want a pull request with our patch?
