# MP17 steps 1-2: fixed-width integer plane predicates, cost and exactness

Date 2026-11-13. Clean-room: written from the maths only (no mesh-kernel, integer-plane-geometry or ember-csg code opened). Boost.Multiprecision
(BSL-1.0, vendored) and Shewchuk predicates.c (public domain, fetched by `oracle/fetch.sh`, not committed) are the only third-party code.
Labels: **measured** = a run in this directory; **derived** = my arithmetic in `predicates.h`; **unverified** = not tested.

## Files
- `fixedint.h`: `FixedInt<N>` N x 32-bit limbs, two's complement: add, sub, neg, mul (truncating, N(N+1)/2 limb multiply-adds), mulMag (skips leading zero limbs), cmp, sign, shl, shr; `-DFIXEDINT_CHECK` recomputes every op with cpp_int (overflow, mismatch, max bit length); `MUT(n)` mutants.
- `predicates.h`: plane from 3 points, point-plane side, vertex-of-3-planes side (Cramer / 4x4 Laplace, no division, sign corrected by sign(W)). Bit-growth derivation is in its header comment.
- `planebased_check.cpp`: exactness harness. `planebased_bench.cpp`: micro-benchmark. `build.sh`: build and run everything (`c++ -std=c++17 -O2 -ffp-contract=off -pthread`).
- Binaries and logs: `storage/mesh/planebased/` (`check_1e7.log`, `check_dbg.log`, `bench.log`, `mut*.log`).

## Commands
```
poc/gpu/mesh/lib/planebased/build.sh build      # builds checker, debug checker, 10 mutants, bench
poc/gpu/mesh/lib/planebased/build.sh test       # planebased_check 10000000 ; planebased_check_dbg 100000   (CPU, 8 threads, ~15 min)
poc/gpu/mesh/lib/planebased/build.sh mutants    # each mutant binary run with `mut`; must exit non-zero
poc/gpu/mesh/lib/planebased/build.sh bench      # single thread
```

## Derivation (derived) and observation (measured), magnitude bits, v = 2^L - 1
Signed width needed = magnitude bits + 1. "Bound" uses the plain coefficient count (edge 2v; n 8v^2; d 24v^3; ...; Num 294912 v^9 = 2^18.17 v^9).

| quantity | bound | L=26 obs/bound | L=40 obs/bound | L=53 obs/bound |
|---|---|---|---|---|
| plane normal n | 2L+3 | 54/55 | 82/83 | 108/109 |
| d | 3L+5 | 80/83 | 122/125 | 161/164 |
| W (3x3 of normals) | 6L+12 | 164/168 | 248/252 | 326/330 |
| Num (4x4) | 9L+19 | 246/253 | 372/379 | 489/496 |
| n.p + d | 3L+6 | 82/84 | 124/126 | 163/165 |

- Minimum signed widths from the bound: L=26 -> 254 (256 ok), L=40 -> 380 (384 ok), L=53 -> 497 (512 ok, 15 bits spare). Survey's 9L+18.6 (L=53: 495.6): same order; my 9L+18.17 differs only in the constant (octree-BSP constants include cell planes).
- **Max bits observed (measured), L=53: Num 489 magnitude bits (490 signed)**, vs my bound 496 and the survey's 495.6. The bound is not tight: real inputs lose about 7 bits (e.g. |n| <= 4v^2, not 8v^2, since a triangle in a 2v x 2v square has area <= 2v^2; I did not tighten the derivation further). Debug mode (every op of every case, not only named intermediates): max true bit length of any op result 246 / 372 / 489 at L=26/40/53, 0 overflows, 0 mismatches at the primary widths. 256 bits does overflow at L=53 and 384 at L=53 (negative controls: overflow counts in `check_dbg.log`), so the check can fail.
- The maxima come from random + corner + mixed + 48 hill-climbed cases (4 objectives); the hill climb is a lower bound on the true maximum, not a proof. The true worst case is **unverified**; the derived bound is the safe figure.
- Caveat: planes built as small integer combinations of planes (harness "override" cases) add ~3 bits to n; Num is 0 there. Not part of the derivation.

## Step 2: exactness (measured)
Every value (16 plane coefficients, W, Num, vertex side, n.p+d for two planes) compared with cpp_int; both the full-width-mul and mulMag variants. Oracle uses a different expansion order (recursive Laplace on the 4x4) and d via the triple-product determinant.

| config | random | extreme corners (+/-v) | extreme mixed | hill-climbed | degenerate (8 kinds) | diffs |
|---|---|---|---|---|---|---|
| L=26, 256 bit | 1e7 | 2e6 | 2e6 | 48 | 2e6 | **0** |
| L=40, 384 bit | 1e7 | 2e6 | 2e6 | 48 | 2e6 | **0** |
| L=53, 512 bit | 1e7 | 2e6 | 2e6 | 48 | 2e6 | **0** |
| extra widths (L26 at 384, 512; L40 at 512) | 1e6 | 2e5 | 2e5 | 48 | 2e5 | 0 |

