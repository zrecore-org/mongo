# Slang evaluation plan: Bazel integration and CPU target

Question (from RESULTS.md, "Slang vs GLSL"): kernel correctness and speed are settled (M1, lavapipe, T4). Two things are
not: can Slang fit MongoDB's Bazel build, and does its CPU target give the "one source for the kernel and its CPU twin" benefit.

## A. CPU target (the claimed main benefit)
The radix select is integer-only, so a CPU-target pass on it proves little. The risk is in the df64 code: contraction and
reassociation in the generated C++. So the test is df64 first, rsel second.
1. Read the bundled CPU-target docs (`~/tools/slang-2026.20/share/doc`) for groupshared, barrier and atomic support.
2. Compile `df64.slang` / `selftest.slang` with `-target cpp` (source-emitting; `host-callable`/`sharedlib`/`exe` make slangc invoke its own
   downstream C++ compiler, which cannot fit a hermetic or remote Bazel build, so try them once only to record that).
3. Inspect what `precise` becomes in the C++ (pragma, volatile, nothing). Build the generated C++ with default flags and with `-ffp-contract=off`
   (Apple clang on arm64 fuses `a*b - p` by default).
4. Pass bar: CPU output is **bit-identical** to the recorded GPU outputs (M1, lavapipe, T4 produced the same df64 error figures), not just
   within 2^-44; a drifting oracle is useless. Record the compile flags this forces on Bazel.
5. Then the radix select on the CPU target against `nth_element`, noting any restructure needed for groupshared/barriers.
6. Record every header the generated C++ includes (prelude headers would need vendoring and a license entry) and generated-code size.

## B. Bazel integration
A pinned `http_archive`/`http_file` of a prebuilt tool is already precedent in `MODULE.bazel` (`scip-clang`, wasm tarball), so do not
re-prove the download. Test what is not proven, in a standalone mini module under `poc/gpu/bazel_slang/` (not the repo's MODULE.bazel):
1. slangc's dynamic libraries (`libslang-compiler`, `libslang-glslang`) resolve when run as a sandboxed action (`--spawn_strategy=sandboxed`).
2. Exec configuration: slangc passed via `tools =`, with an exec-platform `select` (remote workers are Linux; host is macOS here).
3. Hermeticity: nothing written to `$HOME`, works under `--incompatible_strict_action_env`.
4. Determinism: two clean builds give byte-identical `.spv` (the remote cache depends on it).
5. Policy write-up: mirror on mdb-build-public, SBOM entry, license review (Apache-2.0 with LLVM exception), build-time-only use.
Install bazelisk into the scratchpad, not the user's PATH. None of A or B needs the GPU (stay out of the lock queue).

## Out of scope
Touching `MODULE.bazel`, `src/`, or vendoring Slang into the repo; running a full mongod build.

## Status
Done and written up in `poc/gpu/RESULTS.md` ("Slang: CPU target and Bazel integration"). Part A: CPU twin works for barrier-free df64 arithmetic with `-ffp-contract=off`; the radix select histogram cannot be compiled (barriers/atomics). Part B: pinned-tool Bazel build is sandbox-safe and byte-reproducible on macOS arm64.
