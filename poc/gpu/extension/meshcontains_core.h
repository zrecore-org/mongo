// MP14d step 1: the CPU path of $meshContains. Exact point-in-solid for a closed triangle mesh by ray parity along +x with Simulation of Simplicity
// (the query p is replaced by p' = (px, py + e, pz + e^2), e infinitesimal), and an exact surface test. Same algorithm as exactmesh `harness/pip.h`
// (Apache-2.0, zrecore-org), re-implemented here against this extension's own BigInt and with a floating-point-filtered predicate in front of the
// exact one. The filter constants are Shewchuk's published error bounds for orient2d and orient3d (J. R. Shewchuk, "Adaptive Precision Floating-Point
// Arithmetic and Fast Robust Geometric Predicates", 1997; predicates.c, public domain); the exact fallback is integer arithmetic, so no code is shared.
// Verdicts: kOut, kIn, kOn (exactly on the surface). Parity needs no orientation; the mesh must be closed (validated by parsePacked/volume path).
#pragma once
#include "meshvol_core.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace meshcontains {
using meshvol::BigInt;
enum Verdict : uint8_t { kOut = 0, kIn = 1, kOn = 2 };

inline int sgn(double x) { return (x > 0) - (x < 0); }

// Exact integers with a common exponent (finite inputs only).
inline void toInts(const double* xs, int n, BigInt* out) {
    int e[12]; int64_t m[12]; int emin = INT32_MAX;
    for (int i = 0; i < n; i++) {
        int64_t mant; int ex;
        meshvol::decode(xs[i], &mant, &ex);
        m[i] = mant; e[i] = mant == 0 ? 0 : ex;
        if (mant != 0 && ex < emin) emin = ex;
    }
    if (emin == INT32_MAX) emin = 0;
    for (int i = 0; i < n; i++) { out[i] = BigInt(m[i]); if (m[i] != 0) out[i] <<= (e[i] - emin); }
}

// sign of the 2x2 determinant of (a-c, b-c) in the plane of 2-vectors: +1 counter-clockwise (Shewchuk's orient2d convention)
inline int orient2dSign(const double* a, const double* b, const double* c) {
    const double l = (a[0] - c[0]) * (b[1] - c[1]), r = (a[1] - c[1]) * (b[0] - c[0]), det = l - r;
    const double bound = 3.3306690738754716e-16 * (std::fabs(l) + std::fabs(r));   // ccwerrboundA = (3 + 16 eps) eps, eps = 2^-53
    if (det > bound) return 1;
    if (det < -bound) return -1;
    double in[6] = {a[0], a[1], b[0], b[1], c[0], c[1]};
    BigInt v[6]; toInts(in, 6, v);
    return BigInt((v[0] - v[4]) * (v[3] - v[5]) - (v[1] - v[5]) * (v[2] - v[4])).sign();
}
// Shewchuk's orient3d convention: positive when d lies below the plane abc, a b c counter-clockwise seen from above
inline int orient3dSign(const double* a, const double* b, const double* c, const double* d) {
    const double adx = a[0] - d[0], bdx = b[0] - d[0], cdx = c[0] - d[0], ady = a[1] - d[1], bdy = b[1] - d[1], cdy = c[1] - d[1],
                 adz = a[2] - d[2], bdz = b[2] - d[2], cdz = c[2] - d[2];
    const double bdxcdy = bdx * cdy, cdxbdy = cdx * bdy, cdxady = cdx * ady, adxcdy = adx * cdy, adxbdy = adx * bdy, bdxady = bdx * ady;
    const double det = adz * (bdxcdy - cdxbdy) + bdz * (cdxady - adxcdy) + cdz * (adxbdy - bdxady);
    const double permanent = (std::fabs(bdxcdy) + std::fabs(cdxbdy)) * std::fabs(adz) + (std::fabs(cdxady) + std::fabs(adxcdy)) * std::fabs(bdz) +
                             (std::fabs(adxbdy) + std::fabs(bdxady)) * std::fabs(cdz);
    const double bound = 7.771561172376103e-16 * permanent;                         // o3derrboundA = (7 + 56 eps) eps
    if (det > bound) return 1;
    if (det < -bound) return -1;
    double in[12] = {a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2], d[0], d[1], d[2]};
    BigInt v[12]; toInts(in, 12, v);
    BigInt x0 = v[0] - v[9], y0 = v[1] - v[10], z0 = v[2] - v[11], x1 = v[3] - v[9], y1 = v[4] - v[10], z1 = v[5] - v[11], x2 = v[6] - v[9], y2 = v[7] - v[10], z2 = v[8] - v[11];
    return BigInt(x0 * (y1 * z2 - z1 * y2) + y0 * (z1 * x2 - x1 * z2) + z0 * (x1 * y2 - y1 * x2)).sign();
}

