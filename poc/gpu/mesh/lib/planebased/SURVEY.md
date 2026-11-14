# Plane-based fixed-width-integer mesh booleans: survey

Date 2026-11-13. Papers are in /Volumes/CrucialX9/Projects/mongo/storage/papers/:
`campen-kobbelt-2010.pdf`, `octree-bsp-2103.02486.pdf`, `ember-2022.pdf`, `levy-2405.12949v2.pdf` (+ .txt extractions).
Anything not read in a primary source is marked UNVERIFIED.

## Bottom line

- The family is exact only after snapping inputs to a small integer grid (26 bit in the shipped systems). It does not round-trip binary64, and it does not resolve 1e-12 features at 26 bit (resolution about 1.5e-8 of the half-extent).
- Reaching 1e-12 needs 40-bit positions, hence about 384-bit arithmetic with the paper's own bound. Binary64 round-trip needs far more (table below).
- The only authoritative source code is not public: EMBER is "on request" as a binary (project page). The octree-BSP paper states no code release. The one public C++ "EMBER" repo is a third-party community port, MIT-licensed, but it depends on a library (`integer-plane-geometry` in mesh-kernel) that has NO license file, and its own README admits it is not exact in the final classification step.
- No GPU variant found in any source I read.

## 1. Input representation and bit widths

### Campen & Kobbelt 2010 (CGF 29(2), 397-406)
- Sec. 3.1: input vertex coordinates have precision L bits incl. sign (2^L grid positions along each axis of the bounding box). delta <= 2^(K-1-L) is the longest edge relative to the bounding box. Plane coefficients need M = (L-1) + 2(K-1) + 3 + 1 bits, giving "M >= 3L + 2 log2(delta) + 3".
- Their prototype embeds plane coefficients in doubles (M = 53) or x87 long double (M = 64). With M=53 and L=20 they get delta <= 2^-5; with M=64, L=22, delta <= 2^-2.5; full float (L=24) only if delta <= 2^-5.5 (Sec. 3.1). So CK10 handles about 22-24 bit inputs in the prototype.
- Plane coefficient width only. The paper does not give a closed-form for the vertex-construction or 4x4 predicate width in the passages I read (UNVERIFIED whether given elsewhere).
- Arithmetic check for 1e-12 (L=40, delta=1): M >= 3*40+3 = 123 bits for plane coefficients. For binary64 round-trip the formula blows up (below).

### Nehring-Wirxel/Trettner/Kobbelt 2021 (arXiv 2103.02486, CAD 2021)
- Sec. 3.1/3.2: input points must be integer; planes are integer (a,b,c,d); vertices are 4D homogeneous integers from three-plane intersection (eq. 5). Classification `sign(x^T s)*sign(x4)` (eq. 6).
- Sec. 3.2 bound: det |p q r s| <= 16 * n+^3 * d+, with d+ <= 3 v+ n+, so eq. (7): n+^4 * v+ <= 2^(b-1)/48. With normals from triangle edges, n+ = 8 v+^2, eq. (8)/(9): v+^9 <= 2^(b-1)/(8^4*48), i.e. v+ <= 0.239 * 2^(b/9). For b=256: v+ <= 8.73e7 ("slightly above 26 bit"). I re-derived: (2^255/196608)^(1/9) = 8.7298e7. OK.
- Sec. 3.2: for a 1 m^3 volume resolution is 0.22 mm (128 b), 1.58 um (192 b), 11.5 nm (256 b). Sec. 4.6: "most permissive setting uses 256 bit arithmetic with 27 bit positions and 55 bit normals". Sec. 6: "Input meshes in floating point formats have to be rounded to suitable integer coordinates ... technically a limitation"; replacing the fixed arithmetic with gmp "causes at least a 10x slowdown".

### EMBER 2022 (ACM TOG 41(4), art. 39, doi 10.1145/3528223.3530181)
- Sec. 2.2: "we accept input polygons that have 26-bit integers per 3D coordinate or that our method previously produced"; planes (a,b,c,d) with widths "chosen such that ... no intermediate result later on exceeds 256 bit". Same homogeneous intersection (eq. 3) and classify (eq. 4).
- Sec. 3.3: import scales and rounds to fill the 26-bit range: "For a 1 m^3 scene ... roughly 15 nm accuracy (almost 8 decimal digits)"; export to float/double rounds.
- Sec. 3.2: differences of homogeneous points need twice the bits, so vector arithmetic on constructed points is not closed under the fixed widths.
- Sec. 4.5.x: uses reduced widths where planes are axis aligned.

