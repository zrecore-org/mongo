// MP08b prototypes (research code, not product): Hertel-Mehlhorn on earcut v3.2.4 triangles; point visibility polygon (exact angular sweep, int128 fractions);
// 2D alpha shape on a CDT 2.0.2 Delaunay triangulation (CDT is MPL-2.0: scoring comparison only); polygon repair via Clipper2 2.0.1 Union (even-odd / non-zero).
// -DMUT=n plants a mutant (1-3 hm, 4-6 vis, 7-9 alpha, 10-12 repair) so the oracle can be shown to catch it. usage: proto <case.h2d> <result.res>
#include "hard2d.h"
#include <mapbox/earcut.hpp>
#include <CDT.h>
#include "clipper2/clipper.h"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#ifndef MUT
#define MUT 0
#endif
using namespace h2d;
static int sg(i128 v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static i128 cr(P a, P b) { return (i128)a.x * b.y - (i128)a.y * b.x; }
static P sub(P a, P b) { return {a.x - b.x, a.y - b.y}; }
static int orient(P a, P b, P c) { return sg(cr(sub(b, a), sub(c, a))); }
static int quad(P d) { return d.x > 0 && d.y >= 0 ? 0 : d.x <= 0 && d.y > 0 ? 1 : d.x < 0 && d.y <= 0 ? 2 : 3; }
static bool convexRing(const std::vector<P>& r, int skip = -1) { size_t n = r.size(); if (n < 3) return false; int tot = 0;
    for (size_t i = 0; i < n; i++) { P d1 = sub(r[(i + 1) % n], r[i]), d2 = sub(r[(i + 2) % n], r[(i + 1) % n]); i128 c = cr(d1, d2), dt = (i128)d1.x * d2.x + (i128)d1.y * d2.y; if ((int)((i + 1) % n) == skip) continue; if (c < 0 || (c == 0 && dt < 0)) return false; tot += ((quad(d2) - quad(d1)) % 4 + 4) % 4; }
    return skip >= 0 ? true : tot == 4; }
static void out(FILE* f, const std::vector<Ring>& rings) { for (auto& r : rings) { fprintf(f, "ring %zu", r.size()); for (auto& p : r) fprintf(f, " %lld %lld", p.x, p.y); fprintf(f, "\n"); } }
// ------------------------------------------------------------------ Hertel-Mehlhorn
static void doHM(const Case& c, FILE* f) {
    std::vector<std::vector<std::array<double, 2>>> pg; std::vector<P> flat; for (auto& r : c.rings) { std::vector<std::array<double, 2>> q; for (auto& p : r) { q.push_back({(double)p.x, (double)p.y}); flat.push_back(p); } pg.push_back(q); }
    std::vector<uint32_t> idx = mapbox::earcut<uint32_t>(pg); std::vector<std::vector<int>> parts;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) { int a = (int)idx[i], b = (int)idx[i + 1], d = (int)idx[i + 2]; int o = orient(flat[a], flat[b], flat[d]); if (o == 0) continue; if (o < 0) std::swap(b, d); parts.push_back({a, b, d}); }
    if (MUT == 3 && !parts.empty()) parts.pop_back();   // mutant: lose a triangle (coverage gap)
    for (bool changed = true; changed;) { changed = false; std::map<std::pair<int, int>, size_t> owner; for (size_t k = 0; k < parts.size(); k++) for (size_t i = 0; i < parts[k].size(); i++) owner[{parts[k][i], parts[k][(i + 1) % parts[k].size()]}] = k;
        for (auto& e : owner) { auto o = owner.find({e.first.second, e.first.first}); if (o == owner.end() || e.first.first > e.first.second || e.second == o->second) continue; size_t A = e.second, B = o->second; const auto& PA = parts[A]; const auto& PB = parts[B];
            if (MUT == 2 && (PA.size() >= 4 || PB.size() >= 4)) continue;   // mutant: never grows parts beyond quads (not HM-minimal)
            size_t ia = 0; while (!(PA[ia] == e.first.first && PA[(ia + 1) % PA.size()] == e.first.second)) ia++;
            std::vector<int> m; for (size_t t = 1; t <= PA.size(); t++) m.push_back(PA[(ia + t) % PA.size()]);   // b ... a
            size_t ib = 0; while (!(PB[ib] == e.first.second && PB[(ib + 1) % PB.size()] == e.first.first)) ib++;
            for (size_t t = 2; t < PB.size(); t++) m.push_back(PB[(ib + t) % PB.size()]);
            std::vector<P> mp; for (int v : m) mp.push_back(flat[v]); bool ok;
            if (MUT == 1) ok = convexRing(mp, 0);   // mutant: ignore convexity at vertex b (m[0]) -> reflex corners survive
            else ok = convexRing(mp);
            if (!ok) continue; parts[A] = m; parts.erase(parts.begin() + (long)B); changed = true; break; } }
    fprintf(f, "RES1 OK\n"); std::vector<Ring> rs; for (auto& p : parts) { Ring r; for (int v : p) r.push_back(flat[v]); rs.push_back(r); } out(f, rs); }
