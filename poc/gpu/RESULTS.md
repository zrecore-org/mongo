# GPU proofs of concept: results so far

Machine: Apple M1 Pro (arm64), MoltenVK 1.2.9 (Vulkan 1.2.283), unified memory, `shaderFloat64=false`,
`shaderInt64=true`, subgroup size 32. **Unified memory is the best case for host<->device transfer;
do not read these numbers as typical for discrete GPUs.** Build with
`cmake -S . -B build && cmake --build build`; run with `./run.sh <poc> [args]` (it sets the MoltenVK ICD).

Status: all planned POCs are done (0 to 11, see below). Timings come from three places: the M1 Pro (local), the
GitHub-hosted NVIDIA T4 (CI) and lavapipe (CI, correctness only). The local GPU is shared with PG-Strom work, so M1
runs go through the optional lock wrapper and absolute times drift with the other load; ratios are the useful part.

## POC 0: loader and fallback: PASS
- Loader opened with `dlopen` (no link-time dependency); compute smoke test over 1M elements: 0 mismatches.
- Pipeline creation (includes the SPIR-V -> MSL -> Metal compile): about 49 ms for a trivial kernel.
- `GPU_POC_LOADER=/nonexistent` gives `FALLBACK: GPU unavailable (no Vulkan loader found) -> using CPU backend`
  and a correct CPU result. The probe never aborts.

## POC 1: df64 correctness: PASS (with one hard requirement)
Method: GPU add/mul/div/sqrt over 100k adversarial pairs (mixed exponents, cancellation, wide exponent
gaps, tiny 1e-36..1e-31, huge 1e30..1e38), checked against an exact oracle (Python `Decimal`, 150 digits) by
`verify_df64.py` on every other element. "Kernel error" compares to the exact operation on the *split*
inputs (arithmetic quality only); "e2e" compares to the original doubles (adds the double -> df64 split
loss). Units: 2^-44 relative.

| op | worst kernel error (in range) | e2e (mixed) |
|---|---|---|
| add | 0.11 | 4.2 (cancellation: up to 1e12, see below) |
| mul | 0.22 | 0.2 |
| div | 0.13 | 0.1 |
| sqrt | 0.27-0.38 | 0.4 |

- The kernel arithmetic is about 2^-46 to 2^-45 relative, better than the 2^-44 estimate in the plan.
- **Cancellation:** the kernel is exact on the split inputs, but the end-to-end error against the original
  doubles reaches 1e12 x 2^-44 when `a + b` nearly cancels, because the 53 -> ~48 bit split loses the low bits
  before the subtraction.
- **Range:** no unflagged element exceeded 4 x 2^-44. Every large error was on an element the
  operation-aware preflight flagged (an operand or result outside [1e-30, 1e30]). Tiny inputs fail badly
  (flush-to-zero of `lo`, since `shaderDenormPreserveFloat32=false`); huge ones overflow float32.
- **Compiler experiment (fast math x `precise`):** `./run_matrix.sh`, output in `out/matrix.txt`.

| MoltenVK fast math | `precise` | plain (no `NoContraction`) | `precise` + SignedZeroInfNanPreserve |
|---|---|---|---|
| off (0) | correct | correct | correct |
| on (1) / default (2) | correct | **broken: errors 1e6 to 1e19 x 2^-44** | correct |

  Mechanism (from `spirv-cross --msl`): `NoContraction` is lowered to `[[clang::optnone]]` helpers
  `spvFAdd/spvFSub` implemented with `fma`, which fast math cannot reassociate. Without it Metal's fast
  math collapses the 2Sum chain. So `precise` on every df64 operation is mandatory, and the planned startup
  known-answer self-test is justified: it would catch a driver or setting that breaks this.

## POC 2: segmented `$sum`, accuracy part (200 random batches of 100k, GPU-side split variant)
The CPU reference is a port of `DoubleDoubleSummation::addDouble` (`src/mongo/util/summation.h:36-41`,
`_fast2Sum` at 96-100, `_2Sum` at 111-118). It was not compiled against the real class.