// sign of orient2d(u, v, p') in the (y, z) plane for p' = (py + e, pz + e^2): the exact value, else the first non-zero perturbation term
inline int orientYZ(const double* u, const double* v, const double* p) {
    const double U[2] = {u[1], u[2]}, V[2] = {v[1], v[2]}, P[2] = {p[1], p[2]};
    int s = orient2dSign(U, V, P);
    if (s != 0) return s;
#ifdef MESHCONTAINS_MUT_NO_TIEBREAK
    return s;  // mutant: no symbolic tie-break
#endif
    s = sgn(u[2] - v[2]);
    if (s != 0) return s;
    return sgn(v[1] - u[1]);
}

// Is p (coplanar with abc) inside the closed triangle? exact integer test.
inline bool coplanarInClosedTriangle(const double* a, const double* b, const double* c, const double* p) {
    double in[12] = {a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2], p[0], p[1], p[2]};
    BigInt v[12]; toInts(in, 12, v);
    auto sub = [&](int i, int j, BigInt* o) { for (int d = 0; d < 3; d++) o[d] = v[3 * i + d] - v[3 * j + d]; };
    BigInt ab[3], bc[3], ca[3], ap[3], bp[3], cp[3], n[3], c0[3], c1[3], c2[3];
    sub(1, 0, ab); sub(2, 1, bc); sub(0, 2, ca); sub(3, 0, ap); sub(3, 1, bp); sub(3, 2, cp);
    auto cross = [](const BigInt* x, const BigInt* y, BigInt* o) { o[0] = x[1] * y[2] - x[2] * y[1]; o[1] = x[2] * y[0] - x[0] * y[2]; o[2] = x[0] * y[1] - x[1] * y[0]; };
    auto dot = [](const BigInt* x, const BigInt* y) -> BigInt { return x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; };
    cross(ab, ap, c0); cross(bc, bp, c1); cross(ca, cp, c2); cross(ab, bc, n);
    const bool nonDegenerate = n[0] != 0 || n[1] != 0 || n[2] != 0;
    return nonDegenerate && BigInt(dot(c0, n)).sign() >= 0 && BigInt(dot(c1, n)).sign() >= 0 && BigInt(dot(c2, n)).sign() >= 0;
}

// Classifies one point against the whole mesh (all triangles). A triangle whose yz bounding box (compared exactly, doubles) does not contain p's
// (y, z) can neither be crossed by the +x ray through p' nor contain p, so it is skipped without any predicate.
inline Verdict classify(const std::vector<double>& v, const std::vector<uint32_t>& t, const double* p) {
    int crossings = 0;
    const size_t nt = t.size() / 3;
    for (size_t k = 0; k < nt; k++) {
        const double* a = &v[3 * (size_t)t[3 * k]];
        const double* b = &v[3 * (size_t)t[3 * k + 1]];
        const double* c = &v[3 * (size_t)t[3 * k + 2]];
        const double ymin = std::fmin(a[1], std::fmin(b[1], c[1])), ymax = std::fmax(a[1], std::fmax(b[1], c[1]));
        const double zmin = std::fmin(a[2], std::fmin(b[2], c[2])), zmax = std::fmax(a[2], std::fmax(b[2], c[2]));
        if (p[1] < ymin || p[1] > ymax || p[2] < zmin || p[2] > zmax) continue;
        const int s0 = orientYZ(a, b, p), s1 = orientYZ(b, c, p), s2 = orientYZ(c, a, p);
        const int o3 = orient3dSign(a, b, c, p);
        if (o3 == 0 && coplanarInClosedTriangle(a, b, c, p)) return kOn;
        if (s0 == 0 || s1 == 0 || s2 == 0) continue;
        if (!(s0 == s1 && s1 == s2)) continue;
        const double ayz[2] = {a[1], a[2]}, byz[2] = {b[1], b[2]}, cyz[2] = {c[1], c[2]};
        const int nx = orient2dSign(ayz, byz, cyz);
        if (nx == 0) continue;
        int side = o3;
        if (side == 0) {
            double in[9] = {a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]};
            BigInt w[9]; toInts(in, 9, w);
            BigInt ux = w[3] - w[0], uy = w[4] - w[1], uz = w[5] - w[2], wx = w[6] - w[0], wy = w[7] - w[1], wz = w[8] - w[2];
            BigInt ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
            side = -ny.sign();
            if (side == 0) side = -nz.sign();
        }
        if (side == nx) crossings++;
    }
    return (crossings & 1) ? kIn : kOut;
}
}  // namespace meshcontains
