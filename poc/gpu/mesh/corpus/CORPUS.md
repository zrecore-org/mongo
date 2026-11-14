# Mesh corpus (MP01)

Generated, not committed. Build the tools (`cmake --build build --target gen_corpus corpus_check` from `poc/gpu`), then `mesh/corpus/gen_corpus.sh`
writes `poc/gpu/build/corpus/` and checks it. The only committed corpus artifact is `EXPECTED_SHA256.txt` (sha256 of every generated file).

## What it contains
- `primitives/`: unit box, icospheres (subdivision 0 to 5, 20 to 20,480 triangles), a spiky non-convex star, tori (8x4 and 32x16 segments) and a flat grid (an open surface).
- `adversarial/<id>_A.msh`, `<id>_B.msh` plus `manifest.tsv` (31 cases). Classes: identical, touch (face, partial face, edge, vertex, on top of a face), nested, overlap, coplanar overlap, sliver (2^-40 thick), perturbed (shifts of 2^-20, 2^-40, 2^-52 and a one-ulp gap), huge offset (2^40, including a one-ulp shift), curved pairs (sphere/star/torus/tangent box),
  and malformed operands: open box, non-manifold edge (welded), self-intersecting, unwelded vertices, zero-area triangle, inverted, duplicate triangle.
- `sets/many_small_<N>.mshc`: N meshes of 80 to 1,280 triangles at deterministic positions and scales (the many-small workload; default N = 1000, the canonical corpus uses 200). `gen_corpus ... --large` adds icospheres of 1.3M and 5.2M triangles (the few-large workload; hundreds of MB, so write it to `./storage`, e.g. `gen_corpus ./storage/mesh/corpus_large --large --many 100000`, not to the default `build/`).

## Manifest columns
`id class tags nvA ntA nvB ntB vol6_A vol6_B vol6_union vol6_inter vol6_diff`. Volumes are **6 x volume as an exact integer times a power of two** (`num*2^exp`), so no
rounding is involved. The expectations for box pairs come from per-axis interval arithmetic on the box endpoints; `corpus_check` verifies them by an independent route (the exact tetrahedron-sum
volume of the mesh triangles). For non-box pairs, only identical pairs have expectations; the rest are tags. For malformed operands the tag `expect=...` says what a correct engine may do: refuse, repair, or return a correct result, never crash or return a wrong answer.

## Reproducibility and checks
- Every coordinate comes from `+ - * /`, `sqrt` and `ldexp` (correctly rounded or exact), built with `-ffp-contract=off`; the tori use a rational circle parametrization instead of `sin`/`cos` (libm results differ between platforms). Two runs give identical bytes, and the same hashes must come out on x86_64 and aarch64 (`gen_corpus.sh --check`; wired into CI as an informational step).
- `corpus_check` verifies, for each case: the files load with valid indices; the exact volume of both box operands equals the analytic expectation; inclusion-exclusion holds for the expected union/intersection/difference; and every objectively testable tag is true of the meshes
  (open, non-manifold-edge, inverted, unwelded-vertices, zero-area-triangle, duplicate-triangle, valid). It also requires the primitives to be closed, outward oriented manifolds.
- `gen_corpus.sh --selftest` corrupts the corpus three ways (an altered expected volume, a false tag, a moved vertex) and requires `corpus_check` to fail each time (all three are caught).
- The checker has already found a defect in the generator once: the first "non-manifold edge" case merged two boxes without identifying their coincident vertices, so no edge had more than two faces; the case now welds the shared vertices.

## Thingi10K (not fetched yet)
`fetch_thingi.sh --yes-download-9GB` downloads the raw archive into `./storage/thingi10k_raw/` (external volume, ignored by git) and verifies it against `THINGI.pin`. The pin (repo commit `2d5d3b2f3cd3711028ad75b12788c13b25559ec6`, size 9,616,033,688 bytes, sha256 `9d943391...`) was read from the Hugging Face response headers; the archive has not been downloaded, so the hash is as reported by the host and unverified until the first fetch.
Licensing: the dataset's organizing code is Apache-2.0; "each thing in the dataset has its own license" (field `license` per entry; the dataset lists 10 licenses). Fetched only, never committed, local testing only; filter by license before using any model for anything else.
Next steps for it (Wave 1): an STL reader with exact-coordinate vertex welding into `.msh`, a per-model validity tag (closed, manifold, self-intersecting) computed with the exact tools, and a license manifest.
Source: <https://github.com/Thingi10K/Thingi10K>.
