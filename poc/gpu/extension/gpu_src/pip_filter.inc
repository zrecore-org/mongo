// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org contributors
// MP12: ray-parity point-in-solid, one triangle at a time, in exact soft binary64 (one text for C++ and GLSL; include f64soft.inc and orient_filter.inc first).
// The ray runs along +x from the query point p; the exact CPU reference is mesh::classifyPoint (harness/pip.h), which resolves ties by a symbolic perturbation
// p' = (py + e, pz + e^2). This function returns, for one triangle (a, b, c) and p:
//   PIP_NOCROSS  provably no crossing and p is provably not on the triangle,
//   PIP_CROSS    provably one crossing in the +x direction,
//   PIP_UNCERT   anything else (a zero or uncertain filter sign, p possibly on the triangle): the exact CPU path decides the whole query.
// Soundness rests on the same facts as the exact code: a definite non-zero sign pattern does not depend on the perturbation, and a point on the closed triangle can
// never produce strictly mixed projected signs, so it always ends as PIP_UNCERT.
#ifndef PMUT
#define PMUT 0
#endif
#define PIP_NOCROSS 0
#define PIP_CROSS 1
#define PIP_UNCERT 2
// orient2d stage A: (ax-cx)(by-cy) - (ay-cy)(bx-cx) with Shewchuk's ccwerrboundA = (3 + 16 eps) eps, eps = 2^-53 (the host test asserts the constant). Window as orient3d.
#define ORIENT2D_ERRBOUND_A_BITS 0x3CB8000000000004ul
FN int orient2dFilterSoft(uint64_t ax, uint64_t ay, uint64_t bx, uint64_t by, uint64_t cx, uint64_t cy) {
    if (!(inWindow(ax) && inWindow(ay) && inWindow(bx) && inWindow(by) && inWindow(cx) && inWindow(cy))) return 0;
    uint64_t detleft = f64_mul(f64_sub(ax, cx), f64_sub(by, cy));
    uint64_t detright = f64_mul(f64_sub(ay, cy), f64_sub(bx, cx));
    uint64_t det = f64_sub(detleft, detright);
#if PMUT == 1
    if (f64_gt(det, 0ul)) return 1;
    if (f64_gt(f64_neg(det), 0ul)) return -1;
    return 0;
#else
    uint64_t bound = f64_mul(ORIENT2D_ERRBOUND_A_BITS, f64_add(f64_abs(detleft), f64_abs(detright)));
    if (f64_gt(det, bound)) return 1;
    if (f64_gt(f64_neg(det), bound)) return -1;
    return 0;
#endif
}
FN int pipOrient3d(uint64_t ax, uint64_t ay, uint64_t az, uint64_t bx, uint64_t by, uint64_t bz, uint64_t cx, uint64_t cy, uint64_t cz, uint64_t dx, uint64_t dy, uint64_t dz) {
#if PMUT == 1
    uint64_t d, b;
    orient3dFilterSoftEx(ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz, d, b);
    if (f64_gt(d, 0ul)) return 1;
    if (f64_gt(f64_neg(d), 0ul)) return -1;
    return 0;
#else
    return orient3dFilterSoft(ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz);
#endif
}
FN bool f64_lt(uint64_t a, uint64_t b) { return f64_gt(b, a); }
FN uint pipTri(uint64_t ax, uint64_t ay, uint64_t az, uint64_t bx, uint64_t by, uint64_t bz, uint64_t cx, uint64_t cy, uint64_t cz, uint64_t px, uint64_t py, uint64_t pz) {
    // projection box in (y, z): strictly outside means p is not in the closed projected triangle (exact comparisons)
    uint64_t ymin = ay, ymax = ay, zmin = az, zmax = az;
    if (f64_lt(by, ymin)) ymin = by; if (f64_gt(by, ymax)) ymax = by; if (f64_lt(cy, ymin)) ymin = cy; if (f64_gt(cy, ymax)) ymax = cy;
    if (f64_lt(bz, zmin)) zmin = bz; if (f64_gt(bz, zmax)) zmax = bz; if (f64_lt(cz, zmin)) zmin = cz; if (f64_gt(cz, zmax)) zmax = cz;
    if (f64_gt(py, ymax) || f64_lt(py, ymin) || f64_gt(pz, zmax) || f64_lt(pz, zmin)) return PIP_NOCROSS;
    int s0 = orient2dFilterSoft(ay, az, by, bz, py, pz), s1 = orient2dFilterSoft(by, bz, cy, cz, py, pz), s2 = orient2dFilterSoft(cy, cz, ay, az, py, pz);
#if PMUT == 3
    if (s0 == 0) s0 = 1; if (s1 == 0) s1 = 1; if (s2 == 0) s2 = 1;   // planted: an exact zero (ray through an edge or vertex) counts as positive
#endif
    if ((s0 != 0 && s1 != 0 && s0 != s1) || (s1 != 0 && s2 != 0 && s1 != s2) || (s0 != 0 && s2 != 0 && s0 != s2)) return PIP_NOCROSS;
    if (s0 == 0 || s1 == 0 || s2 == 0) {
#if PMUT == 2
        return PIP_NOCROSS;                                            // planted: an uncertain triangle is treated as no crossing
#else
        return PIP_UNCERT;
#endif
    }
    int nx = orient2dFilterSoft(ay, az, by, bz, cy, cz);
    int side = pipOrient3d(ax, ay, az, bx, by, bz, cx, cy, cz, px, py, pz);
#if PMUT == 4
    if (nx != 0 && side == 0) return PIP_CROSS;                        // planted: wrong tie (p in the plane counted as a crossing)
#endif
    if (nx == 0 || side == 0) return PIP_UNCERT;
    return side == nx ? PIP_CROSS : PIP_NOCROSS;
}
