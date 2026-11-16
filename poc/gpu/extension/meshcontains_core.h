// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org and exactmesh contributors (algorithm and structure derived from exactmesh harness/pip.h, Apache-2.0; see poc/gpu/LICENSING.md)
// MP14d step 1: the CPU path of $meshContains. Exact point-in-solid for a closed triangle mesh by ray parity along +x with Simulation of Simplicity
// (the query p is replaced by p' = (px, py + e, pz + e^2), e infinitesimal), and an exact surface test. Same algorithm as exactmesh `harness/pip.h`
// (Apache-2.0, zrecore-org), re-implemented here against this extension's own BigInt and with a floating-point-filtered predicate in front of the
// exact one. The filter constants are Shewchuk's published error bounds for orient2d and orient3d (J. R. Shewchuk, "Adaptive Precision Floating-Point
// Arithmetic and Fast Robust Geometric Predicates", 1997; predicates.c, public domain); the exact fallback is integer arithmetic, so no code is shared.
// CONTRACT: IN means an odd number of surface crossings of the +x ray (parity), not a winding number: for self-intersecting, nested same-orientation or
// inside-out meshes it can differ from the sign of the exact volume. Query points must be finite (the stage rejects NaN/Inf before calling). Build with
// -ffp-contract=off (no fused multiply-add): the filter's error analysis assumes every operation is rounded separately. Never -ffast-math.
// Verdicts: kOut, kIn, kOn (exactly on the surface). Parity needs no orientation; the mesh must be closed (validated by parsePacked/volume path).
#pragma once
#include "meshvol_core.h"
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <vector>

