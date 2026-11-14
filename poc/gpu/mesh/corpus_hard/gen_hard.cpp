// MP08b corpus generator: 4 modes x 60 cases (+ 20 'mono' reuse the hm polygons), integer coordinates only, no floating point.
// RNG: splitmix64; EVERY rr() call is its own statement (argument evaluation order differs between GCC and clang). Build with -ffp-contract=off.
// Every polygon is verified simple by an exact int128 check inside the generator (rejection sampling is deterministic). usage: gen_hard <out-dir>
#include "../lib/hard2d3d/hard2d.h"
#include <algorithm>
#include <sys/stat.h>
using namespace h2d;
struct Rng { uint64_t s; uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ULL); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); }
    i64 rr(i64 lo, i64 hi) { uint64_t r = next(); return lo + (i64)(r % (uint64_t)(hi - lo + 1)); } };
static int sgn(i128 v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static int orient(P a, P b, P c) { return sgn((i128)(b.x - a.x) * (c.y - a.y) - (i128)(b.y - a.y) * (c.x - a.x)); }
static bool onSeg(P a, P b, P c) { return orient(a, b, c) == 0 && std::min(a.x, b.x) <= c.x && c.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= c.y && c.y <= std::max(a.y, b.y); }
static bool segInter(P a, P b, P c, P d) { int o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (o1 * o2 < 0 && o3 * o4 < 0) return true; return onSeg(a, b, c) || onSeg(a, b, d) || onSeg(c, d, a) || onSeg(c, d, b); }
static i128 area2(const Ring& r) { i128 a = 0; for (size_t i = 0; i < r.size(); i++) { P p = r[i], q = r[(i + 1) % r.size()]; a += (i128)p.x * q.y - (i128)q.x * p.y; } return a; }
// ring simple: nonadjacent edges disjoint, adjacent edges share only the common endpoint, no repeated vertex
static bool simpleRing(const Ring& r) { size_t n = r.size(); if (n < 3) return false;
    for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) if (r[i].x == r[j].x && r[i].y == r[j].y) return false;
    for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) { P a = r[i], b = r[(i + 1) % n], c = r[j], d = r[(j + 1) % n];
        if (j == i + 1) { if (onSeg(a, b, d) && !(d.x == b.x && d.y == b.y)) return false; if (onSeg(c, d, a)) return false; }
        else if (i == 0 && j == n - 1) { if (onSeg(a, b, c) && !(c.x == a.x && c.y == a.y)) return false; if (onSeg(c, d, b)) return false; }
        else if (segInter(a, b, c, d)) return false; }
    return area2(r) != 0; }
// strict point in ring: 1 inside, 0 outside, -1 on boundary
static int pip(const Ring& r, P q) { int c = 0; size_t n = r.size(); for (size_t i = 0; i < n; i++) { P a = r[i], b = r[(i + 1) % n]; if (onSeg(a, b, q)) return -1;
        if ((a.y > q.y) != (b.y > q.y)) { i128 lhs = (i128)(q.x - a.x) * (b.y - a.y), rhs = (i128)(b.x - a.x) * (q.y - a.y); bool left = (b.y > a.y) ? lhs < rhs : lhs > rhs; if (left) c ^= 1; } } return c; }
static bool ringsDisjoint(const Ring& a, const Ring& b) { for (size_t i = 0; i < a.size(); i++) for (size_t j = 0; j < b.size(); j++) if (segInter(a[i], a[(i + 1) % a.size()], b[j], b[(j + 1) % b.size()])) return false; return true; }
static void orientRing(Ring& r, bool ccw) { if ((area2(r) > 0) != ccw) std::reverse(r.begin(), r.end()); }
static int half(P v) { return (v.y > 0 || (v.y == 0 && v.x > 0)) ? 0 : 1; }
static bool angLess(P a, P b) { int ha = half(a), hb = half(b); if (ha != hb) return ha < hb; return (i128)a.x * b.y - (i128)a.y * b.x > 0; }
static Ring starRing(Rng& g, int n, i64 R, i64 lo) { // vertices at inf-norm in [lo,R], sorted by exact angle
    for (;;) { std::vector<P> v; for (int i = 0; i < n; i++) { i64 x = g.rr(-R, R); i64 y = g.rr(-R, R); if (std::max(std::llabs(x), std::llabs(y)) < lo) { i--; continue; } v.push_back({x, y}); }
        std::sort(v.begin(), v.end(), angLess); Ring r(v.begin(), v.end()); if (simpleRing(r)) { orientRing(r, true); return r; } } }
