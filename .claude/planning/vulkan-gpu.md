# Vulkan + df64 GPU acceleration for MongoDB (research and design)

## Context

Assess where GPU compute would speed up commonly used MongoDB work, and design an optional Vulkan
backend with a CPU fallback. df64 (a pair of float32s emulating fp64) is the accuracy-preserving
substitute for fp64 on GPUs without fast native double.

Decisions made with the user:
- Platforms: all (Linux x86-64/arm64, macOS via MoltenVK, Windows, integrated GPUs). ppc64le and
  s390x get a CPU-only stub.
- Deliverable: originally research and design only. **Revised by the user:** research carefully, then
  implement standalone proofs of concept that prove or disprove each theory with measurements (see
  "Proof-of-concept plan" below).
- df64: accuracy-preserving fp64 substitute.

This plan is saved to `/Users/aalbino/.claude/plans/logical-honking-ripple.md` because plan mode only
allows writing there. On approval, copy it to `.claude/planning/vulkan-gpu.md` in the repo as requested.
No code exists yet: there is no `src/mongo/gpu`, no Vulkan, no SPIR-V and no VMA in the tree.

## Proof-of-concept plan (what gets implemented now)

Standalone C++ prototypes under `poc/gpu/` on the `vulkan-gpu` branch, built with CMake and kept out of
the Bazel tree, so there is no risk to the server build. Dev machine (checked read-only): Apple M1 Pro,
arm64, MoltenVK, Vulkan SDK 1.3.283, plus `glslc`, `glslangValidator`, `spirv-opt`, `spirv-val`,
`cmake`, `uv` and numpy. Nothing needs installing. Run `vulkaninfo` first to record the real limits
(`shaderFloat64`, `shaderInt64`, subgroup size, storage-buffer range). Apple GPUs have no fp64, so this
machine exercises the df64 path directly.

Each POC states a theory, a measurement and a pass/fail criterion. Results go in `poc/gpu/RESULTS.md`.

1. **POC 0, environment and loader.** Vulkan compute smoke test via `dlopen` of the loader (no
   link-time dependency), device probe, and a clean CPU fallback when the loader is missing (force it
   with a bad path). Proves the dlopen and fallback design.
2. **POC 1, df64 correctness.** A GLSL compute kernel for df64 add, mul, div and sqrt, with `NoContraction`.
   Compare to a CPU `long double`/exact oracle over adversarial inputs (cancellation, wide exponents,
   denormals). Check the theory that precision is about 2^-44 relative. Run the same kernel without
   `NoContraction` to show the compiler can destroy df64, which justifies the startup self-test.
   Pass: error stays within `4*2^-44` for in-range inputs, and range-overflow inputs are detected.
3. **POC 2, segmented df64 `$sum`.** Reproduce `DoubleDoubleSummation` (`src/mongo/util/summation.h`)
   semantics on the GPU: group-id keyed segmented reduction over double values. Compare to the CPU
   reference. Measure end to end (CPU split, upload, kernel, readback) against scalar CPU and a NEON/
   autovectorized CPU sum across batch sizes from 1K to 64M. Output the break-even table. Pass or fail is
   recorded honestly. A negative result is a valid finding.
4. **POC 3, percentile and sort.** GPU radix sort of `(double-as-ordered-uint64 key, idx)` pairs and a
   percentile selection, against `std::sort` and `nth_element`. Break-even by size. Uses only `uint32`
   in the shader (no int64), which is the MoltenVK constraint.
5. **POC 4, fallback and failure handling.** A mock device that injects device-lost, OOM and timeout,
   to prove whole-batch CPU rerun and the circuit-breaker backoff. Pure C++ unit tests, no GPU needed.
6. **Findings.** Update the ranking above with measured numbers, then record the go/no-go gate for
   Phase 1 in `poc/gpu/RESULTS.md`.

Ground rules: research each API or shader technique against the real SDK headers and docs before using
it. Do not claim a speedup without a measurement. Report negative results plainly. Make no changes under
`src/` and no git push without asking. Commit POC work to the `vulkan-gpu` branch locally.

Test data and benchmarks run locally. Anything that downloads software or touches the network is out of scope.

## Where GPU helps (ranked)

Rule: the GPU wins only if `N*bytes/BW + launch (~20-100us) + sync (~50-200us) + staging < CPU time(N)`.
Integrated GPUs and Apple unified memory remove most of the transfer term, which lowers break-even
about 5-10x.