### Required widths for our targets (my arithmetic, using the Sec. 3.2 bound b >= 9L + log2(8^4*48) + 1 = 9L + 18.6, normals computed from triangle edges)
log2(8^4*48) = log2(196608) = 17.585. L = bits of max |coordinate| after scaling.

| Target | L | normal bits (2L+3) | b needed | Practical limb size |
|---|---|---|---|---|
| Shipped (check) | 26 | 55 | 252.6 | 256 (matches paper) |
| 1e-12 of largest coord, 1 grid cell = 2^-40 = 9.1e-13 | 40 | 83 | 378.6 | 384 |
| 1e-12 with about 8 cells across the feature (2^-43 = 1.1e-13) | 43 | 89 | 405.6 | 448 |
| binary64 mantissa, all coords same exponent range | 53 | 109 | 495.6 | 512 |
| binary64, 2^-11 dynamic range between largest and smallest coordinate (64 bit fixed point) | 64 | 131 | 594.6 | 640 |
| binary64 fully arbitrary: 2^1024 / 2^-1074 = 2^2098 | 2099 + sign = 2100 | 4203 | 18 919 | about 19 000 bit (296 limbs, effectively bignum) |

Notes: 1e-12 needs log2(1e12) = 39.86, so L >= 40. A 1-cell feature is only just representable; quantisation moves vertices by 0.5 cell, so L=43 is the safer figure. Exact binary64 round-trip is only practical when the real data has bounded dynamic range; general binary64 means a bignum kernel and loses the speed advantage the papers claim (their own gmp comparison: Table 1, "classify vertex" 142 cycles at 256 bit vs 1540 for gmp, about 11x; "intersect 3 planes" 402 vs 6380). Cost of the wider fixed types: Table 1 shows 128 b -> 192 b -> 256 b classify 13 -> 103 -> 142 cycles, so super-linear growth in limbs; extrapolation to 384-512 bit is UNVERIFIED (not measured by the authors).
For a mesh already on a coarse integer grid plane widths can be reduced (not needed for our case).

## 2. Source code, license, dependencies