static Ring histRing(Rng& g, int k, int maxw, int maxh) { std::vector<i64> x{0}, h; for (int i = 0; i < k; i++) { i64 w = g.rr(1, maxw); x.push_back(x.back() + w); i64 hh = g.rr(1, maxh); h.push_back(hh); }
    Ring r; r.push_back({0, 0}); r.push_back({x[k], 0}); for (int i = k - 1; i >= 0; i--) { P tr{x[i + 1], h[i]}, tl{x[i], h[i]}; if (r.back().x != tr.x || r.back().y != tr.y) r.push_back(tr); r.push_back(tl); }
    return r; }
static void write(const std::string& dir, int idx, const std::string& fam, const std::string& mode, const Case& c, bool hasRule = false, bool hasAlpha = false, bool hasPt = false) {
    char nm[160]; snprintf(nm, sizeof nm, "%03d_%s_%s", idx, mode.c_str(), fam.c_str()); FILE* f = fopen((dir + "/" + nm + ".h2d").c_str(), "w"); if (!f) { perror("open"); exit(4); }
    fprintf(f, "H2D1\nname %s\nfamily %s\nmode %s\n", nm, fam.c_str(), mode.c_str()); if (hasRule) fprintf(f, "rule %d\n", c.rule); if (hasAlpha) fprintf(f, "alpha %lld %lld\n", c.an, c.ad);
    if (hasPt) fprintf(f, "point %lld %lld\n", c.pt.x, c.pt.y);
    if (!c.rings.empty()) { fprintf(f, "rings %zu\n", c.rings.size()); for (auto& r : c.rings) { fprintf(f, "%zu", r.size()); for (auto& p : r) fprintf(f, " %lld %lld", p.x, p.y); fprintf(f, "\n"); } }
    if (!c.points.empty()) { fprintf(f, "points %zu\n", c.points.size()); for (auto& p : c.points) fprintf(f, "%lld %lld\n", p.x, p.y); }
    fclose(f); }
static Ring holeInside(Rng& g, const Ring& outer, i64 R, std::vector<Ring>& others) { for (;;) { int n = (int)g.rr(3, 6); Ring h = starRing(g, n, std::max<i64>(2, R / 4), 1); i64 dx = g.rr(-R / 3, R / 3); i64 dy = g.rr(-R / 3, R / 3); for (auto& p : h) { p.x += dx; p.y += dy; }
        orientRing(h, false); if (!simpleRing(h)) continue; bool ok = ringsDisjoint(outer, h) && pip(outer, h[0]) == 1; for (auto& o : others) ok = ok && ringsDisjoint(o, h) && pip(o, h[0]) == 0; if (ok) return h; } }