| # | Workload | Key files | Fit and break-even |
|---|---|---|---|
| 1 | Block aggregation (`$sum/min/max/count`) | `src/mongo/db/exec/sbe/stages/block_hashagg.cpp`, `exec/sbe/vm/vm_block.cpp` (`AggSum` 553, `AggDoubleDoubleSum` 614), `util/summation.h` | Segmented reductions. `tokenize()` gives dense group ids. `$sum` already uses double-double, so df64 maps directly. About 64K-256K values per batch, so many ~1000-value time-series blocks must be batched. |
| 2 | Percentile and median | `pipeline/percentile_algo_accurate.cpp:69`, `percentile_algo_continuous.cpp` | A flat `vector<double>` sort or select, plus fp64 interpolation. About 100K+ elements. |
| 3 | Sorter runs and index-build spills | `sorter/sorter_template_defs.h:694`, `db/storage/wiredtiger/wiredtiger_index.cpp` | Extract fixed-width key prefixes, radix sort `(key, idx)`, then CPU tie-fixup. About 1M+ keys. |
| 4 | `$stdDev/$avg/$covariance` | `pipeline/accumulator_std_dev.cpp:51-75` | The Welford merge is a tree reduction. Needs df64. Same batch size as #1. Needs a block-form rewrite. |
| 5 | SBE block arithmetic and comparison | `exec/sbe/vm/vm_block.cpp` 1380-2055, `values/block_interface.h:187` | Memory-bound, and CPU SIMD is cheaper. Only worth it fused into #1. Homogeneous int32/int64/double blocks only. |
| 6 | BSONColumn/Simple8b and time-series decompress | `bson/column/simple8b.inl`, `db/timeseries/bucket_compression.cpp:372` | Needs a prefix-sum scan and a word-boundary prepass. About 1K+ buckets per batch. Speculative. |
| - | FLE2 HMAC-SHA256 | `crypto/fle_crypto.cpp` | Not recommended: small batches, key exposure, FIPS. |

Poor fits (non-goals): oplog application, transport, per-document validation, matcher tree, FTS, hash
join, MD5, small-key hashes, BSON compare, geo (row-based), Decimal128. Vector search/ANN is not in
this tree. It lives in out-of-tree extensions via `extension/public/api.h`, so a GPU ANN backend would
be an extension.

Caveat: block (columnar) plans exist only for time-series and columnar scans, so the gain is confined
to those queries. The classic `$group` pipeline is row-based.

Alternative to benchmark first: CPU SIMD (SSE2/AVX2/NEON) for block ops. It may beat the GPU for #4
and #5. The tree has almost no SIMD today (`fts/unicode/byte_vector_*`).

## Phases

- **Phase 0, spike and go/no-go.** Device abstraction, CPU backend, and one df64 `sum` kernel. Benchmark
  on discrete, integrated and Apple GPUs against scalar and SIMD CPU. Stop here if no platform wins.
- **Phase 1, infrastructure.** `src/mongo/gpu`, the build flag, server parameters, metrics, fallback
  and circuit breaker.