- Campen-Kobbelt 2010: code release UNVERIFIED (the abstract mentions a web service per a search result summary; not read in primary source).
- Octree-embedded BSP 2021: the paper text contains no code-availability statement (grep for github/source code/available: no hit other than references to others' code). Search found no repository. UNVERIFIED that none exists.
- EMBER 2022: Sec. 1: "an implementation can be acquired on the project page at graphics.rwth-aachen.de/ember-exact-mesh-booleans". The project page (fetched) says only "If you are interested in a binary implementation including various additional features, please contact the authors" (trettner@shapedcode.com). No source, no license text on the page. So: binary on request, terms unknown. The paper itself is "(c) 2022 held by owner/author(s). Publication rights licensed to ACM" (copyright note on page 1).
- Third-party port: https://github.com/zalo/ember-csg, HEAD d7f9674f046f379333ad921fcf26dbbfdd6b9bd3 (2026-04-08, author Johnathon Selstad, 12 commits). LICENSE read: MIT, "Copyright (c) 2026 zalo". Dependencies per CMakeLists.txt:
  - `integer-plane-geometry`, typed-geometry, clean-core taken from the mesh-kernel checkout (README points to https://github.com/jnehringwirxel/mesh-kernel; that URL requires auth/404 from anonymous git; https://github.com/juliusnehring/mesh-kernel clones). I cloned the latter at 9f6a63ec11601a99addbab25b211f6279e57b640 (2025-06-04): there is NO LICENSE/COPYING file anywhere and no license/copyright string in src/ (checked). Default copyright applies: not usable without permission. Its README is a TODO for the mesh-kernel paper. Its .gitmodules also pull from gitlab.vci.rwth-aachen.de private-looking URLs (glow, imgui-lean, cpp-utils).
  - typed-geometry: MIT, clean-core: MIT, tinybvh (expected at ../tinybvh): MIT (GitHub license API SPDX), nanobind for the Python module (license not checked, UNVERIFIED; commonly BSD-3). CLI11 (via mesh-kernel only): GitHub API returned NOASSERTION; commonly BSD-3, UNVERIFIED.
  - Port quality (from its README, not independently tested): README misattributes the paper ("Nehring-Wirxel, Nehring-Wirxel, Kobbelt ... SIGGRAPH Asia 2024"); WNV classification uses double-precision multi-ray casting with majority vote, "robust but not exact"; subdivision can create duplicate fragments; T-junctions; single-threaded; sphere-intersection volume delta -93.8% in its own table; cube-cube union 71 ms, sphere-sphere (1024 tris) 1.1 s. Mapping to the code: coordinate quantisation in include/ember/mesh.hh uses double scale and round to the 26-bit range (MAX_COORD). Not suitable as an "exact" baseline. I did not build or run it.

## 3. Reported speed, GPU

- CK10 Sec. 7 (i7 2.67 GHz): "2.5 to 13 times faster than CGAL" and about 25x by extrapolation; CGAL could not process more than 200K faces (5.3 GB for its representation vs <300 MB for theirs). Table: CGAL 17.6 s-114 s at 25K-200K faces (row values read from extracted text; exact column mapping UNVERIFIED).
- Octree-BSP 2021: up to 2.5 M mesh-plane cuts/s on one core (abstract); Table 1 above; Sec. 5 milling (58 472 BSP subtractions, 270 tool subtractions) took 29 s; Fig. 14: only CGAL Nef also finished; cork crashed after 17 steps, CK10 after 12, mesh arrangements after 71. Fig. 14 shows ours well below CGAL Nef per step (read from plot only; no numeric ratio stated in text). Single-threaded; Sec. 6 says octree cells are parallelisable "expect an almost linear speedup", not implemented.
- EMBER Sec. 5.1 (i9-9900K, clang-12 -O3, 1000 Thingi10K pairs, 1000-100 000 faces, multithreaded): geometric mean 1.6 ms per boolean, about 15 M input triangles/s; "roughly one order of magnitude faster than the fastest inexact CSG method". Fig. 13 geometric means (labels read from the figure image, order top to bottom): CGAL 4.12 8570.1 ms, mesh arrangements 3163.9 ms, CGAL 5.4 710.9 ms, Cork 138.8 ms, QuickCSG 72.4 ms, float mesh arrangements 17.9 ms (self-intersection only), EMBER 1.6 ms. So about 440x vs CGAL 5.4 Nef and about 45x vs QuickCSG on that benchmark. Fig. 24 (20x20 beams): EMBER 4.5 ms vs CGAL 5.4 941 ms vs mesh arrangements 18 018 ms.
- Manifold is NOT in any of these papers (EMBER predates it as a benchmark; not listed among compared methods in Sec. 5.1). Any Manifold comparison: UNVERIFIED / none in primary sources.
- Lévy (arXiv 2405.12949v2, TOG 2025, Geogram, expansions + multiprecision, not plane-based): states EMBER "remains significantly faster", cites EMBER timings 4.5 ms on 20rods and 5.9 ms on rot_cube_20 (Sec. 3.2/results). Geogram licensing is outside this task (see MP04).
- GPU: no mention in CK10, octree-BSP, EMBER (grep) or on the project page. A web search for 2024-2026 GPU exact plane-based booleans found nothing. UNVERIFIED that none exists beyond that.

## 4. Non-manifold / open input

- CK10 Sec. 7: "Degenerate contact situations that result in non-manifold edges or vertices are handled by our scheme" (output can be split). Self-intersections handled (Sec. 5, outer hull). Open input: not stated in the passages read; the intro notes CGAL Nef can represent open boundaries, in contrast implying their method works on closed solids (UNVERIFIED).
- Octree-BSP Sec. 6: "Currently, input meshes are required to be watertight and self-intersection free." Sec. 4.6: non-watertight input "can contain superfluous microgeometry and misclassified cells". Self-intersections could be added via CK10 or winding numbers, "easy to lift" (author claim).
- EMBER Sec. 3.4: input must induce a piecewise-constant integer generalised winding number field (PWN, Jacobson 2013); "many self-intersecting, non-manifold, or degenerate configurations are supported". Output is a soup of convex polygons with T-junctions (Sec. 6, "not triangulated and contains T-junctions"). Truly open surfaces (non-zero boundary) are not PWN, so excluded (inference from the PWN definition, not an explicit sentence). Output is also rounded to float/double on export (Sec. 3.3).

## Fit for MongoDB goals (assessment)

Pros: exact, fast on CPU, parallel-friendly structure (k-d subdivision + independent leaves). Cons: needs 40+ bit grid (about 384-448 bit arithmetic, no GPU-native 64-bit-integer path, Vulkan/Metal have no int64 multiply-high widely; UNVERIFIED claim per platform), no usable source licence for the original, no GPU precedent, output soup with T-junctions, 1e-12 requires about 3x the limb width of the shipped 26-bit design with unmeasured speed.