static P interiorPoint(Rng& g, const std::vector<Ring>& rs, i64 R) { for (;;) { i64 x = g.rr(-R, R); i64 y = g.rr(-R, R); P q{x, y}; int in = pip(rs[0], q); if (in != 1) continue; bool ok = true; for (size_t i = 1; i < rs.size(); i++) if (pip(rs[i], q) != 0) ok = false; if (ok) return q; } }
static i128 gcd128(i128 a, i128 b) { if (a < 0) a = -a; if (b < 0) b = -b; while (b) { i128 t = a % b; a = b; b = t; } return a; }
int main(int argc, char** argv) {
    if (argc < 2) return 2; std::string dir = argv[1]; mkdir(dir.c_str(), 0755); Rng g{0x4D5030384200ULL}; int idx = 0;
    // ---- hm (60): star simple 24, star+1..2 holes 18, histogram (collinear vertices) 12, histogram big 6
    for (int i = 0; i < 60; i++) { Case c; std::string fam;
        if (i < 24) { fam = "star"; int n = (int)g.rr(5, 40); i64 R = g.rr(20, 400); c.rings.push_back(starRing(g, n, R, R / 2)); }
        else if (i < 42) { fam = "starholes"; int n = (int)g.rr(8, 30); i64 R = g.rr(60, 400); Ring o = starRing(g, n, R, R / 2); c.rings.push_back(o); std::vector<Ring> hs; int nh = (int)g.rr(1, 2);
            for (int k = 0; k < nh; k++) { Ring h = holeInside(g, o, R, hs); hs.push_back(h); } for (auto& h : hs) c.rings.push_back(h); }
        else if (i < 54) { fam = "hist"; int k = (int)g.rr(3, 12); c.rings.push_back(histRing(g, k, 4, 10)); orientRing(c.rings[0], true); }
        else { fam = "histbig"; int k = (int)g.rr(15, 40); c.rings.push_back(histRing(g, k, 6, 50)); orientRing(c.rings[0], true); }
        for (auto& r : c.rings) if (!simpleRing(r)) { fprintf(stderr, "gen bug: nonsimple hm ring\n"); return 5; }
        write(dir, idx++, fam, "hm", c); Case d = c; write(dir, idx++, fam, "mono", d); }
    // ---- vis (60): star 20, star+holes 14, hist 10, rooms (rect holes in rect) 16
    for (int i = 0; i < 60; i++) { Case c; std::string fam; i64 R = 30;
        if (i < 20) { fam = "star"; int n = (int)g.rr(6, 30); c.rings.push_back(starRing(g, n, R, R / 3)); }
        else if (i < 34) { fam = "starholes"; int n = (int)g.rr(8, 24); Ring o = starRing(g, n, R, R / 2); c.rings.push_back(o); std::vector<Ring> hs; int nh = (int)g.rr(1, 3); for (int k = 0; k < nh; k++) { Ring h = holeInside(g, o, R, hs); hs.push_back(h); } for (auto& h : hs) c.rings.push_back(h); }
        else if (i < 44) { fam = "hist"; int k = (int)g.rr(4, 12); c.rings.push_back(histRing(g, k, 4, 12)); orientRing(c.rings[0], true); R = 50; }
        else { fam = "rooms"; int W = (int)g.rr(2, 5), H = (int)g.rr(2, 5); c.rings.push_back({{0, 0}, {W * 20, 0}, {W * 20, H * 20}, {0, H * 20}});
            for (int a = 0; a < W; a++) for (int b = 0; b < H; b++) { i64 take = g.rr(0, 99); if (take < 40) { i64 x0 = g.rr(2, 8); i64 y0 = g.rr(2, 8); i64 x1 = g.rr(12, 18); i64 y1 = g.rr(12, 18); Ring h{{a * 20 + x0, b * 20 + y0}, {a * 20 + x0, b * 20 + y1}, {a * 20 + x1, b * 20 + y1}, {a * 20 + x1, b * 20 + y0}}; c.rings.push_back(h); } } R = 100; }
        for (auto& r : c.rings) if (!simpleRing(r)) { fprintf(stderr, "gen bug: nonsimple vis ring\n"); return 5; }
        if (fam == "hist") { i64 xmax = 0, ymax = 0; for (auto& p : c.rings[0]) { xmax = std::max(xmax, p.x); ymax = std::max(ymax, p.y); } for (;;) { i64 x = g.rr(0, xmax); i64 y = g.rr(0, ymax); P q{x, y}; if (pip(c.rings[0], q) == 1) { c.pt = q; break; } } }
        else if (fam == "rooms") { i64 xm = c.rings[0][1].x, ym = c.rings[0][2].y; for (;;) { i64 x = g.rr(1, xm - 1); i64 y = g.rr(1, ym - 1); P q{x, y}; bool ok = true; for (size_t k = 1; k < c.rings.size(); k++) if (pip(c.rings[k], q) != 0) ok = false; if (ok) { c.pt = q; break; } } }
        else c.pt = interiorPoint(g, c.rings, R);
        write(dir, idx++, fam, "vis", c, false, false, true); }
    // ---- alpha (60): random 25, grid with removals 20, clusters 15. alpha chosen as a circumradius^2 of a random triple (ties likely), or fixed.
    for (int i = 0; i < 60; i++) { Case c; std::string fam; std::vector<P>& pts = c.points;
        if (i < 25) { fam = "random"; int n = (int)g.rr(8, 40); i64 R = g.rr(10, 60); while ((int)pts.size() < n) { i64 x = g.rr(0, R); i64 y = g.rr(0, R); bool dup = false; for (auto& p : pts) if (p.x == x && p.y == y) dup = true; if (!dup) pts.push_back({x, y}); } }
        else if (i < 45) { fam = "grid"; int w = (int)g.rr(3, 7), h = (int)g.rr(3, 7); for (int a = 0; a < w; a++) for (int b = 0; b < h; b++) { i64 keep = g.rr(0, 99); if (keep < 80) pts.push_back({a * 2, b * 2}); } if (pts.size() < 4) { pts.clear(); pts = {{0, 0}, {2, 0}, {2, 2}, {0, 2}}; } }
        else { fam = "cluster"; int cl = (int)g.rr(2, 4); for (int k = 0; k < cl; k++) { i64 cx = g.rr(0, 100); i64 cy = g.rr(0, 100); int m = (int)g.rr(4, 10); for (int a = 0; a < m; a++) { i64 dx = g.rr(-8, 8); i64 dy = g.rr(-8, 8); i64 x = cx + dx, y = cy + dy; bool dup = false; for (auto& p : pts) if (p.x == x && p.y == y) dup = true; if (!dup) pts.push_back({x, y}); } } }
        // not all collinear
        bool nc = false; for (size_t a = 2; a < pts.size(); a++) if (orient(pts[0], pts[1], pts[a]) != 0) nc = true; if (!nc) { pts.push_back({pts[0].x + 1, pts[0].y + 7}); }
        // alpha: pick random triple, if non-collinear use its squared circumradius = la*lb*lc/(4*cross^2) as exact fraction; otherwise fixed 1/2
        i64 an = 1, ad = 2; i64 mode = g.rr(0, 4);
        if (mode <= 2) for (int tries = 0; tries < 50; tries++) { size_t a = (size_t)g.rr(0, (i64)pts.size() - 1), b = (size_t)g.rr(0, (i64)pts.size() - 1), d = (size_t)g.rr(0, (i64)pts.size() - 1); if (a == b || b == d || a == d) continue; if (orient(pts[a], pts[b], pts[d]) == 0) continue;
            i128 la = (i128)(pts[a].x - pts[b].x) * (pts[a].x - pts[b].x) + (i128)(pts[a].y - pts[b].y) * (pts[a].y - pts[b].y), lb = (i128)(pts[b].x - pts[d].x) * (pts[b].x - pts[d].x) + (i128)(pts[b].y - pts[d].y) * (pts[b].y - pts[d].y), lc = (i128)(pts[d].x - pts[a].x) * (pts[d].x - pts[a].x) + (i128)(pts[d].y - pts[a].y) * (pts[d].y - pts[a].y);
            i128 cr = (i128)(pts[b].x - pts[a].x) * (pts[d].y - pts[a].y) - (i128)(pts[b].y - pts[a].y) * (pts[d].x - pts[a].x); i128 num = la * lb * lc, den = 4 * cr * cr; i128 gg = gcd128(num, den); num /= gg; den /= gg; an = (i64)num; ad = (i64)den; break; }
        else if (mode == 3) { an = 2 + g.rr(0, 6); ad = 2; } else { an = 1000000; ad = 1; }
        c.an = an; c.ad = ad; write(dir, idx++, fam, "alpha", c, false, true); }
    // ---- repair (60): orthogonal self-crossing 24 (exact integer intersections), orth two-ring 6, general self-crossing 24, outer+protruding hole 6; rule alternates
    for (int i = 0; i < 60; i++) { Case c; std::string fam; c.rule = i & 1;
        if (i < 24) { fam = "orth"; int k = (int)g.rr(3, 8); Ring r; std::vector<i64> x(k + 1), y(k); x[0] = g.rr(0, 30); for (int a = 1; a < k; a++) { for (;;) { x[a] = g.rr(0, 30); if (x[a] != x[a - 1]) break; } } x[k] = x[0]; if (x[k - 1] == x[k]) { x[k - 1] = x[k] + 1; }
            y[0] = g.rr(0, 30); for (int a = 1; a < k; a++) { for (;;) { y[a] = g.rr(0, 30); if (y[a] != y[a - 1]) break; } }
            for (int a = 0; a < k; a++) { r.push_back({x[a], y[a]}); r.push_back({x[a + 1], y[a]}); } c.rings.push_back(r); }
        else if (i < 30) { fam = "orth2"; for (int t = 0; t < 2; t++) { i64 x0 = g.rr(0, 20), y0 = g.rr(0, 20); i64 w = g.rr(5, 20), h = g.rr(5, 20); Ring r{{x0, y0}, {x0 + w, y0}, {x0 + w, y0 + h}, {x0, y0 + h}}; i64 cw = g.rr(0, 1); if (cw) std::reverse(r.begin(), r.end()); c.rings.push_back(r); } }
        else if (i < 54) { fam = "general"; int n = (int)g.rr(5, 14); Ring r; for (int a = 0; a < n; a++) { i64 x = g.rr(0, 40000); i64 y = g.rr(0, 40000); r.push_back({x, y}); } c.rings.push_back(r); }
        else { fam = "holecross"; c.rings.push_back({{0, 0}, {30000, 0}, {30000, 30000}, {0, 30000}}); i64 cx = g.rr(20000, 40000); i64 cy = g.rr(5000, 25000); i64 w = g.rr(5000, 15000); Ring h{{cx, cy}, {cx, cy + w}, {cx - w, cy + w / 2}}; c.rings.push_back(h); i64 cw = g.rr(0, 1); if (cw) std::reverse(c.rings[1].begin(), c.rings[1].end()); }
        write(dir, idx++, fam, "repair", c, true); }
    fprintf(stderr, "wrote %d cases\n", idx); return 0; }
