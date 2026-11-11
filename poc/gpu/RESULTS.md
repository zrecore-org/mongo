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
- **Caveat:** the ill-conditioned run (+1e10, 1M small values, -1e10) used a non-exact reference, so its
  ulp counts (CPU 1.3M, GPU 1.9M, plain 754M) only rank the three methods; they are not exact errors. It
  needs a rerun against an exact oracle.

## POC 4: failure handling: PASS (12/12, no GPU)
Mock device with injected device-lost, OOM and timeout (with partial output written before failing).
Whole-batch CPU rerun returns the correct result every time; the breaker opens after 3 consecutive
failures, serves 50 calls without touching the device, doubles the backoff 1 s -> 2 s up to the 8 s cap, and
closes after a successful canary; a single transient failure leaves it closed.

## Not yet done
- POC 2 timing sweep (1K..32M elements, GPU vs `DoubleDoubleSummation`, break-even table, modeled PCIe term).
- POC 3: radix sort and percentile.
- Exact-oracle rerun of the ill-conditioned case.