namespace meshcontains {
using meshvol::BigInt;
#ifdef MESHCONTAINS_COUNTERS
struct Counters { long o2Filter = 0, o2Exact = 0, o3Filter = 0, o3Exact = 0, tieFirst = 0, tieSecond = 0, tieNone = 0, bboxSkip = 0, coplanarTrue = 0, coplanarFalse = 0, edgeOn = 0; };
inline thread_local Counters g_cnt;
#define MC_COUNT(f) (++::meshcontains::g_cnt.f)
#else
#define MC_COUNT(f) ((void)0)
#endif
#ifndef MESHCONTAINS_MUT
#define MESHCONTAINS_MUT 0
#endif
enum Verdict : uint8_t { kOut = 0, kIn = 1, kOn = 2 };

inline int sgn(double x) { return (x > 0) - (x < 0); }
// Shewchuk's error bounds assume no underflow or overflow in the products. A difference that is zero, or whose magnitude is within [lo, hi], keeps every
// product of up to three of them inside the normal range; anything else (subnormal differences, huge values, NaN) goes straight to the exact integer path.
#ifdef MESHCONTAINS_MUT_NO_WINDOW
inline bool inWindow(double, double, double) { return true; }   // mutant: trust the float filter everywhere (wrong under underflow/overflow)
#else
inline bool inWindow(double x, double lo, double hi) { const double a = std::fabs(x); return a == 0 || (a >= lo && a <= hi); }
#endif

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
    const double d0 = a[0] - c[0], d1 = b[1] - c[1], d2 = a[1] - c[1], d3 = b[0] - c[0];
    if (inWindow(d0, 1e-150, 1e150) && inWindow(d1, 1e-150, 1e150) && inWindow(d2, 1e-150, 1e150) && inWindow(d3, 1e-150, 1e150)) {
        const double l = d0 * d1, r = d2 * d3, det = l - r;
        const double bound = 3.3306690738754716e-16 * (std::fabs(l) + std::fabs(r));   // ccwerrboundA = (3 + 16 eps) eps, eps = 2^-53
        if (det > bound) { MC_COUNT(o2Filter); return 1; }
        if (det < -bound) { MC_COUNT(o2Filter); return -1; }
#if MESHCONTAINS_MUT == 11
        return sgn(det);   // mutant: the filter's answer trusted without the exact fallback (orient2d)
#endif
    }
    MC_COUNT(o2Exact);
    double in[6] = {a[0], a[1], b[0], b[1], c[0], c[1]};
    BigInt v[6]; toInts(in, 6, v);
    return BigInt((v[0] - v[4]) * (v[3] - v[5]) - (v[1] - v[5]) * (v[2] - v[4])).sign();
}
// Shewchuk's orient3d convention: positive when d lies below the plane abc, a b c counter-clockwise seen from above
inline int orient3dSign(const double* a, const double* b, const double* c, const double* d) {
    const double adx = a[0] - d[0], bdx = b[0] - d[0], cdx = c[0] - d[0], ady = a[1] - d[1], bdy = b[1] - d[1], cdy = c[1] - d[1],
                 adz = a[2] - d[2], bdz = b[2] - d[2], cdz = c[2] - d[2];
    if (inWindow(adx, 1e-90, 1e90) && inWindow(bdx, 1e-90, 1e90) && inWindow(cdx, 1e-90, 1e90) && inWindow(ady, 1e-90, 1e90) && inWindow(bdy, 1e-90, 1e90) &&
        inWindow(cdy, 1e-90, 1e90) && inWindow(adz, 1e-90, 1e90) && inWindow(bdz, 1e-90, 1e90) && inWindow(cdz, 1e-90, 1e90)) {
        const double bdxcdy = bdx * cdy, cdxbdy = cdx * bdy, cdxady = cdx * ady, adxcdy = adx * cdy, adxbdy = adx * bdy, bdxady = bdx * ady;
        const double det = adz * (bdxcdy - cdxbdy) + bdz * (cdxady - adxcdy) + cdz * (adxbdy - bdxady);
        const double permanent = (std::fabs(bdxcdy) + std::fabs(cdxbdy)) * std::fabs(adz) + (std::fabs(cdxady) + std::fabs(adxcdy)) * std::fabs(bdz) +
                                 (std::fabs(adxbdy) + std::fabs(bdxady)) * std::fabs(cdz);
        #if MESHCONTAINS_MUT == 13
        const double bound = 7.771561172376103e-18 * permanent;   // mutant: bound 100x too small
#else
        const double bound = 7.771561172376103e-16 * permanent;
#endif                         // o3derrboundA = (7 + 56 eps) eps
        if (det > bound) { MC_COUNT(o3Filter); return 1; }
        if (det < -bound) { MC_COUNT(o3Filter); return -1; }
#if MESHCONTAINS_MUT == 12
        return sgn(det);   // mutant: trusted without the exact fallback (orient3d)
#endif
    }
    MC_COUNT(o3Exact);
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
    if (s != 0) { MC_COUNT(tieFirst); return s; }
#if MESHCONTAINS_MUT == 14
    return 0;   // mutant: second tie-break term dropped
#endif
    s = sgn(v[1] - u[1]);
    MC_COUNT(tieSecond);
    if (s == 0) MC_COUNT(tieNone);
    return s;
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
    #if MESHCONTAINS_MUT == 16
    return nonDegenerate && BigInt(dot(c0, n)).sign() > 0 && BigInt(dot(c1, n)).sign() > 0 && BigInt(dot(c2, n)).sign() > 0;   // mutant: boundary of the triangle excluded
#elif MESHCONTAINS_MUT == 17
    return BigInt(dot(c0, n)).sign() >= 0 && BigInt(dot(c1, n)).sign() >= 0 && BigInt(dot(c2, n)).sign() >= 0;   // mutant: degenerate-triangle guard removed
#else
    return nonDegenerate && BigInt(dot(c0, n)).sign() >= 0 && BigInt(dot(c1, n)).sign() >= 0 && BigInt(dot(c2, n)).sign() >= 0;
#endif
}