| comparison to CPU `$sum` | bit-identical final double | max difference |
|---|---|---|
| GPU df64 | 2 / 200 | 1155 ulp |
| plain double accumulation | 3 / 200 | 30650 ulp |
| CPU `$sum` on shuffled input | 200 / 200 | 0 ulp |

- Consequence for replicas and shards: GPU `$sum` results would almost never be bit-identical to the CPU's,
  so GPU use must be opt-in (`gpuExactResults: false`) or limited to operators whose result is exact.
  df64 is about 26x closer to `$sum` than plain double accumulation (by max ulp), but not identical.
- Determinism: 30 identical runs gave 1 distinct result; 5 different grid sizes gave 5 distinct results,
  so the answer depends on the workgroup count and the grid would have to be fixed for reproducibility.
- Fallback flag: 1e300, NaN and denormal inputs all set the flag; clean data does not.
- Ill-conditioned run (+1e10, 1M values of ~1e-3, -1e10), now against an exactly rounded reference
  (Shewchuk partials; true sum 1499.935579): CPU `$sum` is off by 1.3M ulp, GPU df64 by 1.9M ulp, naive
  double by 754M ulp. Both double-double paths lose accuracy to the 1e10 cancellation, GPU about 1.5x worse
  than CPU, and both are about 400x better than naive double. Neither is exact. I did not investigate why
  CPU `$sum` loses this much here.

### POC 2 timing (M1 Pro, unified memory; milliseconds, GPU = prep + GPU + combine, GPU-side split)
**The G=16 columns of this table are invalid:** they were measured with a grouped kernel that returned wrong
results on the M1 (see "Bug: grouped kernels on Metal" below). The corrected G=16 numbers are in that section.
| n | CPU `$sum` G=1 | GPU G=1 | speedup | CPU `$sum` G=16 | GPU G=16 | speedup |
|---|---|---|---|---|---|---|
| 16K | 0.109 | 0.383 | 0.28x | 0.039 | 0.408 | 0.10x |
| 64K | 0.440 | 0.422 | 1.04x | 0.152 | 0.419 | 0.36x |
| 256K | 1.72 | 0.658 | 2.6x | 0.641 | 0.680 | 0.94x |
| 1M | 6.89 | 1.57 | 4.4x | 2.48 | 1.55 | 1.6x |
| 4M | 27.7 | 8.9 | 3.1x | 9.71 | 5.10 | 1.9x |
| 16M | 110.4 | 17.5 | 6.3x | 40.7 | 21.4 | 1.9x |

- **Negative result for many groups** (same machine, GPU-side split): the GPU never beats CPU `$sum` at
  G=256 (0.87-0.90x at 16M, 0.62-0.73x at 1M) or G=4096 (0.54-0.57x at 16M, 0.35-0.38x at 1M). CPU
  double-double speeds up with G (16M: 110 ms at G=1, 18 ms at 256) because independent accumulators break
  the serial dependency chain, while the GPU combine step grows with G (11.9 ms at G=4096, 16M). So GPU
  `$sum` only wins for few groups (about 16 or fewer).

- Break-even about 64K elements for one group, about 256K to 1M for 16 groups. Plain `double` summation on
  the CPU is 4 ms at 16M, faster than the GPU, so the GPU only beats the double-double CPU path.
- There is a fixed cost of about 0.3 to 0.4 ms per dispatch. A discrete GPU would add a PCIe transfer
  term that this machine does not show (not measured).

## POC 3: radix select (k-th element / percentile), 63 ranks
Correct 63/63 against `std::nth_element` after fixing a shared array sized by a specialization constant
(Metal miscompiled it; the fix was a fixed-size array). `nth_element x3` is the CPU cost of three ranks.

