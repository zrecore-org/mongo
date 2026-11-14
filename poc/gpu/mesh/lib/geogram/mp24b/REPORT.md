# MP24b: pipeline v2 (exact scale, exact re-centre or c = 0, hard-limit refusal) on the M1

Written by the delegated agent from `M1_report24b.md` (the CI report script output, run once on the M1 after the freeze) and `M1_mutant_report.txt`. Linux x86_64/aarch64 pending the CI job `mesh-mp24b-geogram`.

**Order of events.** Pipeline `geogram_fidelity2.cpp` written; development sweep (`DEV_SWEEP.md`) chose T = 2^12; FROZEN_SHA256.txt written (2026-11-14 17:55Z); only then `corpus/fidelity2/gen_fidelity2.cpp` written (fresh seed), run twice (byte identical, 804 pairs, 1609 files) and hashed (`EXPECTED_SHA256.txt`, `GENERATOR_SHA256.txt`, 18:00Z); then every family run once (18:02Z). After the freeze and the generator I also ran two generator-design checks that touch no pipeline code and no acceptance file: (a) the pipeline on the MP25 magnitude corpus (3315 of 3315 OK, a dev check before the freeze), (b) `dev/gen_dev_axes.cpp` (x-only and x,y-far offsets through the v2 and the c = 0 dev binary, after the freeze, before the generator): c = 0 never failed with one far axis up to ratio 2^30 and began to fail with two far axes at ratio 2^25 (6 of 24 runs at j = 25). That shaped the `cfar` class (x2 offsets up to 2^30) so that the T-too-large mutant is visible.

## Results (M1, 9453 rows)
| bar | result |
|---|---|
| MP24 bar 1, G1 93 rows | MET: 75 PASS, 18 REFUSED (all malformed, invalid input), 0 FAIL, 9/9 must-pass rows PASS; the 6 valid sphere/torus rows that MP24 refused now PASS |
| MP24 bar 2, offset family (511 pairs) | MET: 1533 PASS, 0 REFUSED, 0 FAIL (the 6 span54 pairs now PASS through c = 0, MP24 refused them) |
| MP24 bar 3, thin family (171 pairs) | MET: 513 PASS |
| MP24 bar 4, set 3 and set 5 | **MET: 0 FAIL, 0 REFUSED** (set 3: 1602 PASS, set 5: 3300 PASS; MP24 refused 462) |
| MP24b acceptance f2 (804 pairs, 2412 rows) | **NOT MET: 3 FAIL rows (1 pair)**, 228 REFUSED (all hard limits), 2181 PASS |
| overhead | median 2.80% over 9201 valid rows (ratio of medians 2.56%) |
| translate-back error | maximum 0.5 ulps (42890 of 1320765 coordinates rounded) |

**The one failing pair:** `f2_cfarrc_L3r_j40_shift` (level 3 icosphere of radius 1.1 at offset 2^41, exactly re-centrable, shifted copy): all three operations FAIL with "result has a duplicated triangle" (zero-area triangle present, 0 mis-classified sample points). The plain MP22 runner on the same files also FAILs (93 of 3000 points wrong, inverted difference), so it is a Geogram result-quality failure on near-degenerate coarse-lattice input (the translated mesh has coordinates on a 2^-11 grid), not an error of scaling or centring; the same pair at offset 2^36 (j = 35) and all other cfarrc rows (81 of 84) pass. Not hidden and not changed after the run. A similar zero-area/duplicated-triangle behaviour was seen in the development sweep for a spiky shifted icosphere at small ratios (separate finding, `DEV_SWEEP.md`).

**Hard-limit refusals (228 rows = 76 pairs):** `cfar` 222 rows (74 pairs): full-mantissa icospheres (L1, L2, L3, L3r) far from the origin on one or two axes that are not exactly re-centrable (the near axes carry nonzero full-mantissa centre) and with bbox-centre / largest-side above T = 2^12 (nominal ratio 2^13 .. 2^30; all 222 had ratio > T, all 222 `cfar` rows were refused, none passed). `uflow` 6 rows (2 pairs): scaling by 2^-483 or 2^-983 would underflow a 2^-600 or 2^-100 coordinate. No refusal anywhere else: 0 other refusals of valid rows in G1, offset, thin, set 3, set 5, and the 18 G1 malformed refusals are invalid-input refusals as before. Caveat: the c = 0 path would have passed the x-only far rows (dev check: no failure up to ratio 2^30), so the hard-limit class is conservative (it is defined by T from the worst, all-axes case).

## Mutants (all five caught; M1_mutant_report.txt)
| mutant | caught by (changed rows) |
|---|---|
| no scale | f2 only: 402 changed (nest 375, thin 15, touch 6, uflow 6); g1, offset, thin, set 3, set 5 unchanged (their coordinates stay below 2^18 or are not nested) |
| scale to 2^19 | f2 only: 66 changed, all `nest` (the `bottom` pairs: container half-width 31 x 2^(k-5), inner box at the bottom) |
| c = 0 always | g1 6, offset 767 (741 FAIL), set 5 1, f2 441 (cfar 222, cfarrc 31, off 188) |
| skipped scale-back | g1 6, offset 1314, f2 1037 (nest, off, thin, touch, cfarrc) |
| T too large (2^60) | f2 only: 222 changed (the cfar rows: refusals turned into 56 FAIL and 166 PASS) |

## Not tested
Linux x86_64 and aarch64 (the CI job is added, not run); the mutants in CI (M1 only); the dev sweep on Linux; `T` resolution (grid of one power of two; the first failure is statistical, 6 of 24 runs at the first failing ratio, so lower sporadic failures cannot be excluded); sizes beyond the families (real meshes, Thingi10K); the overhead timings were taken with 6 parallel jobs.