// One triangle's contribution for query p: returns true when p is exactly on this closed triangle (verdict kOn), else adds 0 or 1 to *crossings.
// A triangle whose yz bounding box (compared exactly, doubles) does not contain p's (y, z) can neither be crossed by the +x ray through p' nor contain p.
inline bool triangleStep(const std::vector<double>& v, const std::vector<uint32_t>& t, size_t k, const double* p, int* crossings) {
    const double* a = &v[3 * (size_t)t[3 * k]];
    const double* b = &v[3 * (size_t)t[3 * k + 1]];
    const double* c = &v[3 * (size_t)t[3 * k + 2]];
    const double ymin = std::fmin(a[1], std::fmin(b[1], c[1])), ymax = std::fmax(a[1], std::fmax(b[1], c[1]));
    const double zmin = std::fmin(a[2], std::fmin(b[2], c[2])), zmax = std::fmax(a[2], std::fmax(b[2], c[2]));
#if MESHCONTAINS_MUT == 15
    if (p[1] <= ymin || p[1] >= ymax || p[2] <= zmin || p[2] >= zmax) return false;   // mutant: closed box treated as open
#else
    if (p[1] < ymin || p[1] > ymax || p[2] < zmin || p[2] > zmax) { MC_COUNT(bboxSkip); return false; }
#endif
    const int s0 = orientYZ(a, b, p), s1 = orientYZ(b, c, p), s2 = orientYZ(c, a, p);
    const int o3 = orient3dSign(a, b, c, p);
    if (o3 == 0) { if (coplanarInClosedTriangle(a, b, c, p)) { MC_COUNT(coplanarTrue); return true; } MC_COUNT(coplanarFalse); }
    if (s0 == 0 || s1 == 0 || s2 == 0) return false;
    if (!(s0 == s1 && s1 == s2)) return false;
    const double ayz[2] = {a[1], a[2]}, byz[2] = {b[1], b[2]}, cyz[2] = {c[1], c[2]};
    const int nx = orient2dSign(ayz, byz, cyz);
#if MESHCONTAINS_MUT == 18
#else
    if (nx == 0) return false;   // edge-on triangle
#endif
    int side = o3;
    // Unreachable in exact arithmetic: o3 == 0 means p is in the plane; s0 == s1 == s2 != 0 and nx != 0 mean p' is inside the open projected triangle, so p lies in the
    // closed triangle and the ON test above already returned. Kept to mirror the oracle (exactmesh harness/pip.h), which has the same branch; no mutant can reach it.
    if (side == 0) {
        double in[9] = {a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]};
        BigInt w[9]; toInts(in, 9, w);
        BigInt ux = w[3] - w[0], uy = w[4] - w[1], uz = w[5] - w[2], wx = w[6] - w[0], wy = w[7] - w[1], wz = w[8] - w[2];
        BigInt ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
        side = -ny.sign();
        if (side == 0) side = -nz.sign();
    }
    if (side == nx) (*crossings)++;
    return false;
}

// All triangles (the reference path; O(nt) per point).
inline Verdict classify(const std::vector<double>& v, const std::vector<uint32_t>& t, const double* p) {
    int crossings = 0;
    const size_t nt = t.size() / 3;
    for (size_t k = 0; k < nt; k++) if (triangleStep(v, t, k, p, &crossings)) return kOn;
#if MESHCONTAINS_MUT == 19
    return crossings > 0 ? kIn : kOut;   // mutant: any crossing means inside (wrong for concave meshes)
#else
    return (crossings & 1) ? kIn : kOut;
#endif
}

