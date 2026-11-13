# Slang evaluation plan: Bazel integration and CPU target

Question (from RESULTS.md, "Slang vs GLSL"): kernel correctness and speed are settled (M1, lavapipe, T4). Two things are
not: can Slang fit MongoDB's Bazel build, and does its CPU target give the "one source for the kernel and its CPU twin" benefit.

## A. CPU target (the claimed main benefit)
Goal: run the *same* `rsel.slang` source on the CPU and check it against GPU output and `std::nth_element`.
1. Find which CPU path works for this kernel: `-target cpp` / `host-cpp` (C++ source), `host-callable`, `shader-sharedlib`, `llvm-ir`.
   Known risk: the kernel uses `groupshared` memory, `InterlockedAdd` and `GroupMemoryBarrierWithGroupSync`; the CPU targets run one
   thread per call and may not support group barriers, in which case a kernel meant for both targets needs a restructure
   (e.g. one group per call with an explicit loop over lanes).
2. If it runs: harness drives the generated C++ over the same data and ranks, compare with the GPU result and `nth_element` (bit-exact).
3. Record what had to change in the kernel, build steps, and generated-code size / dependencies (does it need slang runtime headers?).
Pass: CPU-target output equals `nth_element` on the POC 3 data. Fail is a result too: record the blocker.

## B. Bazel integration
Goal: show a Bazel build can obtain a pinned `slangc` and compile `.slang` to SPIR-V as a build action, hermetically, on the
platforms the server builds on (linux x86_64/aarch64, macOS arm64; Windows not assessed unless cheap).
1. Read how the repo brings in prebuilt tools (`MODULE.bazel` http_archive/http_file, mdb-build-public mirror, SBOM and
   `src/third_party/README.md` policy) and how `mongo_src_rules.bzl` could take a `slang_library` rule.
2. Prove the mechanism in a standalone mini Bazel module under `poc/gpu/bazel_slang/` (NOT in the repo's MODULE.bazel): `http_archive` of the
   release per platform with sha256, a small rule/genrule producing `.spv` from `.slang`, a test that the output validates and is
   byte-identical to the hand-built one. Needs bazel (install via the repo's `buildscripts/install_bazel.py` or bazelisk into the scratchpad).
3. Write up: what MongoDB's policy would require (mirror, fork, SBOM entry, license review: Apache-2.0 with LLVM exception plus
   third-party notices in the release), build-time vs runtime use (precompiled SPIR-V checked in or generated at build, no runtime slangc),
   Windows/other-platform gaps.
Pass: `bazel build` of the mini module produces the same SPIR-V on this machine. CI on Linux is optional.

## Out of scope
Touching `MODULE.bazel`, `src/`, or vendoring Slang into the repo; running a full mongod build.
