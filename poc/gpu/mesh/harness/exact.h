// Exact geometric predicates on binary64 inputs, by integer arithmetic. Every double is m * 2^e with m an integer, so after scaling all
// inputs to the smallest exponent the determinant is an exact integer (a dyadic rational scaled by a positive power of two), and its sign is the
// exact sign. Uses Boost.Multiprecision cpp_int (vendored in src/third_party/boost/libs/multiprecision; BSL-1.0). No GPL code involved.
#pragma once
#include "mesh_io.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <cstdint>
#include <initializer_list>

namespace mesh {
using BigInt = boost::multiprecision::cpp_int;

// Scales the given doubles (finite) to integers with a common exponent. Returns false on NaN/Inf.
inline bool toCommonInts(std::initializer_list<double> xs, BigInt* out) {
    int emin = INT32_MAX;
    int e[16]; int64_t m[16]; size_t n = 0;
    for (double x : xs) {
        if (!std::isfinite(x)) return false;
        int ex; double f = std::frexp(x, &ex);                   // x = f * 2^ex, 0.5 <= |f| < 1
        int64_t mant = (int64_t)std::ldexp(f, 53);              // exact: 53-bit integer
        int exp2 = ex - 53;
        if (mant == 0) { exp2 = 0; }
        e[n] = exp2; m[n] = mant; n++;
        if (mant != 0 && exp2 < emin) emin = exp2;
    }
    if (emin == INT32_MAX) emin = 0;
    for (size_t i = 0; i < n; i++) {
        out[i] = BigInt(m[i]);
        if (m[i] != 0) out[i] <<= (e[i] - emin);
    }
    return true;
}

// Sign of the 2x2 determinant | ax-cx ay-cy ; bx-cx by-cy | : +1 counter-clockwise (Shewchuk's orient2d convention), 0 collinear.
inline int exactOrient2d(const double* a, const double* b, const double* c) {
    BigInt v[6];
    if (!toCommonInts({a[0], a[1], b[0], b[1], c[0], c[1]}, v)) return 2;
    BigInt det = (v[0] - v[4]) * (v[3] - v[5]) - (v[1] - v[5]) * (v[2] - v[4]);
    return det.sign();
}

// Sign of the 3x3 determinant with rows (a-d), (b-d), (c-d): Shewchuk's orient3d convention (positive when d lies below the plane abc,
// with a, b, c counter-clockwise seen from above). Returns 0 for exactly coplanar, 2 for non-finite input.
inline int exactOrient3d(const double* a, const double* b, const double* c, const double* d) {
    BigInt v[12];
    if (!toCommonInts({a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2], d[0], d[1], d[2]}, v)) return 2;
    BigInt adx = v[0] - v[9], ady = v[1] - v[10], adz = v[2] - v[11];
    BigInt bdx = v[3] - v[9], bdy = v[4] - v[10], bdz = v[5] - v[11];
    BigInt cdx = v[6] - v[9], cdy = v[7] - v[10], cdz = v[8] - v[11];
    BigInt det = adx * (bdy * cdz - bdz * cdy) + ady * (bdz * cdx - bdx * cdz) + adz * (bdx * cdy - bdy * cdx);
    return det.sign();
}

// Exact value num * 2^exp2 (a dyadic rational). Compare with equalExact / signExact.
struct Dyadic {
    BigInt num; int exp2 = 0;
};
inline Dyadic align(const Dyadic& a, int exp2) { Dyadic r{a.num, exp2}; if (a.exp2 > exp2) r.num <<= (a.exp2 - exp2); return r; }
inline bool equalExact(const Dyadic& a, const Dyadic& b) { int e = std::min(a.exp2, b.exp2); return align(a, e).num == align(b, e).num; }
inline int signExact(const Dyadic& a) { return a.num.sign(); }
inline Dyadic subExact(const Dyadic& a, const Dyadic& b) { int e = std::min(a.exp2, b.exp2); return {align(a, e).num - align(b, e).num, e}; }
inline Dyadic addExact(const Dyadic& a, const Dyadic& b) { int e = std::min(a.exp2, b.exp2); return {align(a, e).num + align(b, e).num, e}; }
inline Dyadic mulExact(const Dyadic& a, const Dyadic& b) { return {a.num * b.num, a.exp2 + b.exp2}; }
inline std::string toString(const Dyadic& a) { return a.num.str() + "*2^" + std::to_string(a.exp2); }

// Exact signed volume of a triangle mesh: (1/6) sum over triangles of det(a, b, c). Returned as 6 * volume (a Dyadic) so no division is needed;
// divide the *comparison*, not the value (compare 6*V of two meshes, or 6*V against 6 * the analytic volume). Positive for an outward-oriented closed mesh.
// Non-finite coordinates are not accepted (check first).
inline Dyadic exactVolumeTimes6(const Mesh& m) {
    int emin = INT32_MAX;
    std::vector<int> ex(m.v.size()); std::vector<int64_t> mant(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) {
        int e; double f = std::frexp(m.v[i], &e);
        mant[i] = (int64_t)std::ldexp(f, 53); ex[i] = mant[i] == 0 ? 0 : e - 53;
        if (mant[i] != 0 && ex[i] < emin) emin = ex[i];
    }
    if (emin == INT32_MAX) emin = 0;
    std::vector<BigInt> iv(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) { iv[i] = BigInt(mant[i]); if (mant[i] != 0) iv[i] <<= (ex[i] - emin); }
    BigInt sum = 0;
    for (size_t k = 0; k < m.nt(); k++) {
        const BigInt *a = &iv[3 * (size_t)m.t[3 * k]], *b = &iv[3 * (size_t)m.t[3 * k + 1]], *c = &iv[3 * (size_t)m.t[3 * k + 2]];
        sum += a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
    }
    return {sum, 3 * emin};
}
}  // namespace mesh