- Degenerate kinds (cycled): collinear triple, duplicate points, vertex exactly on 4th plane, three planes sharing a line, query point coplanar, tiny coordinate range (-2..2), all points equal, parallel planes. Outcome mix is in the log (e.g. L=53: 385k zero sides and 1.16M "no vertex" in the 2e6 degenerate cases), so those paths are exercised.
- 1e6 random pairs per width (x3) for the primitives (add, sub, neg, mul, mulMag, cmp, sign, shl, shr): 0 differences.
- Negative controls (width too small, L=40 at 256, L=53 at 384 and at 256): 1.26M / 1.26M / 1.32M differences, as required.
- Debug build (`-DFIXEDINT_CHECK`, 1e5 random + all other classes at 20% of that): 0 overflows, 0 mismatches at 256/384/512 for L=26/40/53.
- Bar "0 differences": **met**. Caveat: 'extreme' is a generated and hill-climbed class, not a proof that no input reaches the bound.

## Planted mutants (measured): 10 of 10 caught
| # | mutant | caught by |
|---|---|---|
| 1 | mul drops a carry | primitives and predicate classes |
| 2 | mul flips sign of one limb product | primitives and predicate classes |
| 3 | shl off by one | primitives only |
| 4 | add drops a carry | primitives and predicate classes |
| 5 | shr logical instead of arithmetic | primitives only |
| 6 | cmp compares top limb unsigned | primitives only |
| 7 | vertex sign flipped | predicate classes |
| 8 | wrong cofactor sign | predicate classes |
| 9 | sub loses a borrow | primitives and predicate classes |
| 10 | plane d sign wrong | predicate classes |

Mutants 3, 5, 6 are in operations the predicates never call (no shift, no compare), so only the primitive tests can see them; that is why the primitive tests exist.

## Step 1: micro-benchmark (measured, M1, single thread, L=26 random integer inputs, best of 5)
Shewchuk adaptive orient3d on points 0..3 of each case (exact doubles); fixed-width vertex sign on the 4 planes built from points 0..11 (planes precomputed, as a plane-based boolean stores them).

| | ns/op | ratio to Shewchuk (5.81 ns) | limb multiply-adds |
|---|---|---|---|
| Shewchuk adaptive orient3d, random | 5.81 | 1 | - |
| 256-bit vertex sign, full-width mul | 416 | 72x | 1008 |
| 384-bit, full-width mul | 2401 | 413x | 2184 |
| **512-bit, full-width mul (unfiltered, the kill-criterion input)** | **4650** | **800x** | **3808** |
| 256 / 384 / 512-bit, mulMag (zero limbs skipped) | 557 / 674 / 812 | 96x / 116x / 140x | 224 each (28 multiplies on 2-6 limb operands) |
| cpp_int, same 28-multiply scheme | 896 | 154x | - |

Other (measured): plane from 3 points at 512: 1542 ns / 1224 MACs (mulMag 216 ns / 12); point-plane side at 512: 523 ns / 408 MACs (mulMag 84 ns / 6); pipeline 12 points -> 4 planes -> vertex sign at 512: 10518 ns full, 1705 ns mulMag; cpp_int pipeline 1862 ns. Shewchuk on exactly coplanar input (full exact path, context only): 117 ns, which would make the 512-bit full-width ratio 40x; PLAN's criterion names random inputs.

**Kill criterion (>50x Shewchuk adaptive, unfiltered 512-bit): triggered, 800x** (mulMag variant 140x; even the 256-bit type is 72x). No variant I built is under 50x.
Observations: the full-width mul does 3808 limb MACs per sign where the data needs 224 (operands are far below 512 bits), so the gap between 800x and 140x is wasted limbs; mulMag spends its time in full-width add/sub/zeroing (`add`/`sub` are not trimmed), so at 256 bits it is slower than the naive version. A tighter implementation (mixed widths per intermediate, trimmed add/sub) might land nearer the ~224 MAC floor; **unverified**, not built. Filtered variant: not built (a floating-point filter is the obvious route; no number to report).

## Ambiguities in PLAN (not changed)
- The kill criterion compares different inputs: orient3d takes 4 points, the plane test takes 4 precomputed planes (or 12 points). I report both (planes-given as headline; pipeline 10518 ns = 1810x). The criterion does not say which, nor whether mulMag-style trimming counts as "unfiltered 512-bit"; the verdict above is the same under every reading I built.
- Shewchuk's 5.8 ns is its stage-A filter on well-conditioned random input; a fair "exact vs exact" comparison would be the coplanar row (117 ns).
- "Max-determinant" extreme inputs: corners alone do not reach the bound (a corner triple cannot reach 8v^2); I added a hill climb. The bound 9L+19 is loose by about 7 bits.
- Scoring at L=53 relies on exact re-centred input only (PLAN step 3); not addressed here.
