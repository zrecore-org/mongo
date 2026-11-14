# Newest Geogram retest (2026-11-13)

Question: does the newest Geogram fix the corner-contact failure (`box_vertex_touch`)? Answer: no.

## Versions
- Latest GitHub release/tag (API `repos/BrunoLevy/geogram/releases` and `/tags`): **v1.10.1**, published 2026-09-01. This is the version already pinned (commit bbcacba146b357926b5524111f56cc70c42182a7). There is no newer release, so the "newest tag" retest is the existing MP04 result.
- Unreleased `origin/main` (fetched 2026-11-13): commit `e6f8667a883b97cb5d1046caf702624f0cfa8acd` (committer date 2026-11-13), reports GEOGRAM_VERSION 1.10.2-rc. Tested here as an extra. It is not a release.
- GitHub source tarball sha256 (downloaded with curl, 2026-11-13):
  - v1.10.1: 24b661c13627d64120efd05217ddc6b12741e2d22698309bb2128416b439f2d1
  - main e6f8667a: d4dc0f41ccc7852bc869919d45c3d0e51680cc634c07059ed815823b2773d297
  (Tarball hashes are of GitHub's generated archives; GitHub does not guarantee they are stable. The four non-graphics submodules are at the same commits as in REPORT.md; they are not in the tarballs.)

## Release notes / commits read
v1.10.1..main touches boolean code: `mesh_surface_intersection*.cpp/h`, `triangle_intersection.cpp`, `expansion_nt.cpp`, `interval_nt.h`, `multi_precision.*` (9 files, 89+/70-). Relevant commit subjects: "BooleanExpression: support parallel invocations ... classify facets in parallel", "IsectInfo sort: no need to sort by f2", "Removed mechanism to detect overlapping facet in MeshInTriangle", "Exact rounding for computing heights for expansion-based kernel", "do not allocate long expansions on stack in ratio_compare()". None mentions vertex or corner contact, coplanar handling or the huge-offset cases. (Commit subjects only; I did not read the diffs line by line.) No GitHub release notes text was read beyond the tag list.

## Build (external volume, separate from storage/mesh/geogram)
Same flags as REPORT.md section 1 (TetGen/Triangle/HLBFGS OFF, graphics OFF, LIB_ONLY, static Darwin-aarch64-clang), source copied from storage/mesh/geogram/src then `git checkout --detach origin/main`:
```
export TMPDIR=/Volumes/CrucialX9/Projects/mongo/storage/tmp
cd storage/mesh/geogram_new/src
cmake -S . -B ../build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DVORPALINE_PLATFORM=Darwin-aarch64-clang -DGEOGRAM_WITH_TETGEN=OFF -DGEOGRAM_WITH_TRIANGLE=OFF -DGEOGRAM_WITH_HLBFGS=OFF -DGEOGRAM_WITH_GRAPHICS=OFF -DGEOGRAM_WITH_LEGACY_NUMERICS=OFF -DGEOGRAM_WITH_LUA=OFF -DGEOGRAM_WITH_TBB=OFF -DGEOGRAM_WITH_FPG=OFF -DGEOGRAM_WITH_GARGANTUA=OFF -DGEOGRAM_LIB_ONLY=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cd ../build && ninja -j 3 -v > ../ninja_build.log 2>&1      # 148 steps, 0 mentions of third_party/tetgen|triangle|HLBFGS in the log
cd /Volumes/CrucialX9/Projects/mongo && GEOGRAM_DIR=$PWD/storage/mesh/geogram_new sh poc/gpu/mesh/lib/geogram/build.sh
```
The nm license check (check_license_proof.sh) was NOT re-run for this build (unverified beyond the ninja-log grep).

## Scoring (same tool, whole corpus because it is cheap, 93 case x op runs, ~seconds)
```
export TMPDIR=$PWD/storage/tmp GEOGRAM_RUNNER_THREADS=1
python3 poc/gpu/mesh/oracle/score_library.py geogram_new storage/mesh/geogram_new/bin/geogram_runner poc/gpu/build/corpus --outdir storage/mesh/geogram_new/score
```
Baseline: `scoring_results_threads1.tsv` (pinned v1.10.1, amendment-3 scoring).

| | v1.10.1 (pinned) | main e6f8667a |
|---|---|---|
| valid-input rows | 63 PASS / 9 FAIL | 63 PASS / 9 FAIL |
| malformed rows | 6 PASS / 14 ACCEPTED-UNVERIFIED / 1 FAIL | identical |
| verdict differences (93 rows) | | 0 |

The 9 valid-input failures are the same rows: `box_vertex_touch` (union, intersection, difference: 1 cell and 197 sample points misclassified, byte-identical output to v1.10.1), `box_huge_offset_overlap` x3 and `box_huge_offset_one_ulp` x3 (still wrong; per-case error magnitudes for overlap differ slightly, e.g. union -1.4667 became -0.5333, still FAIL). No new failures. 55 of 93 output files differ byte-wise from v1.10.1 (ordering/threading changes in the classifier; verdicts unchanged), which was not investigated.

## Caveats
- Single machine (M1), GEOGRAM_RUNNER_THREADS=1, one run per row for the new build.
- A fix on a later unreleased commit than e6f8667a, or with non-default flags, is not covered.
