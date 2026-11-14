# MP24: the assembled high-fidelity boolean pipeline (validate, exact re-centre, Geogram MP20+MP22, translate back)

Written by the main session from the sub-agent's report; the per-family tables are `M1_report24.md` (checked by me against the sub-agent's totals). Frozen before the new families existed: `FROZEN_SHA256.txt` (17:19Z); families' hashes in `corpus/fidelity/EXPECTED_SHA256.txt` (1366 files) recorded before any build ran on them. Each family was run once on the M1.
## M1 results (7041 rows, 0 FAIL overall)
| bar | result |
|---|---|
| 1. G1 93 rows: 0 FAIL, the 9 huge-offset and vertex-touch rows pass | **MET**: 69 PASS, 24 REFUSED (18 malformed, plus 6 valid rows: sphere_vs_star, torus_vs_sphere), 0 FAIL |
| 2. offset family (511 pairs, 2^20..2^50 and odd offsets): 0 FAIL, refusals only where the span exceeds 53 bits | **MET**: 1515 PASS, 18 REFUSED (the six span54 pairs, "53 bits"), 0 FAIL; the 27 b53 rows PASS |
| 3. thin family (171 pairs, 2^-30..2^-50): 0 FAIL | **MET**: 513 PASS, 0 REFUSED, 0 FAIL |
| 4. touch sets 3 and 5 through the pipeline: 0 FAIL, **0 REFUSED** | **NOT MET**: 0 FAIL but 462 REFUSED by the 53-bit rule (set 3: 282 of 1602, class sphere; set 5: 180 of 3300, class curved); every other class PASSES with exact volume |
| 5. overhead <= 10% | MET: median 2.75% over 6534 valid rows (in-process timers while 6 jobs ran; not a quiet serial timing) |
| 6. M1 and Linux x86_64/aarch64 | pending the CI job `mesh-mp24-geogram` |
| 7. mutants | MET: wrong c (plain midpoint) 27 offset rows PASS to REFUSED; skipped translate-back 1694 rows FAIL; rounding instead of refusing 24 rows change (12 span54 rows FAIL, 6 sphere G1 rows REFUSED to PASS) |
Maximum translate-back error: **0.5 ulp** (the IEEE bound; 25109 of 386304 result coordinates, 6.5%, rounded; Geogram's own rounding in the translated frame is not included).
Manifold on the same families (serial): offset family 887 of 1533 rows FAIL (364 of 511 pairs); thin family 153 of 513 rows FAIL (84 of 171 pairs, all at widths 2^-40 and 2^-50).
## Findings
1. **The registered re-centring rule refuses full-mantissa curved meshes**: their lowest set bit is near 2^-60 while coordinates reach 1, so any nonzero c makes `x - c` unrepresentable; meshes whose bounding-box centre is exactly 0 get c = 0 and pass. A fallback to c = 0 would be a rule change, so it needs a new registered rule and a fresh run (not patched here).
2. **A new Geogram defect** found outside the families (hand-made smoke tests; I reproduced it with my own program on the pinned v1.10.1 build): a box nested inside a larger cube returns an EMPTY intersection when the container's half-width is >= 2^20 (2^19 is fine); the same size ratio at a smaller absolute scale is correct (container 2^10 with inner edge 2^-10 passes; container 2^20 with inner edge 1 fails; container 2^40 with inner edge 2^20 fails), so it depends on the **absolute magnitude of the coordinates, not the ratio**. It happens for unpatched, MP20 and MP22 builds. Cause not investigated (the threshold (2h)^3 = 2^63 suggests a 64-bit overflow, UNVERIFIED). An exact power-of-two scaling of both operands would avoid it without losing bits, if the cause is magnitude; not tested.
## Not tested
No Linux scoring yet (the aarch64 container ran the CI stages fetch, build-chain, build-fidelity with the frozen-hash check, tools24 and corpus24: generator hashes matched); x86_64 not at all; mutants are not in CI.