| n | CPU nth_element x3 | GPU total | speedup |
|---|---|---|---|
| 64K | 0.85 | 0.92 | 0.9x |
| 256K | 3.46 | 1.45 | 2.4x |
| 1M | 13.3 | 2.49 | 5.3x |
| 4M | 58.6 | 5.82 | 10x |
| 16M | 211 | 13.2 | 16x |

Break-even about 64K. Numeric results equal the CPU's (radix select returns an input element).

## POC 3b: percentile end to end (`poc3b_percentile.cpp`, `out/poc3b_session.log`)
Mirrors `percentile_algo_accurate.cpp` (one `std::nth_element` per p on the same vector) and
`percentile_algo_continuous.cpp` (`std::partial_sort` per p, then interpolate). Input is already a contiguous
`std::vector<double>`, so there is no gather. 0 mismatches: GPU values and interpolated results are
bit-identical to the CPU's. Median of 3-7 reps, ms. GPU numbers are conservative: ranks run in fixed chunks
of 16, so P=1 pays for 16 ranks of work.

| operator | P | 64K | 256K | 1M | 4M | 16M |
|---|---|---|---|---|---|---|
| discrete | 1 | 0.5x | 1.3x | 3.1x | 3.2x | 3.6x |
| discrete | 3 | 1.0x | 1.9x | 3.3x | 3.6x | 4.8x |
| discrete | 7 | 1.4x | 2.9x | 4.0x | 5.0x | 7.3x |
| discrete | 20 | 1.0x | 2.8x | 3.5x | 5.6x | 7.2x |
| continuous | 1 | 2.3x | 6.2x | 8.5x | 27x | 75x |
| continuous | 3 | 6.2x | 10.8x | 24x | 60x | 151x |

(Continuous P=7 and 20 reach 10-107x; omitted here, see the log.)

Finding that changes the picture: the continuous CPU path is slow because of `std::partial_sort`, not
because of the data size. POC 6 (`poc6_continuous_cpu.cpp`, CPU only) shows `std::nth_element` at the ceil
rank plus `std::max_element` over the lower part gives bit-identical results 12x (1M) to 27x (16M) faster
(P=3: 195 -> 16.5 ms at 1M, 7177 -> 269 ms at 16M). Against that fixed CPU path the GPU is about 2x at 1M and 5.5x at
16M (P=3), in line with the discrete numbers. **The CPU fix should land first and needs no GPU.**
Earlier fixes: the `rsel_hist.comp` shared array held only 8 ranks, so runs above 8 ranks wrote out of
bounds (NaN results); it is now 16 and larger rank sets run in chunks.

## POC 7: sort-key radix sort (`poc7_sort.cpp`, shaders `sort_*.comp`, `out/poc7_session.log`)
GPU LSD radix sort of (u64 key prefix, u32 index) pairs, 4-bit digits, 16 passes for 64-bit keys (8 for
32-bit), three kernels per pass (block histogram, scan, stable scatter). Correct: the permutation equals
`std::stable_sort` in 32/32 cases (n from 1 to 1M, duplicates, sorted, high bits set). The CPU baseline is
`std::sort` of the same (key, index) pairs. Times in ms:

| n | CPU std::sort | GPU 64-bit | speedup | GPU 32-bit | speedup | reordering 32-byte records by the permutation (CPU) |
|---|---|---|---|---|---|---|
| 64K | 3.4 | 1.9 | 1.8x | 0.9 | 3.8x | 0.1 |
| 256K | 15.5 | 5.0 | 3.1x | 2.7 | 5.8x | 0.6 |
| 1M | 69.9 | 20.0 | 3.5x | 8.1 | 8.7x | 4.9 |
| 4M | 309 | 69.4 | 4.5x | 33.6 | 9.2x | 25.8 |
| 16M | 1341 | 271 | 4.9x | 134 | 10.0x | 116 |