- **Phase 2.** Block aggregation (#1, #4) and percentile (#2).
- **Phase 3 (conditional).** Sort (#3) and fused block ops (#5).
- **Phase 4 (speculative).** Bulk BSONColumn decode (#6). A WiredTiger accelerator-compressor plugin
  would follow the `ext/compressors/iaa` precedent.

## Architecture (`src/mongo/gpu/`)

- `compute_device.h` is the abstract interface: `caps()`, `allocate`, and `submit(kernel, bufs, params) -> Future<void>`.
  On top of it sit typed ops such as `segmentedSumF64`, `sortKeysU64` and `percentileF64`.
  Operators never see Vulkan.
- `cpu_backend.cpp` is the fallback and the differential-test oracle.
- `vulkan/` holds a `vk_loader` (dlopen of `libvulkan.so.1`, MoltenVK or `vulkan-1.dll`, volk-style
  tables), `vk_device`, a VMA-like `vk_allocator`, and `vk_pipeline_cache`.
- `gpu_stub.cpp` is the no-op for ppc64le/s390x and flag-off builds. It follows the precedent of
  `src/mongo/otel/metrics/instrumentation/disk_metrics_{linux,noop}.cpp`.
- `gpu_manager` is a `ServiceContext` decoration created in a `MONGO_INITIALIZER_GENERAL`
  (`src/mongo/base/init.h`). A failed probe logs and selects the CPU backend, and never fails startup.
- Register the module in `modules_poc/modules.yaml` and mark headers with `src/mongo/util/modules.h`.
- Buffers: device-local arenas plus a host-visible staging ring. Use direct host-coherent memory on UMA
  devices. Enforce a hard `gpuMaxMemoryBytes` budget, and send oversized work to the CPU.
- Pipelines are cached by (kernel, specialization constants, device UUID). The persisted cache tolerates corruption.
- Async: timeline semaphores, with fences as fallback. The caller waits via
  `opCtx->waitForConditionOrInterrupt`, so killOp and maxTimeMS work. Kernels run in chunks of about
  5-10 ms to stay under Windows TDR and the macOS watchdog. Never hold a lock or ticket while waiting.
- Admission: a semaphore bounds concurrent GPU jobs, and excess work goes to the CPU.
- Failure: on device-lost, OOM or timeout, rerun the whole batch on the CPU (never mix partial results).
  Then a circuit breaker disables the GPU with exponential backoff and a canary re-probe.

## df64 design

- Representation: `(hi, lo)` float32 with `|lo| <= ulp(hi)/2`, giving about 44-48 mantissa bits. The CPU
  splits and reassembles: `hi = (float)d`, `lo = (float)(d - hi)`.
- Algorithms: branch-free Knuth `twoSum`. `twoProd` via FMA, or Dekker/Veltkamp (split 4097) if FMA is
  slow. Accurate add. Mul, div (one Newton step) and sqrt derived from these.
- Precision is about 2^-44 relative for add and mul. It is not bit-identical to IEEE double.
- Range: float32 exponent range is about 1e±38. A CPU preflight (vectorized with the split) falls back
  when any non-zero `|x|` is above about 1e30 or below about 1e-30, or is NaN or Inf. The shader reports an
  overflow flag, and the batch is rerun on the CPU.
- Every 2Sum/2Prod op must carry `NoContraction` and never `RelaxedPrecision`. Otherwise the compiler
  collapses df64 to plain float32. A startup known-answer self-test disables df64 kernels on broken drivers.
- Native fp64: query `shaderFloat64` plus a startup fp64:fp32 microbenchmark. Prefer native when the
  ratio is better than about 1:4. Decimal128 and NaN/Inf stay on the CPU.
- Verification: a differential harness against an exact/long-double oracle, with the bound
  `relerr <= 4*2^-44 * (sum|x_i| / |sum x_i|)`. Optional production spot-checks disable a kernel that exceeds it.

## Toolchain and platform notes

- Shaders in GLSL or Slang. Compile with glslang and `spirv-opt`. **Check the SPIR-V blobs in** as
  generated C arrays, with a CI check that they match the sources, because the Bazel tree has no shader
  compiler. An optional later step is a `python_genrule` (`bazel/config/python_genrule.bzl`).
- Baseline is Vulkan 1.1 / SPIR-V 1.3 (what MoltenVK reliably supports). Avoid shader `int64` and 64-bit
  atomics (use `uint32` pairs). Use specialization constants for workgroup size and query `subgroupSize`.
- MoltenVK has no fp64 and tight storage-buffer limits, and needs `VK_KHR_portability_subset`. A driver
  denylist keyed by vendorID/driverVersion is editable via a server parameter.

## Config, observability, security

- Build: a `bool_flag` `gpu_acceleration` plus a `config_setting` in `bazel/config/BUILD.bazel` (the
  `compiledb`/`ssl` pattern), and a `select()` between the real backend and the stub in `mongo_cc_library` deps.
  There is no link-time Vulkan dependency. Vulkan-Headers, volk and VMA are vendored under
  `src/third_party/<lib>/BUILD.bazel`, with `README.third_party.md` and `sbom.json` entries.
- IDL server parameters: `gpuAccelerationMode` (`off|auto|force`, default `off`), `gpuMinBatchElements`,
  `gpuMaxMemoryBytes`, `gpuExactResults` (default true), `gpuDeviceIndex`, `gpuDriverDenylist`. Gate operator
  integration with a feature flag (`src/mongo/db/feature_flag.h`).
- Metrics: use `MetricsService` (`src/mongo/otel/metrics`) via the serverStatus adapter, for a
  `serverStatus.gpu` section: device and driver, fallback counts by reason, per-kernel latency histograms,
  bytes transferred, queue depth. `explain` shows `gpuUsed` and `fallbackReason`.
- Security: SPIR-V is embedded only. The GPU stays off in FIPS mode and never touches key material. Zero
  staging buffers after use. Avoid shared or virtualized GPUs.

## Verification (for the eventual implementation)

- CPU-backend unit tests always run. Hardware tests use `mongo_cc_unit_test` with a `requires_gpu` tag.
- Differential and property tests, GPU vs CPU: adversarial exponents, cancellation, NaN/Inf, denormals,
  int overflow promotion, empty batches, 1 group and 1M groups. Exact for integer ops, min/max and sort
  permutations. Error-bounded for df64.
- CI: lavapipe/SwiftShader for functional coverage on every platform (not performance), plus a nightly
  real-GPU job including Apple Silicon. Failpoints simulate device loss, OOM, timeout and killOp mid-flight.
- jstests with a `requires_gpu` tag and a `gpuAccelerationMode: force` suite over the aggregation correctness tests.
- `mongo_cc_benchmark` sweeps batch size end to end to produce the break-even table.

## Risks and open questions

- Results are not bit-identical to the CPU. Different nodes of a replicated or sharded cluster could
  return different last bits. This may restrict GPU use to explicit opt-in (`gpuExactResults: false`).
  **Open: is that acceptable at all?**
- The speedup may not materialize (small blocks, transfer cost, CPU SIMD). Phase 0 is the gate.
- Few server deployments have GPUs, so the audience may be small.
- Maintenance cost: shaders, checked-in blobs, driver quirks, CI matrix.
- Open: is a checked-in generated binary blob acceptable? Which hardware tiers are officially supported?

Non-goals: GPU use in the matcher, transport, storage I/O, replication, FLE and Decimal128; ANN (an extension); CUDA,
Metal or ROCm backends; multi-GPU; any behavior change when the flag is off.