// ------------------------------------------------------------------ visibility
struct Fr { i128 n, d; };
static i128 gcd(i128 a, i128 b) { if (a < 0) a = -a; if (b < 0) b = -b; while (b) { i128 t = a % b; a = b; b = t; } return a; }
static bool lessF(const Fr& a, const Fr& b) { return a.n * b.d < b.n * a.d; }   // d > 0
static void doVis(const Case& c, FILE* f) {
    P p = c.pt; struct E { P a, b; }; std::vector<E> es; for (size_t k = 0; k < c.rings.size(); k++) { if (MUT == 4 && k > 0) break; const Ring& r = c.rings[k]; for (size_t i = 0; i < r.size(); i++) es.push_back({r[i], r[(i + 1) % r.size()]}); }
    std::vector<P> ev; for (auto& e : es) { ev.push_back(sub(e.a, p)); ev.push_back(sub(e.b, p)); } ev.push_back({1, 0}); ev.push_back({0, 1}); ev.push_back({-1, 0}); ev.push_back({0, -1});
    auto half = [](P v) { return (v.y > 0 || (v.y == 0 && v.x > 0)) ? 0 : 1; };
    std::sort(ev.begin(), ev.end(), [&](P a, P b) { int ha = half(a), hb = half(b); if (ha != hb) return ha < hb; return cr(a, b) > 0; });
    std::vector<P> d; for (auto& v : ev) if (d.empty() || !(cr(d.back(), v) == 0 && half(d.back()) == half(v))) d.push_back(v);
    size_t n = d.size(); std::vector<int> nearest(n, -1);
    for (size_t i = 0; i < n; i++) { P m{d[i].x + d[(i + 1) % n].x, d[i].y + d[(i + 1) % n].y}; Fr best{0, 1}; int bi = -1;
        for (size_t k = 0; k < es.size(); k++) { P ab = sub(es[k].b, es[k].a), ap = sub(es[k].a, p); i128 den = cr(m, ab); if (den == 0) continue; i128 tn = cr(ap, ab), sn = cr(ap, m); if (den < 0) { den = -den; tn = -tn; sn = -sn; }
            if (tn <= 0 || sn < 0 || sn > den) continue; Fr t{tn, den}; if (bi < 0 || (MUT == 6 ? lessF(best, t) : lessF(t, best))) { best = t; bi = (int)k; } }
        nearest[i] = bi; }
    std::vector<std::array<i128, 4>> V; auto hit = [&](int k, P dir) { P ab = sub(es[k].b, es[k].a), ap = sub(es[k].a, p); i128 den = cr(dir, ab), tn = cr(ap, ab); if (den < 0) { den = -den; tn = -tn; }
        i128 xn = (i128)p.x * den + (i128)dir.x * tn, yn = (i128)p.y * den + (i128)dir.y * tn; i128 g = gcd(gcd(xn, yn), den); if (g == 0) g = 1; V.push_back({xn / g, den / g, yn / g, den / g}); };
    for (size_t i = 0; i < n; i++) { int prev = nearest[(i + n - 1) % n], next = nearest[i]; if (prev < 0 || next < 0) { fprintf(f, "RES1 REFUSED open\n"); return; } if (MUT != 5) hit(prev, d[i]); hit(next, d[i]); }
    std::vector<std::array<i128, 4>> W; for (auto& v : V) if (W.empty() || W.back() != v) W.push_back(v); if (W.size() > 1 && W.front() == W.back()) W.pop_back();
    fprintf(f, "RES1 OK\nrrat %zu", W.size()); for (auto& v : W) fprintf(f, " %lld %lld %lld %lld", (long long)v[0], (long long)v[1], (long long)v[2], (long long)v[3]); fprintf(f, "\n"); }