- Break-even is below 64K for this pair sort, but the plan's 1M+ keys is still where it matters.
- Not measured: extracting key prefixes from real sorter records, and the CPU tie-fixup of equal prefixes
  on the full keys. A CPU path that sorts the same index pairs also pays the reorder, so it cancels:
  at 16M, (1341+116)/(271+116) is about 3.8x end to end.
- Sort throughput is bandwidth-bound: 16 passes at about 12 B read and written per key. Prefixes that fit
  in 32 bits halve the time.

## POC 5: batch assembly cost (`poc5_gather.cpp`, CPU only, no GPU)
Gather of scattered 1000-double blocks into one contiguous staging buffer (best of 20, warm cache, so a
lower bound; it excludes BSONColumn decode, which would be needed either way):

| blocks | n | gather | per element |
|---|---|---|---|
| 256 | 256K | 0.043 ms | 0.17 ns |
| 1024 | 1M | 0.27 ms | 0.27 ns |
| 4096 | 4M | 2.2 ms | 0.53 ns |
| 16384 | 16M | 9.3 ms | 0.57 ns |

Adding it to the GPU `$sum` totals (GPU-side split): 1M, G=1: 1.84 ms vs CPU 6.9 (3.7x, was 4.4x); 1M, G=16:
1.82 vs 2.48 (1.4x, was 1.6x); 256K, G=16: 0.72 vs 0.64 (now a loss); 16M, G=16: 30.7 vs 40.7 (1.3x, was 1.9x).
Assembly is not a deal-breaker but pushes the G=16 break-even to about 1M elements. Gathering is skipped if
the shader reads blocks in place (unified memory only); not measured.

## Second implementation: lavapipe in CI (GitHub Actions run 43159891689, x86_64 and aarch64, llvmpipe LLVM 20.1.2)
CI runs the POCs on Mesa's CPU Vulkan driver, a different SPIR-V compiler and arithmetic implementation from
MoltenVK/Metal. Correctness only; no timing.

- Pass on both architectures: POC 4 (12/12), POC 3 radix select (63/63), POC 7 radix sort (32/32). The
  integer-only kernels give the same answers on two independent implementations.
- **df64 fails on lavapipe**, on both architectures: `add` is correct (kernel error 0.11 x 2^-44) but `mul`,
  `div` and `sqrt` are off by about 5e5 to 1e6 x 2^-44 (and `mul` on huge inputs returns NaN for half the cases).
  That is about float32 precision, as if the low word were lost. `verify_df64.py` reports FAIL: 10000 of
  20000 `mul` cases and thousands of `div`/`sqrt` cases are wrong and **not flagged by the in-shader range
  preflight**. Likely cause (not verified): the `fma` used to recover the product's rounding error is not
  executed as a fused operation, so the error term comes out zero.
- **Retracted (see the T4 section):** an earlier version of this note said the POC 2 `$sum` result on lavapipe
  (up to 42316 ulp from CPU `$sum`, worse than naive double) showed lavapipe degrading df64 sums. It does not:
  the real NVIDIA T4, whose df64 is correct, gives exactly the same numbers. The difference from the Mac run
  comes from the test data, which `std::normal_distribution` generates differently with libstdc++ than with
  libc++, so the ulp figures are only comparable within one platform. The fallback flag works on lavapipe
  (1e300, NaN, denormal set it). The df64 `mul`/`div`/`sqrt` kernel failure on lavapipe stands: it is
  measured against an exact oracle and does not depend on the data generator.
- **Consequence for the design:** a driver can pass every range check and still compute df64 wrongly. The
  startup known-answer self-test (a `mul`/`div`/`sqrt`/`fma` check against exact values) is therefore a
  hard requirement, not an optional extra, and a failed test must disable GPU `$sum` and anything else using
  df64. Selection and sort use only integer ops and are unaffected.

