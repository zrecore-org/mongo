// MP07 exact oracles. Boost.Multiprecision cpp_int / cpp_rational / cpp_bin_float_100 only; no shared code with own3d.h or the runners.
// Every geometry is (integers) * 2^E; E is carried next to the data and applied when a value is compared with a double.
// -DMUTANT=k plants a bug (see DECLARED.txt) so that selftest3d can prove each check is able to fail.
#pragma once
#include "c3d.h"
#include "../../harness/topology.h"
#include "../../harness/pip.h"
#include <boost/multiprecision/cpp_bin_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#ifndef MUTANT
#define MUTANT 0
#endif
namespace o3 {
using mesh::BigInt;
typedef boost::multiprecision::cpp_rational Q;
typedef boost::multiprecision::cpp_bin_float_100 F;
typedef std::array<BigInt, 3> IV; typedef std::array<Q, 3> QV;
struct IMesh { std::vector<IV> v; std::vector<std::array<int, 3>> t; int E = 0; };

inline Q pow2Q(long k) { return k >= 0 ? Q(BigInt(1) << k) : Q(1) / Q(BigInt(1) << (-k)); }
inline Q d2Q(double x) { if (x == 0) return 0; int e; double f = std::frexp(x, &e); BigInt m((long long)std::ldexp(f, 53)); return Q(m) * pow2Q(e - 53); }
inline IMesh fromCase(const c3d::IMesh& m, int exp) { IMesh o; o.E = exp; for (auto& v : m.v) o.v.push_back({BigInt(v[0]), BigInt(v[1]), BigInt(v[2])}); o.t = m.t; return o; }
// exact conversion of a double mesh to integers with a common exponent
inline IMesh fromDoubles(const mesh::Mesh& m) {
    IMesh o; int emin = INT32_MAX; std::vector<int> ex(m.v.size()); std::vector<long long> mant(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) { int e; double f = std::frexp(m.v[i], &e); mant[i] = (long long)std::ldexp(f, 53); ex[i] = mant[i] == 0 ? 0 : e - 53; if (mant[i] != 0 && ex[i] < emin) emin = ex[i]; }
    if (emin == INT32_MAX) emin = 0; o.E = emin;
    for (size_t i = 0; i < m.nv(); i++) { IV p; for (int d = 0; d < 3; d++) { size_t k = 3 * i + d; p[d] = BigInt(mant[k]); if (mant[k] != 0) p[d] <<= (ex[k] - emin); } o.v.push_back(p); }
    for (size_t k = 0; k < m.nt(); k++) o.t.push_back({(int)m.t[3 * k], (int)m.t[3 * k + 1], (int)m.t[3 * k + 2]});
    return o; }
template <class T> inline std::array<T, 3> sub(const std::array<T, 3>& a, const std::array<T, 3>& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
template <class T> inline std::array<T, 3> add(const std::array<T, 3>& a, const std::array<T, 3>& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
template <class T> inline std::array<T, 3> cross(const std::array<T, 3>& a, const std::array<T, 3>& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
template <class T> inline T dot(const std::array<T, 3>& a, const std::array<T, 3>& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline QV toQ(const IV& a) { return {Q(a[0]), Q(a[1]), Q(a[2])}; }
inline QV scaleQ(const QV& a, const Q& s) { return {a[0] * s, a[1] * s, a[2] * s}; }

// ---- verdicts ----
enum Cls { CORRECT = 0, NEAR = 1, WRONG = 2, REFUSED = 3, NOAPI = 4 };   // NOAPI: the library has no such function (not a refusal)
struct Verdict { int cls = CORRECT; double ulps = 0; std::string msg; };
inline Verdict wrong(const std::string& m) { Verdict v; v.cls = WRONG; v.msg = m; return v; }
inline int64_t ordered(double x) { int64_t b; std::memcpy(&b, &x, 8); return b < 0 ? (int64_t)0x8000000000000000ULL - b : b; }
inline double ulpDist(double a, double b) { if (std::isnan(a) || std::isnan(b)) return 1e300; int64_t x = ordered(a), y = ordered(b); return (double)(x > y ? x - y : y - x); }
// RN(x * 2^k) of an exact rational
inline double rnQ(const Q& x, long k) { if (x == 0) return 0; F f = F(boost::multiprecision::numerator(x)) / F(boost::multiprecision::denominator(x)); f = boost::multiprecision::ldexp(f, (int)k); return f.convert_to<double>(); }
// scalar comparison: CORRECT if bit-equal to the correctly rounded exact value, NEAR within 4 ulps
inline Verdict cmpScalar(double got, const Q& exact, long k) {
    Verdict v; double rn = rnQ(exact, k);
    if (std::isnan(got)) return wrong("nan");
    if (exact == 0) { if (got == 0) return v; return wrong("expected exactly 0, got " + std::to_string(got)); }
    v.ulps = ulpDist(got, rn); if (got == rn) return v; v.cls = v.ulps <= 4 ? NEAR : WRONG; return v; }
// d == RN(sqrt(D*2^(2e))) decided by an exact bracket (midpoints to the neighbouring doubles)
inline Verdict cmpDist(double d, const Q& D, int e) {
    Verdict v; if (std::isnan(d) || d < 0) return wrong("nan/neg");
    if (D == 0) { if (d == 0) return v; return wrong("expected 0 got " + std::to_string(d)); }
    if (d == 0) return wrong("returned 0, exact D>0");
    Q Dd = D * pow2Q(2L * e); double lo = std::nextafter(d, 0.0), hi = std::nextafter(d, 1e300);
    Q l = (d2Q(d) + d2Q(lo)) / 2, h = (d2Q(d) + d2Q(hi)) / 2;
    bool ok = l * l <= Dd && Dd <= h * h;
#if MUTANT == 3
    ok = (d2Q(d) * d2Q(d) == Dd) || ok;     // mutant: compares d^2 for equality as well
    ok = ok && (d2Q(d) * d2Q(d) != Dd + 1);
    ok = (d2Q(d) * d2Q(d) <= Dd + 1 && Dd <= d2Q(d) * d2Q(d) + 1);   // accepts anything within 1 of the exact squared value
#endif
    if (ok) return v;
    double rn = rnQ(D, 0); F f = F(boost::multiprecision::numerator(D)) / F(boost::multiprecision::denominator(D)); f = boost::multiprecision::sqrt(f); f = boost::multiprecision::ldexp(f, e); rn = f.convert_to<double>();
    v.ulps = ulpDist(d, rn); v.cls = v.ulps <= 4 ? NEAR : WRONG; return v; }

// ---- measures ----
inline BigInt vol6(const IMesh& m) {      // 6V (integer, scaled by 2^(3E)), tetrahedra against vertex 0 (different from harness/exact.h, which uses the origin)
    BigInt s = 0; if (m.v.empty()) return s; const IV& o = m.v[0];
    for (auto& t : m.t) { IV a = sub(m.v[t[0]], o), b = sub(m.v[t[1]], o), c = sub(m.v[t[2]], o); s += dot(a, cross(b, c)); }
#if MUTANT == 1
    s = -s;
#endif
    return s; }
inline bool centroid(const IMesh& m, QV& c) {
    if (m.v.empty()) return false; const IV& o = m.v[0]; std::array<BigInt, 3> num = {0, 0, 0}; BigInt den = 0;
    for (auto& t : m.t) { IV a = sub(m.v[t[0]], o), b = sub(m.v[t[1]], o), cc = sub(m.v[t[2]], o); BigInt D = dot(a, cross(b, cc)); den += D;
        for (int d = 0; d < 3; d++) num[d] += D * (a[d] + b[d] + cc[d]);      // relative to o: tetra centroid (0+a+b+c)/4
    }
    if (den == 0) return false;
#if MUTANT == 2
    Q k = 3;
#else
    Q k = 4;
#endif
    for (int d = 0; d < 3; d++) c[d] = Q(num[d]) / (k * Q(den)) + Q(o[d]);
    return true; }
struct Area { bool exact = false; Q q; F f; };     // real area = value * 2^(2E)
inline Area area(const IMesh& m) {
    Area a; a.exact = true; F sum = 0; Q qs = 0;
    for (auto& t : m.t) { IV n = cross(sub(m.v[t[1]], m.v[t[0]]), sub(m.v[t[2]], m.v[t[0]])); BigInt n2 = dot(n, n); BigInt r = boost::multiprecision::sqrt(n2);
        if (r * r == n2) qs += Q(r); else a.exact = false; sum += boost::multiprecision::sqrt(F(n2)); }
    a.q = qs / 2; a.f = sum / 2; return a; }
inline Verdict cmpArea(double got, const Area& a, int E) {
    if (a.exact) return cmpScalar(got, a.q, 2L * E);
    F v = boost::multiprecision::ldexp(a.f, 2 * E); double rn = v.convert_to<double>(); Verdict r; r.ulps = ulpDist(got, rn); if (got == rn) return r; r.cls = r.ulps <= 4 ? NEAR : WRONG; return r; }
inline void bbox(const IMesh& m, IV& lo, IV& hi) { lo = m.v[0]; hi = m.v[0]; for (auto& v : m.v) for (int d = 0; d < 3; d++) { if (v[d] < lo[d]) lo[d] = v[d]; if (v[d] > hi[d]) hi[d] = v[d]; } }

// ---- distance candidates ----
struct Cand { Q d2; QV p; };
inline Q dist2(const QV& a, const QV& b) { QV d = sub(a, b); return dot(d, d); }
inline void ptSeg(const QV& p, const QV& a, const QV& b, std::vector<Cand>& out) {
    QV ab = sub(b, a); Q l2 = dot(ab, ab); Q t = 0; if (l2 != 0) { t = dot(sub(p, a), ab) / l2; if (t < 0) t = 0; if (t > 1) t = 1; }
    QV q = add(a, scaleQ(ab, t)); out.push_back({dist2(p, q), q}); }
inline void ptTri(const QV& p, const QV& a, const QV& b, const QV& c, std::vector<Cand>& out) {
    for (const QV* v : {&a, &b, &c}) out.push_back({dist2(p, *v), *v});
    ptSeg(p, a, b, out); ptSeg(p, b, c, out); ptSeg(p, c, a, out);
    QV n = cross(sub(b, a), sub(c, a)); Q n2 = dot(n, n);
    if (n2 != 0) { Q s = dot(sub(p, a), n) / n2; QV q = sub(p, scaleQ(n, s));
        if (dot(cross(sub(b, a), sub(q, a)), n) >= 0 && dot(cross(sub(c, b), sub(q, b)), n) >= 0 && dot(cross(sub(a, c), sub(q, c)), n) >= 0) out.push_back({dist2(p, q), q}); } }
inline Q minOf(const std::vector<Cand>& c) { Q m = c[0].d2; for (auto& x : c) if (x.d2 < m) m = x.d2; return m; }
inline Q segSeg(const QV& p0, const QV& p1, const QV& q0, const QV& q1) {
    std::vector<Cand> c; ptSeg(p0, q0, q1, c); ptSeg(p1, q0, q1, c); ptSeg(q0, p0, p1, c); ptSeg(q1, p0, p1, c); Q best = minOf(c);
    QV u = sub(p1, p0), v = sub(q1, q0), w = sub(p0, q0); Q a = dot(u, u), b = dot(u, v), cc = dot(v, v), d = dot(u, w), e = dot(v, w), den = a * cc - b * b;
    if (den != 0) { Q s = (b * e - cc * d) / den, t = (a * e - b * d) / den; if (s >= 0 && s <= 1 && t >= 0 && t <= 1) { Q x = dist2(add(p0, scaleQ(u, s)), add(q0, scaleQ(v, t))); if (x < best) best = x; } }
    return best; }
inline bool segCrossTri(const QV& p, const QV& q, const QV& a, const QV& b, const QV& c) {     // closed segment meets closed triangle, non-coplanar crossing
    QV n = cross(sub(b, a), sub(c, a)); Q dp = dot(n, sub(p, a)), dq = dot(n, sub(q, a));
    if ((dp > 0 && dq > 0) || (dp < 0 && dq < 0) || (dp == 0 && dq == 0)) return false;
    Q t = dp / (dp - dq); QV x = add(p, scaleQ(sub(q, p), t));
    return dot(cross(sub(b, a), sub(x, a)), n) >= 0 && dot(cross(sub(c, b), sub(x, b)), n) >= 0 && dot(cross(sub(a, c), sub(x, c)), n) >= 0; }
inline Q triTri(const QV* A, const QV* B) {
    for (int i = 0; i < 3; i++) if (segCrossTri(A[i], A[(i + 1) % 3], B[0], B[1], B[2]) || segCrossTri(B[i], B[(i + 1) % 3], A[0], A[1], A[2])) return 0;
    std::vector<Cand> c; for (int i = 0; i < 3; i++) { ptTri(A[i], B[0], B[1], B[2], c); ptTri(B[i], A[0], A[1], A[2], c); } Q best = minOf(c);
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { Q x = segSeg(A[i], A[(i + 1) % 3], B[j], B[(j + 1) % 3]); if (x < best) best = x; }
    return best; }
struct PM { Q d2; std::vector<QV> pts; };
inline PM pointMesh(const IV& pi, const IMesh& m) {      // unsigned distance to the surface, with every closest point
    QV p = toQ(pi); std::vector<Cand> all; for (auto& t : m.t) ptTri(p, toQ(m.v[t[0]]), toQ(m.v[t[1]]), toQ(m.v[t[2]]), all);
    PM r; r.d2 = minOf(all); for (auto& c : all) if (c.d2 == r.d2) { bool dup = false; for (auto& q : r.pts) if (q == c.p) dup = true; if (!dup) r.pts.push_back(c.p); }
#if MUTANT == 7
    r.pts.resize(1);
#endif
    return r; }
inline Q bboxGap2(const IV& alo, const IV& ahi, const IV& blo, const IV& bhi) { BigInt s = 0; for (int d = 0; d < 3; d++) { BigInt g = 0; if (ahi[d] < blo[d]) g = blo[d] - ahi[d]; else if (bhi[d] < alo[d]) g = alo[d] - bhi[d]; s += g * g; } return Q(s); }
inline Q surfaceDist2(const IMesh& A, const IMesh& B) {
    std::vector<QV> qa(A.v.size()), qb(B.v.size()); for (size_t i = 0; i < A.v.size(); i++) qa[i] = toQ(A.v[i]); for (size_t i = 0; i < B.v.size(); i++) qb[i] = toQ(B.v[i]);
    auto tb = [&](const IMesh& m, const std::array<int, 3>& t, IV& lo, IV& hi) { lo = hi = m.v[t[0]]; for (int k = 1; k < 3; k++) for (int d = 0; d < 3; d++) { if (m.v[t[k]][d] < lo[d]) lo[d] = m.v[t[k]][d]; if (m.v[t[k]][d] > hi[d]) hi[d] = m.v[t[k]][d]; } };
    std::vector<IV> alo(A.t.size()), ahi(A.t.size()), blo(B.t.size()), bhi(B.t.size()); for (size_t i = 0; i < A.t.size(); i++) tb(A, A.t[i], alo[i], ahi[i]); for (size_t j = 0; j < B.t.size(); j++) tb(B, B.t[j], blo[j], bhi[j]);
    bool have = false; Q best = 0;
    for (size_t i = 0; i < A.t.size(); i++) for (size_t j = 0; j < B.t.size(); j++) {
        if (have && bboxGap2(alo[i], ahi[i], blo[j], bhi[j]) >= best) continue;
        QV ta[3] = {qa[A.t[i][0]], qa[A.t[i][1]], qa[A.t[i][2]]}, tbv[3] = {qb[B.t[j][0]], qb[B.t[j][1]], qb[B.t[j][2]]};
        Q x = triTri(ta, tbv); if (!have || x < best) { best = x; have = true; if (best == 0) return 0; } }
    return best; }
inline mesh::Mesh toDouble(const IMesh& m) { mesh::Mesh o; for (auto& v : m.v) for (int d = 0; d < 3; d++) o.v.push_back(std::ldexp(v[d].convert_to<double>(), m.E)); for (auto& t : m.t) o.addT(t[0], t[1], t[2]); return o; }
inline Q solidDist2(const IMesh& A, const IMesh& B) {
    Q d = surfaceDist2(A, B); if (d == 0) return 0;
    mesh::Mesh dA = toDouble(A), dB = toDouble(B); double pa[3] = {dA.v[0], dA.v[1], dA.v[2]}, pb[3] = {dB.v[0], dB.v[1], dB.v[2]};
    mesh::Pip x = mesh::classifyPoint(dB, pa), y = mesh::classifyPoint(dA, pb); if (x.inside || x.boundary || y.inside || y.boundary) return 0; return d; }

// ---- structure ----
struct Valid { bool ok = false; std::string why; mesh::Topology t; };
inline Valid solidValid(const mesh::Mesh& m, bool rejectZeroArea) {
    Valid v; if (m.nt() == 0) { v.why = "empty"; return v; } v.t = mesh::analyzeTopology(m);
    if (!v.t.closedManifold()) v.why = "not closed 2-manifold"; else if (v.t.inverted) v.why = "inverted (negative volume)"; else if (v.t.duplicateTri) v.why = "duplicate triangle";
    else if (rejectZeroArea && v.t.zeroArea) v.why = "zero-area triangle"; else v.ok = true; return v; }

// ---- extrude ----
struct Ring2 { std::vector<std::array<BigInt, 2>> p; };
inline void profileMoments(const std::vector<c3d::Ring>& rings, BigInt& A2, BigInt& Sx, BigInt& Sy) {   // A2 = 2A; Sx = sum (xi+xj)*cross (=6*A*Cx)
    A2 = 0; Sx = 0; Sy = 0; for (auto& r : rings) { size_t n = r.p.size(); for (size_t i = 0; i < n; i++) { const auto& p = r.p[i]; const auto& q = r.p[(i + 1) % n]; BigInt cr = BigInt(p[0]) * q[1] - BigInt(q[0]) * p[1]; A2 += cr; Sx += BigInt(p[0] + q[0]) * cr; Sy += BigInt(p[1] + q[1]) * cr; } } }
inline Verdict checkExtrude(const c3d::Case& c, const mesh::Mesh& res, bool refused) {
    BigInt A2, Sx, Sy; profileMoments(c.rings, A2, Sx, Sy); int e = c.exp; Verdict v;
    if (A2 == 0) { if (refused || res.nt() == 0) { v.msg = "degenerate profile: empty"; return v; } return wrong("collinear profile produced a solid"); }
    if (refused) { v.cls = REFUSED; v.msg = "refused valid input"; return v; }
    Valid val = solidValid(res, true); if (!val.ok) return wrong("invalid: " + val.why);
    IMesh m = fromDoubles(res); Q h = Q(c.P("vec", 2)); Q dx = Q(c.P("vec", 0)), dy = Q(c.P("vec", 1)), A = Q(A2);
    Q want6V = 3 * h * A * pow2Q(3L * e); Q got6V = Q(vol6(m)) * pow2Q(3L * m.E);
    if (want6V != got6V) { Verdict w = wrong(""); w.ulps = ulpDist(rnQ(got6V, 0), rnQ(want6V, 0)); char buf[120]; snprintf(buf, sizeof buf, "exact volume differs: relative error %.3g (%.3g ulps of the volume)", ((got6V - want6V) / want6V).convert_to<double>(), w.ulps); w.msg = buf; return w; }
    QV cen; if (!centroid(m, cen)) return wrong("no centroid");
    Q cx = Q(Sx) / (3 * A) + dx / 2, cy = Q(Sy) / (3 * A) + dy / 2, cz = h / 2; Q s = pow2Q(e), sm = pow2Q(m.E);
    if (cen[0] * sm != cx * s || cen[1] * sm != cy * s || cen[2] * sm != cz * s) { Verdict w = wrong(""); Q cc2[3] = {cx * s, cy * s, cz * s}; for (int d = 0; d < 3; d++) w.ulps = std::max(w.ulps, ulpDist(rnQ(cen[d] * sm, 0), rnQ(cc2[d], 0))); char buf[100]; snprintf(buf, sizeof buf, "exact centroid differs (volume exact); worst coordinate %.3g ulps", w.ulps); w.msg = buf; return w; }
    int holes = (int)c.P("holes"); if (val.t.euler() != 2 - 2L * holes) return wrong("Euler characteristic " + std::to_string(val.t.euler()) + " expected " + std::to_string(2 - 2 * holes));
    return v; }

// ---- revolve ----
typedef std::array<Q, 2> Q2;
inline Q ringMx(std::vector<Q2> r) {       // first moment about the axis of the polygon clipped to x >= 0 : (1/6) sum (xi+xj)(xi yj - xj yi)
    std::vector<Q2> o; size_t n = r.size();
    for (size_t i = 0; i < n; i++) { const Q2 &p = r[i], &q = r[(i + 1) % n]; bool ip = p[0] >= 0, iq = q[0] >= 0; if (ip) o.push_back(p);
        if (ip != iq) { Q y = p[1] + (0 - p[0]) / (q[0] - p[0]) * (q[1] - p[1]); o.push_back({Q(0), y}); } }
    Q s = 0; for (size_t i = 0; i < o.size(); i++) { const Q2 &p = o[i], &q = o[(i + 1) % o.size()]; s += (p[0] + q[0]) * (p[0] * q[1] - q[0] * p[1]); } return s / 6; }
inline Q profileMx(const c3d::Case& c) { Q s = 0; for (auto& r : c.rings) { std::vector<Q2> v; for (auto& p : r.p) v.push_back({Q(p[0]), Q(p[1])}); s += ringMx(v); } return s; }
inline Verdict checkRevolve(const c3d::Case& c, const mesh::Mesh& res, bool refused) {
    Verdict v; Q Mx0 = profileMx(c);
    if (Mx0 == 0) { if (refused || res.nt() == 0) { v.msg = "zero-area profile: empty"; return v; } return wrong("zero-area profile produced a non-empty mesh"); }
    if (refused) { v.cls = REFUSED; v.msg = "refused"; return v; }
    Valid val = solidValid(res, true); if (!val.ok) return wrong("invalid: " + val.why);
    IMesh m = fromDoubles(res); long n = (long)c.P("seg"), deg = (long)c.P("deg"); Q Mx = profileMx(c); Q got = Q(vol6(m)) * pow2Q(3L * m.E);
    if ((n == 4 && deg == 360) || (n == 3 && deg == 270)) { Q want = 6 * n * Mx * pow2Q(3L * c.exp); if (got == want) return v; Verdict w = wrong(""); w.ulps = ulpDist(rnQ(got, 0), rnQ(want, 0)); char buf[100]; snprintf(buf, sizeof buf, "exact volume differs, %.3g ulps", w.ulps); w.msg = buf; return w; }
    F pi = 4 * boost::multiprecision::atan(F(1)); F th = 2 * pi / n; F ref = n * boost::multiprecision::sin(th) * (F(boost::multiprecision::numerator(Mx)) / F(boost::multiprecision::denominator(Mx))) * 6;
    F g = F(boost::multiprecision::numerator(got)) / F(boost::multiprecision::denominator(got)); F r = ref * boost::multiprecision::ldexp(F(1), (int)(3 * c.exp)); F rel = (g - r) / r; if (rel < 0) rel = -rel;
    v.ulps = (rel / F(std::ldexp(1.0, -52))).convert_to<double>(); v.cls = v.ulps <= 16 ? CORRECT : WRONG; v.msg = "generic n=" + std::to_string(n); return v; }

// ---- tessellation ----
struct TriOut { std::vector<std::array<std::array<double, 3>, 3>> t; };
inline Verdict checkTess(const c3d::Case& c, const TriOut& out, bool refused) {
    Verdict v; typedef std::array<c3d::i64, 3> P; std::map<P, int> id; std::vector<P> pts; std::vector<std::vector<int>> rings;
    for (auto& r : c.rings) { std::vector<int> ri; for (auto& p : r.p) { P k = {p[0], p[1], p[2]}; auto it = id.find(k); if (it == id.end()) { it = id.emplace(k, (int)pts.size()).first; pts.push_back(k); } ri.push_back(it->second); } rings.push_back(ri); }
    std::array<BigInt, 3> N = {0, 0, 0};
    for (auto& r : rings) for (size_t i = 0; i < r.size(); i++) { const P& p = pts[r[i]]; const P& q = pts[r[(i + 1) % r.size()]]; IV a = {BigInt(p[0]), BigInt(p[1]), BigInt(p[2])}, b = {BigInt(q[0]), BigInt(q[1]), BigInt(q[2])}; IV cr = cross(a, b); for (int d = 0; d < 3; d++) N[d] += cr[d]; }
    bool zero = N[0] == 0 && N[1] == 0 && N[2] == 0;
    if (refused) { if (zero) { v.msg = "zero-area polygon: refused"; return v; } v.cls = REFUSED; v.msg = "refused"; return v; }
    std::map<std::pair<int, int>, int> chain; size_t degenerate = 0, tjunc = 0; IV p0 = {BigInt(pts[0][0]), BigInt(pts[0][1]), BigInt(pts[0][2])};
    auto addSplit = [&](int u, int w, int sgn) {      // edge u->w split at every input vertex strictly inside it
        IV a = {BigInt(pts[u][0]), BigInt(pts[u][1]), BigInt(pts[u][2])}, b = {BigInt(pts[w][0]), BigInt(pts[w][1]), BigInt(pts[w][2])}; IV ab = sub(b, a); std::vector<std::pair<BigInt, int>> on;
        for (size_t k = 0; k < pts.size(); k++) { if ((int)k == u || (int)k == w) continue; IV p = {BigInt(pts[k][0]), BigInt(pts[k][1]), BigInt(pts[k][2])}; IV ap = sub(p, a); IV cr = cross(ab, ap);
            if (cr[0] == 0 && cr[1] == 0 && cr[2] == 0) { BigInt t = dot(ap, ab); if (t > 0 && t < dot(ab, ab)) on.push_back({t, (int)k}); } }
        std::sort(on.begin(), on.end(), [](const std::pair<BigInt, int>& x, const std::pair<BigInt, int>& y) { return x.first < y.first; });
        if (sgn > 0 && !on.empty()) tjunc++;
        std::vector<int> seq = {u}; for (auto& o : on) seq.push_back(o.second); seq.push_back(w);
        for (size_t i = 0; i + 1 < seq.size(); i++) { int x = seq[i], y = seq[i + 1];
#if MUTANT == 5
            chain[{std::min(x, y), std::max(x, y)}] += sgn;
#else
            if (x < y) chain[{x, y}] += sgn; else chain[{y, x}] -= sgn;
#endif
        } };
    for (auto& tr : out.t) {
        int vi[3]; for (int k = 0; k < 3; k++) { P key; for (int d = 0; d < 3; d++) { double x = std::ldexp(tr[k][d], -c.exp); if (x != std::floor(x) || std::fabs(x) > 4e18) return wrong("non-integer output coordinate (Steiner or rounding)"); key[d] = (c3d::i64)x; }
            auto it = id.find(key); if (it == id.end()) return wrong("output vertex is not an input vertex"); vi[k] = it->second; }
        IV a = {BigInt(pts[vi[0]][0]), BigInt(pts[vi[0]][1]), BigInt(pts[vi[0]][2])}, b = {BigInt(pts[vi[1]][0]), BigInt(pts[vi[1]][1]), BigInt(pts[vi[1]][2])}, cc = {BigInt(pts[vi[2]][0]), BigInt(pts[vi[2]][1]), BigInt(pts[vi[2]][2])};
        IV at = cross(sub(b, a), sub(cc, a)); BigInt s = dot(at, N);
#if MUTANT != 4
        for (const IV* q : {&a, &b, &cc}) if (!zero && dot(sub(*q, p0), N) != 0) return wrong("triangle vertex not in the polygon plane");
#endif
        if (zero) { if (at[0] != 0 || at[1] != 0 || at[2] != 0) return wrong("positive-area triangle for a zero-area polygon"); continue; }
        if (s == 0) { degenerate++; continue; } if (s < 0) return wrong("negative orientation");
        for (int k = 0; k < 3; k++) addSplit(vi[k], vi[(k + 1) % 3], +1);
    }
    if (zero) { v.msg = "zero-area polygon: no positive triangles"; return v; }
    for (auto& r : rings) for (size_t i = 0; i < r.size(); i++) addSplit(r[i], r[(i + 1) % r.size()], -1);
    for (auto& kv : chain) if (kv.second != 0) return wrong("triangulation boundary differs from the polygon (gap, overlap or T-junction)");
    if (degenerate) v.msg = std::to_string(degenerate) + " zero-area triangles"; if (tjunc) v.msg += (v.msg.empty() ? "" : "; ") + std::to_string(tjunc) + " T-junction triangle edges (pass through an input vertex)"; return v; }

// ---- convex Minkowski ----
// result == hull(pairwise sums) is proved in three exact stages (Q arithmetic on the exact doubles of the result):
//  1. every pairwise sum lies on or inside every face plane of the result (result contains the hull);
//  2. every face plane is supported: the sums lying exactly on it contain the face (its 3 vertices lie in the 2D convex hull of those sums), so the boundary of the result is inside the hull;
//  3. every result vertex lies on or inside every face plane (the result is convex).
// Stages 2+3: a convex body whose boundary lies in the (convex) hull lies in it. A vertex that is not itself a sum is fine when it lies on a hull face or edge (reported, not failed).
typedef std::array<Q, 2> Q2p;
inline bool inHull2D(std::vector<Q2p> P, const Q2p& v) {
    std::sort(P.begin(), P.end(), [](const Q2p& a, const Q2p& b) { return a[0] < b[0] || (a[0] == b[0] && a[1] < b[1]); }); P.erase(std::unique(P.begin(), P.end()), P.end());
    auto o = [](const Q2p& a, const Q2p& b, const Q2p& c) { Q x = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); return x > 0 ? 1 : (x < 0 ? -1 : 0); };
    std::vector<Q2p> H; for (int pass = 0; pass < 2; pass++) { size_t base = H.size(); for (size_t i = 0; i < P.size(); i++) { const Q2p& p = P[pass ? P.size() - 1 - i : i]; while (H.size() >= base + 2 && o(H[H.size() - 2], H.back(), p) <= 0) H.pop_back(); H.push_back(p); } H.pop_back(); }
    if (H.size() < 3) return false;
    for (size_t i = 0; i < H.size(); i++) if (o(H[i], H[(i + 1) % H.size()], v) < 0) return false;
    return true; }
inline Verdict checkMinkowski(const c3d::Case& c, const mesh::Mesh& res, bool refused) {
    Verdict v; if (refused) { v.cls = REFUSED; v.msg = "refused"; return v; }
    Valid val = solidValid(res, true); if (!val.ok) return wrong("invalid: " + val.why);
    std::vector<c3d::V3> A, B; const c3d::IMesh& ma = c.mesh.at("A"); A = ma.v; if (c.mesh.count("B")) B = c.mesh.at("B").v; else B = c.pts.at("B");
    std::set<std::array<c3d::i64, 3>> sums; std::vector<QV> S; Q sc = pow2Q(c.exp);
    for (auto& a : A) for (auto& b : B) { std::array<c3d::i64, 3> k = {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; if (sums.insert(k).second) S.push_back({Q(k[0]) * sc, Q(k[1]) * sc, Q(k[2]) * sc}); }
    std::vector<QV> R; size_t nonSum = 0; for (size_t i = 0; i < res.nv(); i++) { QV p = {d2Q(res.v[3 * i]), d2Q(res.v[3 * i + 1]), d2Q(res.v[3 * i + 2])}; R.push_back(p); bool isSum = false; for (auto& s : S) if (s == p) { isSum = true; break; } if (!isSum) nonSum++; }
    double worstOut = 0;
    for (size_t k = 0; k < res.nt(); k++) {
        const QV &a = R[res.t[3 * k]], &b = R[res.t[3 * k + 1]], &cc = R[res.t[3 * k + 2]]; QV n = cross(sub(b, a), sub(cc, a)); Q n2 = dot(n, n); std::vector<QV> onPlane;
        for (auto& s : S) { Q d = dot(n, sub(s, a));
#if MUTANT == 6
            d = -d;
#endif
            if (d > 0) { double dd = (d * d / n2).convert_to<double>(); worstOut = std::max(worstOut, std::sqrt(dd)); } else if (d == 0) onPlane.push_back(s); }
        if (worstOut > 0) { char buf[160]; snprintf(buf, sizeof buf, "a pairwise sum lies outside a face plane of the result by up to %.3g (result smaller than the hull)", worstOut); return wrong(buf); }
        for (auto& p : R) { Q d = dot(n, sub(p, a)); if (d > 0) return wrong("result is not convex (a vertex lies outside a face plane)"); }
        if (onPlane.size() < 3) return wrong("a face plane touches fewer than 3 pairwise sums (result bigger than the hull)");
        int ax = 0; { double bn = 0; for (int d = 0; d < 3; d++) { double x = std::fabs(n[d].convert_to<double>()); if (x > bn) { bn = x; ax = d; } } }
        int u = (ax + 1) % 3, w = (ax + 2) % 3; std::vector<Q2p> P2; for (auto& s : onPlane) P2.push_back({s[u], s[w]});
        for (const QV* q : {&a, &b, &cc}) { Q2p t = {(*q)[u], (*q)[w]}; if (!inHull2D(P2, t)) return wrong("a face of the result is not inside the hull of the sums on its plane (result bigger than the hull)"); } }
    if (nonSum) v.msg = std::to_string(nonSum) + " result vertices are not pairwise sums but lie on hull faces/edges";
    return v; }

inline Verdict checkMinkowskiNC(const c3d::Case& c, const mesh::Mesh& res, bool refused) {
    Verdict v; if (refused) { v.cls = REFUSED; v.msg = "refused"; return v; }
    Valid val = solidValid(res, true); if (!val.ok) return wrong("invalid: " + val.why);
    const std::vector<c3d::i64>& bx = c.param.at("boxes"); const c3d::IMesh& B = c.mesh.at("B"); c3d::i64 blo[3] = {B.v[0][0], B.v[0][1], B.v[0][2]}, bhi[3] = {blo[0], blo[1], blo[2]};
    for (auto& p : B.v) for (int d = 0; d < 3; d++) { blo[d] = std::min(blo[d], p[d]); bhi[d] = std::max(bhi[d], p[d]); }
    std::vector<std::array<c3d::i64, 6>> boxes; for (size_t i = 0; i + 5 < bx.size(); i += 6) boxes.push_back({bx[i] + blo[0], bx[i + 1] + blo[1], bx[i + 2] + blo[2], bx[i + 3] + bhi[0], bx[i + 4] + bhi[1], bx[i + 5] + bhi[2]});
    std::vector<c3d::i64> xs[3]; for (auto& b : boxes) for (int d = 0; d < 3; d++) { xs[d].push_back(b[d]); xs[d].push_back(b[3 + d]); } for (int d = 0; d < 3; d++) { std::sort(xs[d].begin(), xs[d].end()); xs[d].erase(std::unique(xs[d].begin(), xs[d].end()), xs[d].end()); }
    BigInt vol = 0; for (size_t i = 0; i + 1 < xs[0].size(); i++) for (size_t j = 0; j + 1 < xs[1].size(); j++) for (size_t k = 0; k + 1 < xs[2].size(); k++) {
        bool in = false; for (auto& b : boxes) if (b[0] <= xs[0][i] && xs[0][i + 1] <= b[3] && b[1] <= xs[1][j] && xs[1][j + 1] <= b[4] && b[2] <= xs[2][k] && xs[2][k + 1] <= b[5]) { in = true; break; }
        if (in) vol += BigInt(xs[0][i + 1] - xs[0][i]) * BigInt(xs[1][j + 1] - xs[1][j]) * BigInt(xs[2][k + 1] - xs[2][k]); }
    IMesh m = fromDoubles(res); Q want = Q(6 * vol) * pow2Q(3L * c.exp), got = Q(vol6(m)) * pow2Q(3L * m.E);
    if (want == got) return v; double rel = ((got - want) / want).convert_to<double>(); char buf[120]; snprintf(buf, sizeof buf, "exact volume differs, relative error %.3g", rel); return wrong(buf); }
}  // namespace o3