// Bounding-volume hierarchy over the triangles' binary64 boxes (exact min/max, closed boxes, comparisons of doubles are exact). Median split on the longest
// centroid axis (nth_element), leaves of at most 8 triangles. The ray box of a query is [px, +inf) x {py} x {pz}. Only the tree SHAPE depends on floating
// point; which triangles are candidates depends only on exact comparisons, so the verdicts do not depend on the tree.
struct Bvh {
    struct Node { double lo[3], hi[3]; uint32_t left = 0, right = 0, begin = 0, count = 0; };  // count > 0: leaf over idx[begin, begin+count)
    std::vector<Node> nodes;
    std::vector<uint32_t> idx;
    std::vector<double> clo, chi;   // per-triangle boxes
    void build(const std::vector<double>& v, const std::vector<uint32_t>& t) {
        const size_t nt = t.size() / 3;
        clo.assign(3 * nt, 0); chi.assign(3 * nt, 0); idx.resize(nt);
        for (size_t k = 0; k < nt; k++) {
            idx[k] = (uint32_t)k;
            for (int d = 0; d < 3; d++) {
                const double a = v[3 * (size_t)t[3 * k] + d], b = v[3 * (size_t)t[3 * k + 1] + d], c = v[3 * (size_t)t[3 * k + 2] + d];
                clo[3 * k + d] = std::fmin(a, std::fmin(b, c)); chi[3 * k + d] = std::fmax(a, std::fmax(b, c));
            }
        }
        nodes.clear(); nodes.reserve(2 * nt / 4 + 2);
        if (nt) buildRange(0, (uint32_t)nt);
    }
    uint32_t buildRange(uint32_t b, uint32_t e) {
        const uint32_t me = (uint32_t)nodes.size();
        nodes.emplace_back();
        Node n; n.begin = b;
        for (int d = 0; d < 3; d++) { n.lo[d] = INFINITY; n.hi[d] = -INFINITY; }
        for (uint32_t i = b; i < e; i++) for (int d = 0; d < 3; d++) { n.lo[d] = std::fmin(n.lo[d], clo[3 * idx[i] + d]); n.hi[d] = std::fmax(n.hi[d], chi[3 * idx[i] + d]); }
        if (e - b <= 8) { n.count = e - b; nodes[me] = n; return me; }
        int axis = 0; double ext = n.hi[0] - n.lo[0];
        for (int d = 1; d < 3; d++) if (n.hi[d] - n.lo[d] > ext) { ext = n.hi[d] - n.lo[d]; axis = d; }
        const uint32_t mid = (b + e) / 2;
        std::nth_element(idx.begin() + b, idx.begin() + mid, idx.begin() + e, [&](uint32_t x, uint32_t y) {
            const double cx = clo[3 * x + axis] + chi[3 * x + axis], cy = clo[3 * y + axis] + chi[3 * y + axis];
            return cx != cy ? cx < cy : x < y; });
        n.left = buildRange(b, mid);
        n.right = buildRange(mid, e);
        nodes[me] = n;
        return me;
    }
    // candidates for the +x ray from p: triangle ids whose closed box meets [px, inf) x {py} x {pz}
    void query(const double* p, std::vector<uint32_t>* out) const {
        out->clear();
        if (nodes.empty()) return;
        uint32_t stack[128]; int sp = 0; stack[sp++] = 0;
        while (sp > 0) {
            const Node& n = nodes[stack[--sp]];
            if (n.hi[0] < p[0] || p[1] < n.lo[1] || p[1] > n.hi[1] || p[2] < n.lo[2] || p[2] > n.hi[2]) continue;
            if (n.count) {
                for (uint32_t i = n.begin; i < n.begin + n.count; i++) {
                    const uint32_t k = idx[i];
                    if (chi[3 * k] < p[0] || p[1] < clo[3 * k + 1] || p[1] > chi[3 * k + 1] || p[2] < clo[3 * k + 2] || p[2] > chi[3 * k + 2]) continue;
                    out->push_back(k);
                }
            } else { stack[sp++] = n.right; stack[sp++] = n.left; }
        }
    }
};
// Same verdict as classify(), through the BVH.
inline Verdict classifyBvh(const std::vector<double>& v, const std::vector<uint32_t>& t, const Bvh& bvh, const double* p, std::vector<uint32_t>* scratch) {
    bvh.query(p, scratch);
    int crossings = 0;
    for (uint32_t k : *scratch) if (triangleStep(v, t, k, p, &crossings)) return kOn;
#if MESHCONTAINS_MUT == 19
    return crossings > 0 ? kIn : kOut;
#else
    return (crossings & 1) ? kIn : kOut;
#endif
}
}  // namespace meshcontains