// ------------------------------------------------------------------ alpha shape on CDT
static void doAlpha(const Case& c, FILE* f) {
    std::vector<CDT::V2d<double>> pts; for (auto& q : c.points) pts.push_back(CDT::V2d<double>((double)q.x, (double)q.y));
    CDT::Triangulation<double> t; t.insertVertices(pts); t.eraseSuperTriangle(); const auto& P0 = c.points;
    fprintf(f, "RES1 OK\n"); std::vector<std::array<int, 3>> sel; std::map<std::pair<int, int>, int> ec;
    for (auto& tr : t.triangles) { int a = (int)tr.vertices[0], b = (int)tr.vertices[1], d = (int)tr.vertices[2]; int o = orient(P0[(size_t)a], P0[(size_t)b], P0[(size_t)d]); if (o == 0) continue; if (o < 0) std::swap(b, d);
        fprintf(f, "ftri %d %d %d\n", a, b, d); P A = P0[(size_t)a], B = P0[(size_t)b], D = P0[(size_t)d];
        i128 la = (i128)(A.x - B.x) * (A.x - B.x) + (i128)(A.y - B.y) * (A.y - B.y), lb = (i128)(B.x - D.x) * (B.x - D.x) + (i128)(B.y - D.y) * (B.y - D.y), lc = (i128)(D.x - A.x) * (D.x - A.x) + (i128)(D.y - A.y) * (D.y - A.y);
        i128 cx = cr(sub(B, A), sub(D, A)); i128 lhs = la * lb * lc * c.ad, rhs = (i128)c.an * (MUT == 8 ? 16 : 4) * cx * cx;   // R^2 = la lb lc / (4 cross^2)
        bool in = MUT == 7 ? lhs < rhs : lhs <= rhs; if (!in) continue; sel.push_back({a, b, d}); fprintf(f, "btri %d %d %d\n", a, b, d);
        for (int i = 0; i < 3; i++) { int u = sel.back()[(size_t)i], v = sel.back()[(size_t)(i + 1) % 3]; ec[{std::min(u, v), std::max(u, v)}]++; } }
    for (auto& e : ec) if (e.second == 1 || MUT == 9) fprintf(f, "bedge %d %d\n", e.first.first, e.first.second); }
// ------------------------------------------------------------------ polygon repair
static void doRepair(const Case& c, FILE* f) {
    using namespace Clipper2Lib; Paths64 sub_; for (auto& r : c.rings) { Path64 q; for (auto& p : r) q.push_back(Point64(p.x, p.y)); sub_.push_back(q); }
    int rule = c.rule; if (MUT == 10) rule ^= 1; Paths64 res = Union(sub_, rule == 0 ? FillRule::EvenOdd : FillRule::NonZero);
    fprintf(f, "RES1 OK\n"); std::vector<Ring> rs; bool dropped = false; for (auto& q : res) { Ring r; for (auto& p : q) r.push_back({p.x, p.y}); if (MUT == 11 && Area(q) < 0) continue; if (MUT == 12 && !dropped && r.size() >= 4) { r.pop_back(); dropped = true; } rs.push_back(r); } out(f, rs); }
int main(int argc, char** argv) { if (argc < 3) return 2; Case c; if (!readCase(argv[1], c)) return 3; FILE* f = fopen(argv[2], "w"); if (!f) return 4;
    try { if (c.mode == "hm") doHM(c, f); else if (c.mode == "vis") doVis(c, f); else if (c.mode == "alpha") doAlpha(c, f); else if (c.mode == "repair") doRepair(c, f); else fprintf(f, "RES1 REFUSED no-prototype\n"); }
    catch (const std::exception& e) { fprintf(f, "RES1 REFUSED exception\n"); } fclose(f); return 0; }