## NVIDIA Tesla T4: a discrete GPU over PCIe (GitHub-hosted `gpu-t4` runner)
Ubuntu 24.04, driver 590.48.01, 16 GiB, 4 vCPU host, Vulkan through `nvidia_icd.json`. The harness identifies a
discrete GPU by device type, uses device-local buffers with staging copies inside the timed submission, so PCIe
transfer is included in every "GPU" time below, and keeps staging buffers in cached system memory. CPU baselines
are the runner's 4 vCPUs, so speedups compare the T4 with that CPU, not with the M1. Runs: A = 43164462049,
B = 43172532681 (after the staging-memory fix), C = 43171892951 (POC 9 first run).

**Harness bugs found by these runs (all fixed, listed so older numbers are not trusted):** the first T4 run
(43163593526) mistook the T4's small host-visible VRAM window (PCIe BAR) for unified memory and ran out of device
memory; POC 0, 1 and 8 lacked the staging copies and returned zeros in run A; and staging buffers were in
uncached (write-combined) memory, so every host readback was extremely slow and was counted as GPU time. That
last one made the radix sort look 2x instead of 3x to 4x and produced run A's sort numbers.

**Correct on a real discrete NVIDIA GPU:** df64 add/mul/div/sqrt kernel errors 0.1 to 0.6 x 2^-44 and `fma` fused
(POC 1, POC 8 6/6 on both variants), the GPU-side double -> df64 split, radix select 63/63, radix sort 32/32
permutations, POC 4. The df64 failure of lavapipe does not occur here.

**Method (changed after run B):** each benchmark first times the CPU baselines, then warms the GPU for 1.5 s, then
times the GPU back to back; one extra "cold" sample is taken right after the CPU phase. Before this change the
CPU baselines (100 ms to 400 ms) ran between GPU calls, and the T4 idles at 300 MHz: it showed up as 4x run-to-run
swings (e.g. `$sum` at 16M with one group 5.4x in run A, 2.0x in run B) and a bogus slowdown of radix select at 4M.
Telemetry (nvidia-smi once a second, run 43173618273): SM clock 300 to 465 MHz during the CPU phases and 1590 MHz
under GPU load, temperature 43 C at most (no thermal throttling), power cap (SW power cap throttle reason) in 9 of
121 samples at up to 71 W of its 70 W limit.

Speedup of the GPU (PCIe included) over the CPU on the runner, sustained (warmed-up) clocks, run D = 43173618273:

| workload | 64K | 256K | 1M | 4M | 16M |
|---|---|---|---|---|---|
| `$sum`, 1 group, GPU split | 3.7x | 5.9x | 5.6x | 5.6x | 5.8x |
| `$sum`, 16 groups | 0.98x | 1.5x | 1.1x | 1.5x | 1.6x |
| `$stdDev`, 1 group | 2.9x | 6.4x | 6.8x | 7.2x | 7.4x |
| `$stdDev`, 16 groups | 0.54x | 0.97x | 0.78x | 0.96x | 1.0x |
| radix select, 3 ranks | 2.7x | 6.4x | 8.2x | 11.1x | 12.4x |
| radix sort, 64-bit keys | 3.5x | 4.0x | 2.9x | 3.5x | 3.9x |
| radix sort, 32-bit keys | 5.4x | 6.2x | 4.9x | 6.0x | 6.6x |

- **Cold (first query after the GPU idled) costs more, by an amount that depends on the idle gap (POC 11: 1.6x after 2 s, 4.9x after 12 s)**; the figures here are for the short gaps of this benchmark: radix select at 4M took 11.0 ms cold against 6.0 ms
  sustained, and at 16M 34 ms against 23 ms. A server that sends the GPU sporadic work pays the idle-clock ramp; a
  busy one does not. Earlier T4 numbers (runs A and B above) mixed the two states and should not be quoted.
- **`$sum` and `$stdDev` for one group keep a 4x to 7x gain over PCIe from about 256K values; with 16 groups they are
  break-even** (0.8x to 1.6x), the same few-groups limit as on the M1.
