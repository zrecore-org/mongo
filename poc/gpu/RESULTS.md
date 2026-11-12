# GPU proofs of concept: results so far

Machine: Apple M1 Pro (arm64), MoltenVK 1.2.9 (Vulkan 1.2.283), unified memory, `shaderFloat64=false`,
`shaderInt64=true`, subgroup size 32. **Unified memory is the best case for host<->device transfer;
do not read these numbers as typical for discrete GPUs.** Build with
`cmake -S . -B build && cmake --build build`; run with `./run.sh <poc> [args]` (it sets the MoltenVK ICD).

Status: POC 0, POC 1, POC 2 (accuracy part) and POC 4 are done. The POC 2 timing sweep and POC 3 (sort) are
**not run yet**: the GPU is shared with PG-Strom work (`../pg-strom`, a Metal/MoltenVK service plus a test
lock at `/tmp/pgstrom_test.lock`), and benchmark numbers taken beside it would be unreliable. Only short,
small runs (seconds, GPU utilization about 3%) were done so far.

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

## Phase 1 go/no-go: conditional go
- Go only as opt-in, for batches of at least 256K elements, with the CPU path as automatic fallback (POC 4)
  and a startup known-answer self-test (POC 1: `precise` on every df64 op is mandatory).
- `$sum` on the GPU is limited to few groups (at most about 16) and is not bit-identical to the CPU, so it must not be on by default; selection and
  percentile are exact and are the better first targets.
- Not measured: a discrete GPU with PCIe transfer, Linux/CUDA-class drivers, .
## POC 4: failure handling: PASS (12/12, no GPU)
Mock device with injected device-lost, OOM and timeout (with partial output written before failing).
Whole-batch CPU rerun returns the correct result every time; the breaker opens after 3 consecutive
failures, serves 50 calls without touching the device, doubles the backoff 1 s -> 2 s up to the 8 s cap, and
closes after a successful canary; a single transient failure leaves it closed.
