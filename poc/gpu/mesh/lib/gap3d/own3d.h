// MP07 own-code routes (double precision). No code shared with oracle3d.h.
#pragma once
#include "c3d.h"
#include "../../harness/mesh_io.h"
#include <mapbox/earcut.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace own {
using c3d::i64;
typedef std::array<double, 3> D3;
inline D3 sub(const D3& a, const D3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline D3 add(const D3& a, const D3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline D3 mul(const D3& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
inline double dot(const D3& a, const D3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline D3 cross(const D3& a, const D3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
struct Neumaier { double s = 0, c = 0; void add(double x) { double t = s + x; if (std::fabs(s) >= std::fabs(x)) c += (s - t) + x; else c += (x - t) + s; s = t; } double get() const { return s + c; } };

// ---- extrude: earcut caps + walls; arbitrary vector (dx,dy,h) ----
inline bool extrude(const c3d::Case& c, mesh::Mesh& out, const std::vector<uint32_t>* caps = nullptr) {
    double sc = std::ldexp(1.0, c.exp); std::vector<std::vector<std::array<double, 2>>> pg; std::vector<std::array<double, 2>> flat; double A2 = 0;
    for (auto& r : c.rings) { std::vector<std::array<double, 2>> q; for (auto& p : r.p) q.push_back({(double)p[0] * sc, (double)p[1] * sc}); for (size_t i = 0; i < q.size(); i++) { auto& a = q[i]; auto& b = q[(i + 1) % q.size()]; A2 += a[0] * b[1] - b[0] * a[1]; } pg.push_back(q); for (auto& a : q) flat.push_back(a); }
    if (A2 == 0 || c.rings.empty()) return false;
    double dx = (double)c.P("vec", 0) * sc, dy = (double)c.P("vec", 1) * sc, h = (double)c.P("vec", 2) * sc; size_t n = flat.size();
    for (size_t i = 0; i < n; i++) out.addV(flat[i][0], flat[i][1], 0); for (size_t i = 0; i < n; i++) out.addV(flat[i][0] + dx, flat[i][1] + dy, h);
    std::vector<uint32_t> idx = caps ? *caps : mapbox::earcut<uint32_t>(pg);
    for (size_t i = 0; i + 2 < idx.size(); i += 3) { uint32_t a = idx[i], b = idx[i + 1], cc = idx[i + 2]; double o = (flat[b][0] - flat[a][0]) * (flat[cc][1] - flat[a][1]) - (flat[b][1] - flat[a][1]) * (flat[cc][0] - flat[a][0]);
        if (o > 0) { out.addT(a, cc, b); out.addT(n + a, n + b, n + cc); } else { out.addT(a, b, cc); out.addT(n + a, n + cc, n + b); } }
    size_t base = 0; for (auto& r : pg) { size_t m = r.size(); for (size_t i = 0; i < m; i++) { uint32_t u = (uint32_t)(base + i), w = (uint32_t)(base + (i + 1) % m); out.addT(u, w, n + w); out.addT(u, n + w, n + u); } base += m; }
    return true; }

// ---- tessellation glue: exact integer Newell normal, drop the axis of the largest component, project, triangulate, map back ----
struct Proj { int ax = 0; bool flip = false; bool ok = false; };
inline Proj makeProj(const c3d::Case& c) {
    __int128 N[3] = {0, 0, 0};
    for (auto& r : c.rings) for (size_t i = 0; i < r.p.size(); i++) { const auto& a = r.p[i]; const auto& b = r.p[(i + 1) % r.p.size()];
        N[0] += (__int128)a[1] * b[2] - (__int128)a[2] * b[1]; N[1] += (__int128)a[2] * b[0] - (__int128)a[0] * b[2]; N[2] += (__int128)a[0] * b[1] - (__int128)a[1] * b[0]; }
    Proj p; int best = 0; for (int k = 1; k < 3; k++) { __int128 x = N[k] < 0 ? -N[k] : N[k], y = N[best] < 0 ? -N[best] : N[best]; if (x > y) best = k; }
    if (N[best] == 0) return p; p.ax = best; p.flip = N[best] < 0; p.ok = true; return p; }
inline std::array<double, 2> proj2(const Proj& p, const std::array<i64, 3>& q) { int u = (p.ax + 1) % 3, w = (p.ax + 2) % 3; return p.flip ? std::array<double, 2>{(double)q[w], (double)q[u]} : std::array<double, 2>{(double)q[u], (double)q[w]}; }
inline bool tessEarcut(const c3d::Case& c, std::vector<std::array<std::array<double, 3>, 3>>& out) {
    Proj p = makeProj(c); if (!p.ok) return false; double sc = std::ldexp(1.0, c.exp);
    std::vector<std::vector<std::array<double, 2>>> pg; std::vector<std::array<i64, 3>> flat;
    for (auto& r : c.rings) { std::vector<std::array<double, 2>> q; for (auto& pt : r.p) { q.push_back(proj2(p, pt)); flat.push_back(pt); } pg.push_back(q); }
    std::vector<uint32_t> idx = mapbox::earcut<uint32_t>(pg);
    for (size_t i = 0; i + 2 < idx.size(); i += 3) { std::array<std::array<double, 3>, 3> t; for (int k = 0; k < 3; k++) for (int d = 0; d < 3; d++) t[k][d] = (double)flat[idx[i + k]][d] * sc; out.push_back(t); }
    return true; }

// ---- closest point on a triangle (Ericson, Real-Time Collision Detection 5.1.5) ----
inline D3 closestPtTri(const D3& p, const D3& a, const D3& b, const D3& c) {
    D3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a); double d1 = dot(ab, ap), d2 = dot(ac, ap); if (d1 <= 0 && d2 <= 0) return a;
    D3 bp = sub(p, b); double d3 = dot(ab, bp), d4 = dot(ac, bp); if (d3 >= 0 && d4 <= d3) return b;
    double vc = d1 * d4 - d3 * d2; if (vc <= 0 && d1 >= 0 && d3 <= 0) { double v = d1 / (d1 - d3); return add(a, mul(ab, v)); }
    D3 cp = sub(p, c); double d5 = dot(ab, cp), d6 = dot(ac, cp); if (d6 >= 0 && d5 <= d6) return c;
    double vb = d5 * d2 - d1 * d6; if (vb <= 0 && d2 >= 0 && d6 <= 0) { double w = d2 / (d2 - d6); return add(a, mul(ac, w)); }
    double va = d3 * d6 - d5 * d4; if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { double w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); return add(b, mul(sub(c, b), w)); }
    double denom = 1.0 / (va + vb + vc); double v = vb * denom, w = vc * denom; return add(a, add(mul(ab, v), mul(ac, w))); }
inline double d2(const D3& a, const D3& b) { D3 d = sub(a, b); return dot(d, d); }
// closest points between segments (Ericson 5.1.9); returns squared distance
inline double segSegD(const D3& p1, const D3& q1, const D3& p2, const D3& q2) {
    D3 d1 = sub(q1, p1), d2v = sub(q2, p2), r = sub(p1, p2); double a = dot(d1, d1), e = dot(d2v, d2v), f = dot(d2v, r), s, t;
    if (a <= 1e-300 && e <= 1e-300) return d2(p1, p2);
    if (a <= 1e-300) { s = 0; t = std::min(1.0, std::max(0.0, f / e)); }
    else { double c = dot(d1, r); if (e <= 1e-300) { t = 0; s = std::min(1.0, std::max(0.0, -c / a)); }
        else { double b = dot(d1, d2v), den = a * e - b * b; s = den != 0 ? std::min(1.0, std::max(0.0, (b * f - c * e) / den)) : 0; t = (b * s + f) / e;
            if (t < 0) { t = 0; s = std::min(1.0, std::max(0.0, -c / a)); } else if (t > 1) { t = 1; s = std::min(1.0, std::max(0.0, (b - c) / a)); } } }
    return d2(add(p1, mul(d1, s)), add(p2, mul(d2v, t))); }
inline bool segHitsTri(const D3& p, const D3& q, const D3& a, const D3& b, const D3& c) {   // double-precision signed-volume test (not exact)
    D3 n = cross(sub(b, a), sub(c, a)); double dp = dot(n, sub(p, a)), dq = dot(n, sub(q, a)); if ((dp > 0 && dq > 0) || (dp < 0 && dq < 0) || (dp == 0 && dq == 0)) return false;
    double t = dp / (dp - dq); D3 x = add(p, mul(sub(q, p), t));
    return dot(cross(sub(b, a), sub(x, a)), n) >= 0 && dot(cross(sub(c, b), sub(x, b)), n) >= 0 && dot(cross(sub(a, c), sub(x, c)), n) >= 0; }
inline double triTriD(const D3* A0, const D3* B0) {
    D3 A[3], B[3]; for (int i = 0; i < 3; i++) { A[i] = sub(A0[i], A0[0]); B[i] = sub(B0[i], A0[0]); }     // relative to a vertex of A: exact for integer data, no absolute-coordinate rounding
    for (int i = 0; i < 3; i++) if (segHitsTri(A[i], A[(i + 1) % 3], B[0], B[1], B[2]) || segHitsTri(B[i], B[(i + 1) % 3], A[0], A[1], A[2])) return 0;
    double best = std::numeric_limits<double>::infinity(); for (int i = 0; i < 3; i++) { best = std::min(best, d2(A[i], closestPtTri(A[i], B[0], B[1], B[2]))); best = std::min(best, d2(B[i], closestPtTri(B[i], A[0], A[1], A[2]))); }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) best = std::min(best, segSegD(A[i], A[(i + 1) % 3], B[j], B[(j + 1) % 3])); return best; }

// ---- BVH over triangles (median split on the longest axis, leaf <= 4), best-first pruning by squared box distance ----
struct Bvh {
    struct Node { D3 lo, hi; int l = -1, r = -1, b = 0, e = 0; };
    std::vector<Node> nodes; std::vector<int> order; const std::vector<D3>* V = nullptr; const std::vector<std::array<uint32_t, 3>>* T = nullptr;
    void build(const std::vector<D3>& v, const std::vector<std::array<uint32_t, 3>>& t) { V = &v; T = &t; order.resize(t.size()); for (size_t i = 0; i < t.size(); i++) order[i] = (int)i; nodes.reserve(2 * t.size() / 2 + 4); rec(0, (int)t.size()); }
    D3 cen(int i) const { const auto& t = (*T)[i]; D3 c = add(add((*V)[t[0]], (*V)[t[1]]), (*V)[t[2]]); return mul(c, 1.0 / 3); }
    int rec(int b, int e) {
        int id = (int)nodes.size(); nodes.push_back({}); Node n; n.lo = {1e300, 1e300, 1e300}; n.hi = {-1e300, -1e300, -1e300}; n.b = b; n.e = e;
        for (int i = b; i < e; i++) for (int k = 0; k < 3; k++) { const D3& p = (*V)[(*T)[order[i]][k]]; for (int d = 0; d < 3; d++) { n.lo[d] = std::min(n.lo[d], p[d]); n.hi[d] = std::max(n.hi[d], p[d]); } }
        if (e - b > 4) { int ax = 0; for (int d = 1; d < 3; d++) if (n.hi[d] - n.lo[d] > n.hi[ax] - n.lo[ax]) ax = d; int m = (b + e) / 2;
            std::nth_element(order.begin() + b, order.begin() + m, order.begin() + e, [&](int x, int y) { return cen(x)[ax] < cen(y)[ax]; }); int l = rec(b, m); int r = rec(m, e); n.l = l; n.r = r; }
        nodes[id] = n; return id; }
    static double boxD2(const D3& lo, const D3& hi, const D3& qlo, const D3& qhi) { double s = 0; for (int d = 0; d < 3; d++) { double g = 0; if (hi[d] < qlo[d]) g = qlo[d] - hi[d]; else if (qhi[d] < lo[d]) g = lo[d] - qhi[d]; s += g * g; } return s; }
    // generic best-first: f(triIndex) returns the squared distance of that triangle to the query box/primitive
    template <class Fn> void query(const D3& qlo, const D3& qhi, double& best, Fn&& f, int node = 0) const {
        const Node& n = nodes[node]; if (boxD2(n.lo, n.hi, qlo, qhi) >= best) return;
        if (n.l < 0) { for (int i = n.b; i < n.e; i++) best = std::min(best, f(order[i])); return; }
        double dl = boxD2(nodes[n.l].lo, nodes[n.l].hi, qlo, qhi), dr = boxD2(nodes[n.r].lo, nodes[n.r].hi, qlo, qhi);
        if (dl <= dr) { query(qlo, qhi, best, f, n.l); query(qlo, qhi, best, f, n.r); } else { query(qlo, qhi, best, f, n.r); query(qlo, qhi, best, f, n.l); } } };
struct DMesh { std::vector<D3> v; std::vector<std::array<uint32_t, 3>> t; Bvh bvh; };
inline DMesh fromMesh(const mesh::Mesh& m) { DMesh d; for (size_t i = 0; i < m.nv(); i++) d.v.push_back({m.v[3 * i], m.v[3 * i + 1], m.v[3 * i + 2]}); for (size_t k = 0; k < m.nt(); k++) d.t.push_back({m.t[3 * k], m.t[3 * k + 1], m.t[3 * k + 2]}); return d; }
inline DMesh fromCase(const c3d::IMesh& m, int exp) { double sc = std::ldexp(1.0, exp); DMesh d; for (auto& v : m.v) d.v.push_back({(double)v[0] * sc, (double)v[1] * sc, (double)v[2] * sc}); for (auto& t : m.t) d.t.push_back({(uint32_t)t[0], (uint32_t)t[1], (uint32_t)t[2]}); return d; }
inline double pointMeshD(const DMesh& m, const D3& p, D3* cp = nullptr) {
    double best = std::numeric_limits<double>::infinity(); D3 bp = p;
    D3 zero = {0, 0, 0};
    m.bvh.query(p, p, best, [&](int ti) { const auto& t = m.t[ti]; D3 q = closestPtTri(zero, sub(m.v[t[0]], p), sub(m.v[t[1]], p), sub(m.v[t[2]], p)); double d = dot(q, q); if (d < best) bp = add(p, q); return d; });   // coordinates relative to p: no absolute-coordinate rounding
    if (cp) *cp = bp; return std::sqrt(best); }
inline bool insideParity(const DMesh& m, const D3& p) {       // +x ray crossing parity (double, not exact): only used for the containment case
    int cnt = 0; D3 dir = {1.0, 0.31415926535, 0.27182818284}; for (auto& t : m.t) { const D3 &a = m.v[t[0]], &b = m.v[t[1]], &c = m.v[t[2]]; D3 q = add(p, mul(dir, 1e9)); if (segHitsTri(p, q, a, b, c)) cnt++; } return cnt & 1; }   // jittered ray: avoids the lattice edge/vertex hits of an axis ray
inline double meshMeshD(const DMesh& A, const DMesh& B, bool containment) {
    double best = std::numeric_limits<double>::infinity();
    for (auto& t : A.t) { D3 ta[3] = {A.v[t[0]], A.v[t[1]], A.v[t[2]]}; D3 lo = ta[0], hi = ta[0]; for (int k = 1; k < 3; k++) for (int d = 0; d < 3; d++) { lo[d] = std::min(lo[d], ta[k][d]); hi[d] = std::max(hi[d], ta[k][d]); }
        B.bvh.query(lo, hi, best, [&](int bi) { const auto& u = B.t[bi]; D3 tb[3] = {B.v[u[0]], B.v[u[1]], B.v[u[2]]}; return triTriD(ta, tb); }); if (best == 0) return 0; }
    if (containment && (insideParity(B, A.v[0]) || insideParity(A, B.v[0]))) return 0;
    return std::sqrt(best); }
// ---- measures (apex at vertex 0, Neumaier sums) ----
inline double volumeD(const DMesh& m) { Neumaier s; const D3& o = m.v[0]; for (auto& t : m.t) s.add(dot(sub(m.v[t[0]], o), cross(sub(m.v[t[1]], o), sub(m.v[t[2]], o)))); return s.get() / 6; }
inline D3 centroidD(const DMesh& m) { Neumaier num[3], den; const D3& o = m.v[0]; for (auto& t : m.t) { D3 a = sub(m.v[t[0]], o), b = sub(m.v[t[1]], o), c = sub(m.v[t[2]], o); double D = dot(a, cross(b, c)); den.add(D); for (int d = 0; d < 3; d++) num[d].add(D * (a[d] + b[d] + c[d])); }
    double dn = den.get(); return {o[0] + num[0].get() / (4 * dn), o[1] + num[1].get() / (4 * dn), o[2] + num[2].get() / (4 * dn)}; }
inline double areaD(const DMesh& m) { Neumaier s; for (auto& t : m.t) { D3 n = cross(sub(m.v[t[1]], m.v[t[0]]), sub(m.v[t[2]], m.v[t[0]])); s.add(0.5 * std::sqrt(dot(n, n))); } return s.get(); }
}  // namespace own