- **Radix select is the biggest win**, 6x to 12x from 256K values. **Radix sort is 3x to 4x** (5x to 6x for 32-bit
  keys), close to the M1's 3.5x to 5x. At 16M the 16 sort passes take 380 ms on the GPU (host upload
  8, host readback 9). Isolated per-kernel timing over 16 passes: count 70 ms, scan 56 ms. The isolated scatter timing is
  invalid (run without the count and scan kernels it computes positions from a stale histogram, so every key lands in
  the same place; the M1 shows 3400 ms for the same reason), so scatter's share is only known by subtraction, roughly
  two thirds of the total. The single-workgroup scan is about 15% and is the obvious thing to parallelize.
- **Retracted:** (1) that the T4 radix sort was slow because of uncoalesced reads, and that the sort and select
  kernels "scale badly on NVIDIA": the sort cost was uncached host readback, and the select slowdown at 4M was GPU
  idle clocks plus run-to-run noise; (2) that the select slowdown "is not a cold-start effect": my warm test ran only
  three calls of a few ms and could not have ramped the clock, and the telemetry and the later runs show it is.
- `$sum` accuracy on the T4 equals the lavapipe numbers exactly, which is how the earlier lavapipe claim was
  caught (see the retraction above).

## POC 9: `$stdDev` (`poc9_stddev.cpp`, `shaders/std_m2.comp`)
The real accumulator (`accumulator_std_dev.cpp`) is plain-double Welford with a division per element. GPU method:
pass 1 is the POC 2 `$sum` kernel (df64 sum per group), the host computes the means, pass 2 sums `(x - mean)^2` in
df64 relative to the global mean, so partials just add with no Chan merge correction. Data is uploaded once.
Relative error of `stdDevPop` against a double-double oracle, n = 1M (T4, run C and B):

| data | CPU Welford | naive double 2-pass | GPU df64 |
|---|---|---|---|
| mean 0, sd 1 | 1.9e-14 | 2.1e-14 | 4.4e-16 |
| mean 1e6+0.3, sd 1 | 2.6e-11 | 1.3e-14 | 2.9e-13 |
| mean 123456789.123, sd 1e-2 | 1.1e-7 | 1.2e-12 | 8.5e-10 |
| mixed magnitude 1e-3..1e3 | 8.8e-15 | 7.1e-15 | 3.0e-16 |

The GPU result is as accurate as or better than the CPU's Welford everywhere, and 100x to 1000x better when the
mean dwarfs the deviation; a plain double two-pass beats the GPU's df64 on that kind of data because converting a
53-bit double to df64 keeps only about 48 bits. (A "mean 1e9" row was dropped: 1e9 is a multiple of 64, so it
splits exactly and the 0 error was an artifact.) Timing is in the table above: 5x to 7x for one group from 256K
values, no gain for 16 groups. Like `$sum` it is not bit-identical to the CPU, so it is opt-in.

## POC 11: pre-warming (`poc11_prewarm.cpp`, T4 runs 43174502364 and 43175298309)
Radix select, 4M values, 3 ranks; each cell starts with 2 s of sustained load, then an idle gap, then one timed query
(2 runs per cell; the clock and power are read from nvidia-smi right before the query).

- **Startup warm-up:** pipeline creation costs 26 + 8 ms and the first query 9.5 ms against 6.2 ms for the second. A
  dummy query at startup (the df64 self-test already runs kernels) pays the 3 ms once.
- **Idle gap, no keep-alive:** the query slows as the gap grows, because the T4 drops its clocks and then enters a
  deeper idle state:

| gap | query ms | SM clock | power before the query |
|---|---|---|---|
| 0 s | 6.5 | 1590 MHz | 49 to 51 W |
| 0.5 s | 6.9 to 7.4 | 1590 MHz | 33 W |
| 2 s | 10.2 | 360 MHz | 26 W |
| 5 s | 15.8 to 16.0 | 300 MHz | 25 W |
| 12 s | 31.8 | 300 MHz | 13 W |

  After 12 s idle the query takes 4.9x longer than at full clocks (still about 2x faster than the CPU's 66 ms at
  this size). The SM clock is the same at 5 s and 12 s, so the extra slowdown comes from a deeper idle state
  (lower power) that nvidia-smi's SM clock does not show; the cause is not identified.
- **A heartbeat does not help:** a tiny dispatch every 20 ms during the gap gave the same times and the same
  clocks and power as idling (25 W at 2 s and 5 s, 13 W at 12 s), with the same slow queries. The driver ignores that little load.
- **Locking the clocks works:** `sudo nvidia-smi -lgc 1590,1590` (allowed on the hosted runner) removes the penalty:
  the query after 12 s idle took 6.75 to 6.94 ms, against 31.8 ms unlocked. The price is idle power: about 37 to 38 W
  with the clocks held, against 13 to 25 W unlocked idle (and 50 to 53 W under load; the T4's limit is 70 W).
  Memory clocks and PCIe link state were not locked. This is an operator setting, not something the server can
  or should change by itself.
- **The M1 shows a similar penalty after CPU work (local run, `poc3_select sweep`, interference at 4M values):** 4.3 ms
  back to back, 10.8 ms right after 3 `nth_element` calls on a copy, 8.9 ms after a `std::sort`, 5.7 ms after summing 128 MB.
  The M1 is a unified SoC, so the cause is not necessarily GPU clocks: CPU activity right before may interfere through
  shared power, memory bandwidth or caches. Not investigated, so "Apple GPUs may ramp faster" is not supported.
- **Design implication:** the GPU path should track how long the GPU has been idle. After a gap of a few seconds,
  raise the minimum batch size or use the CPU, unless the operator has locked the clocks. The cold penalty is gap
  dependent (1.6x at 2 s, 2.5x at 5 s, 4.9x at 12 s), so the earlier "1.5x to 2x" figure only holds for short gaps.
  Not measured: other GPUs (the M1 was not tested, Apple GPUs may ramp faster), and how real query arrival patterns
  fall across these gaps.

## Bug: grouped kernels were wrong on Metal (found while checking POC 9 on the M1)
POC 9 printed that the GPU and CPU `$stdDev` for 16 groups disagreed (1% at 1M values on the M1, never on the T4).
The POC 2 accuracy test used one group, so nothing had checked a grouped result. A new `poc2_sum groups` check
(every group's GPU sum against the CPU double-double sum, relative to the group's sum of |x|) showed the grouped
`$sum` kernel was wrong on the M1 for 2, 4 and 16 groups (errors 1% to 3%; 1 group exact), for both split variants.
Cause: the per-thread accumulators were declared `vec2 acc[GROUPS]`, an array sized by a specialization constant,
which Metal (through MoltenVK) miscompiles. This is the second time that pattern failed (the first was the shared
array in the radix select), so the rule is: **no arrays sized by specialization constants; use a fixed maximum**
(`kMaxGroups = 16` now). After the fix all of 1, 2, 4 and 16 groups match to about 1e-17 on the M1. The check is a
gating step in CI on lavapipe and the T4 (the T4 looked right because POC 9 printed no mismatch, but it had not
been checked directly).

Corrected M1 timings for `$sum` (GPU split, PCIe not applicable). The machine was busier than for the first
measurements (CPU `$sum` 2x slower than before), so read the ratios, not the milliseconds:

| n | 64K | 256K | 1M | 4M | 16M |
|---|---|---|---|---|---|
| 1 group, speedup | 0.89x | 1.9x | 4.1x | 4.1x | 5.7x |
| 16 groups, speedup | 0.18x | 0.56x | 0.68x | 1.9x | 2.2x |

With 16 groups the old (wrong) kernel showed 1.6x at 1M; the correct one needs about 4M values to win. The few-groups
conclusion stands, with a higher threshold for several groups. POC 5's G=16 comparisons (1.4x at 1M, 1.3x at 16M)
rest on the wrong kernel and are not valid either.

## POC 9 on the M1 (after the fix)
Accuracy is the same as on the T4 (GPU df64 4.4e-16 to 8.7e-9, CPU Welford 1.7e-14 to 1.4e-6; the "mean 1e9+12345.678"
row shows the 48-bit df64 split: 8.7e-9 against 0 for a plain double two-pass). All 16-group results now match the
CPU. Timing (busy machine): 1 group 1.1x at 256K, 2.9x at 1M, 3.3x at 4M, 2.8x at 16M (an earlier quieter run gave 5x at
16M); 16 groups 0.3x at 1M, 0.7x at 4M, 1.0x at 16M.

## POC 10: in-place reads of host memory on a unified-memory GPU (`poc10_inplace.cpp`, M1)
MoltenVK offers `VK_EXT_external_memory_host`, so a server could decode blocks straight into a page-aligned arena and
hand it to the GPU as a buffer instead of copying scattered blocks into a Vulkan buffer. `$sum` kernel, 1 group,
1000-value blocks, all results bit-identical. Milliseconds, GPU path total (kernel included; decode excluded):

| blocks (values) | gather into mapped buffer | import the arena per batch | arena imported once and reused |
|---|---|---|---|
| 256 (256K) | 1.75 | 1.71 | 1.54 |
| 1024 (1M) | 4.16 | 3.66 | 3.31 |
| 4096 (4M) | 10.3 | 8.1 | 7.1 |
| 16384 (16M) | 33.9 | 23.0 | 17.7 |

Importing saves 2% to 12% up to 1M values and 21% to 32% at 4M to 16M; reusing a pooled arena saves 11% to 31% and 48% at
16M. It needs the engine to decode into page-aligned arena memory (16 KB pages on the M1) and a pool of imported
arenas to avoid the per-batch import cost. Only measured on the M1; on a discrete GPU importing host memory means
reading it over PCIe, so this is a unified-memory (Apple, integrated) optimization.

## Phase 1 go/no-go: conditional go
- Fix the continuous percentile CPU path first (`partial_sort` -> `nth_element` + `max_element`, POC 6).
- Go only as opt-in, for batches of at least 256K elements (about 4M for `$sum` and `$stdDev` with 16 groups on the M1), with the CPU path as automatic fallback (POC 4)
  and a startup known-answer self-test that gates df64 use (POC 1 and lavapipe above: `precise` on every df64 op is mandatory).
- `$sum` on the GPU is limited to few groups (at most about 16) and is not bit-identical to the CPU, so it must not be on by default; selection and
  percentile are exact and are the better first targets.
- Discrete GPU (T4, PCIe included): `$sum` and `$stdDev` for few groups keep a 3x to 7x gain, radix sort is about
  3x to 4x and radix select 6x to 12x at 256K and above, at sustained clocks (a cold first query costs 1.6x to 4.9x
  more on the T4 depending on the idle gap; POC 11). The df64 startup self-test picks the variant (fma, or Dekker without fma on lavapipe).
- GPU idle policy (POC 11): a software heartbeat does not keep the T4 warm; either the operator locks the clocks
  (`nvidia-smi -lgc`, about +13 to +25 W idle) or the GPU path becomes idle-aware (raise the batch threshold or use
  the CPU after a few seconds idle).
- Not measured: other discrete GPUs, Windows/Linux AMD, PCIe 4/5 hosts.

## POC 4: failure handling: PASS (12/12, no GPU)
Mock device with injected device-lost, OOM and timeout (with partial output written before failing).
Whole-batch CPU rerun returns the correct result every time; the breaker opens after 3 consecutive
failures, serves 50 calls without touching the device, doubles the backoff 1 s -> 2 s up to the 8 s cap, and
closes after a successful canary; a single transient failure leaves it closed.
