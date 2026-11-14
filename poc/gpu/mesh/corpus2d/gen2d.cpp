// MP08a 2D corpus generator. Exact-integer polygons (multi-polygons with holes). No libm: all geometry is integer arithmetic, so the output
// bytes do not depend on compiler, libm or architecture. RNG: splitmix64, and EVERY rr() call is its own statement (never inside an
// argument list or brace-init list), because GCC and clang evaluate call arguments in different orders. Build with -ffp-contract=off
// (no floating point is used anyway).
//   gen2d <cases-dir> <speed-dir>
#include "../lib/gap2d/common2d.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <sys/stat.h>
using namespace p2d;
typedef __int128 i128;

struct Rng {
    uint64_t s;
    uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ULL); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); }
    i64 rr(i64 lo, i64 hi) { uint64_t r = next(); return lo + (i64)(r % (uint64_t)(hi - lo + 1)); }
};

static i128 area2(const Ring& r) { i128 a = 0; for (size_t i = 0; i < r.size(); i++) { const P& p = r[i]; const P& q = r[(i + 1) % r.size()]; a += (i128)p.x * q.y - (i128)q.x * p.y; } return a; }
static Ring ccw(Ring r) { if (area2(r) < 0) std::reverse(r.begin(), r.end()); return r; }
static Ring cw(Ring r) { if (area2(r) > 0) std::reverse(r.begin(), r.end()); return r; }
static Ring rectR(i64 x0, i64 y0, i64 x1, i64 y1) { return Ring{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }
static Poly rect(i64 x0, i64 y0, i64 x1, i64 y1) { return Poly{ccw(rectR(x0, y0, x1, y1))}; }
static Poly frame(i64 x0, i64 y0, i64 x1, i64 y1, i64 hx0, i64 hy0, i64 hx1, i64 hy1) { return Poly{ccw(rectR(x0, y0, x1, y1)), cw(rectR(hx0, hy0, hx1, hy1))}; }
static Poly tri(P a, P b, P c) { return Poly{ccw(Ring{a, b, c})}; }
static Set one(const Poly& p) { return Set{p}; }
static Poly shifted(Poly p, i64 dx, i64 dy) { for (auto& r : p) for (auto& q : r) { q.x += dx; q.y += dy; } return p; }
static Set shiftedS(Set s, i64 dx, i64 dy) { for (auto& p : s) p = shifted(p, dx, dy); return s; }
static i64 gcdll(i64 a, i64 b) { a = a < 0 ? -a : a; b = b < 0 ? -b : b; while (b) { i64 t = a % b; a = b; b = t; } return a; }

// Primitive integer directions with |dx|,|dy| <= K, sorted by exact angle (half-plane, then cross product): no libm.
static std::vector<P> dirTable(int K) {
    std::vector<P> d;
    for (int x = -K; x <= K; x++) for (int y = -K; y <= K; y++) if ((x || y) && gcdll(x, y) == 1) d.push_back({x, y});
    auto half = [](const P& p) { return (p.y > 0 || (p.y == 0 && p.x > 0)) ? 0 : 1; };
    std::sort(d.begin(), d.end(), [&](const P& a, const P& b) { int ha = half(a), hb = half(b); if (ha != hb) return ha < hb; return (i128)a.x * b.y - (i128)a.y * b.x > 0; });
    return d;
}
// Star-shaped (hence simple) polygon about (cx,cy): n distinct directions in increasing angle, vertex = c + s*dir, s in [smin,smax].
static Ring starRing(Rng& g, const std::vector<P>& dirs, i64 cx, i64 cy, int n, i64 smin, i64 smax) {
    std::vector<int> idx(dirs.size()); for (size_t i = 0; i < idx.size(); i++) idx[i] = (int)i;
    for (size_t i = idx.size() - 1; i > 0; i--) { i64 j = g.rr(0, (i64)i); std::swap(idx[i], idx[(size_t)j]); }
    idx.resize((size_t)n); std::sort(idx.begin(), idx.end());
    Ring r;
    for (int k : idx) { i64 s = g.rr(smin, smax); r.push_back({cx + s * dirs[(size_t)k].x, cy + s * dirs[(size_t)k].y}); }
    return r;
}

struct Out { std::string dir; int count = 0; void write(const std::string& name, const std::string& fam, int valid, i64 off, const std::vector<Set>& sets) {
    char buf[64]; snprintf(buf, sizeof buf, "%03d_", count++);
    std::string path = dir + "/" + buf + name + ".p2d";
    FILE* f = fopen(path.c_str(), "w"); if (!f) { perror(path.c_str()); exit(1); }
    fprintf(f, "P2D1\nname %s%s\nfamily %s\nvalid %d\noffset %lld\n", buf, name.c_str(), fam.c_str(), valid, off);
    for (auto& s : sets) { fprintf(f, "set %zu\n", s.size()); for (auto& p : s) { fprintf(f, "poly %zu\n", p.size()); for (auto& r : p) { fprintf(f, "%zu", r.size()); for (auto& q : r) fprintf(f, " %lld %lld", q.x, q.y); fprintf(f, "\n"); } } }
    fclose(f); } };

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: gen2d <cases-dir> <speed-dir>\n"); return 2; }
    mkdir(argv[1], 0755); mkdir(argv[2], 0755);
    Out out; out.dir = argv[1];
    Rng g{0x2D0C0DE5EEDULL};
    std::vector<P> d3 = dirTable(3), d2 = dirTable(2);
    struct Pair { std::string name, fam; Set a, b; };
    std::vector<Pair> pairs;
    auto add = [&](const std::string& fam, int k, const Set& a, const Set& b) { pairs.push_back({fam + "_" + std::to_string(k), fam, a, b}); };

    // 1. random axis-aligned rectangles on a 16x16 grid: many coincident edges and corners
    for (int k = 0; k < 24; k++) {
        i64 a0 = g.rr(0, 12); i64 b0 = g.rr(0, 12); i64 a1 = g.rr(a0 + 1, 16); i64 b1 = g.rr(b0 + 1, 16);
        i64 c0 = g.rr(0, 12); i64 e0 = g.rr(0, 12); i64 c1 = g.rr(c0 + 1, 16); i64 e1 = g.rr(e0 + 1, 16);
        Set A = one(rect(a0, b0, a1, b1)), B = one(rect(c0, e0, c1, e1));
        if (k % 4 == 3 && a1 - a0 >= 4 && b1 - b0 >= 4) A = one(frame(a0, b0, a1, b1, a0 + 1, b0 + 1, a1 - 1, b1 - 1));
        add("rects", k, A, B);
    }
    // 2. random convex triangles and quads, extent 1000
    for (int k = 0; k < 16; k++) {
        auto mk = [&](bool quad) { for (;;) { Ring r; int n = quad ? 4 : 3; for (int i = 0; i < n; i++) { i64 x = g.rr(0, 1000); i64 y = g.rr(0, 1000); r.push_back({x, y}); }
            Ring c = ccw(r); bool ok = area2(c) != 0; if (quad && ok) { for (int i = 0; i < 4 && ok; i++) { const P &p = c[(size_t)i], &q = c[(size_t)(i + 1) % 4], &s = c[(size_t)(i + 2) % 4]; if ((i128)(q.x - p.x) * (s.y - q.y) - (i128)(q.y - p.y) * (s.x - q.x) <= 0) ok = false; } } // convex quad only
            if (ok) return Poly{c}; } };
        Poly a = mk(k % 2 == 1); Poly b = mk(k % 3 == 0);
        add("convex", k, one(a), one(b));
    }
    // 3. random star-shaped simple polygons
    for (int k = 0; k < 24; k++) {
        int n = (int)g.rr(5, 14); i64 cx = 500, cy = 500; i64 dx = g.rr(-150, 150); i64 dy = g.rr(-150, 150);
        Ring a = starRing(g, d3, cx, cy, n, 60, 140); int m = (int)g.rr(5, 14); Ring b = starRing(g, d3, cx + dx, cy + dy, m, 60, 140);
        add("star", k, one(Poly{a}), one(Poly{b}));
    }
    // 4. star with a star hole (hole fully inside) against a star
    for (int k = 0; k < 18; k++) {
        int n = (int)g.rr(6, 14); Ring a = starRing(g, d3, 500, 500, n, 80, 140); int h = (int)g.rr(4, 6); Ring hole = starRing(g, d2, 500, 500, h, 8, 10);
        i64 dx = g.rr(-120, 120); i64 dy = g.rr(-120, 120); int m = (int)g.rr(5, 12); Ring b = starRing(g, d3, 500 + dx, 500 + dy, m, 60, 140);
        add("starhole", k, one(Poly{a, cw(hole)}), one(Poly{b}));
    }
    // 5. touching vertices and edges
    for (int k = 0; k < 14; k++) {
        i64 s = g.rr(3, 20); i64 t = g.rr(1, s - 1); i64 u = std::min(t, s - t); Set A = one(rect(0, 0, s, s)), B;
        switch (k % 7) {
            case 0: B = one(rect(s, 0, 2 * s, s)); break;                                       // full shared edge
            case 1: B = one(rect(s, t, 2 * s, t + s)); break;                                   // partial shared edge
            case 2: B = one(rect(s, s, 2 * s, 2 * s)); break;                                   // corner contact
            case 3: B = one(tri({s, t}, {2 * s, t - u}, {2 * s, t + u})); break;                // apex on an edge
            case 4: B = one(Poly{ccw(Ring{{s, t}, {s + u, t - u}, {s + 2 * u, t}, {s + u, t + u}})}); break;  // diamond vertex on an edge
            case 5: A = one(rect(0, 0, 3 * s, 3 * s)); B = one(rect(0, s, s, 2 * s)); break;    // inside, sharing part of an edge
            default: A = one(rect(0, 0, 3 * s, 3 * s)); B = one(rect(0, 0, s, s)); break;       // inside, sharing a corner and two edges
        }
        add("touch", k, A, B);
    }
    // 6. collinear overlapping edges: L-shape and rectangle on a 10-grid
    for (int k = 0; k < 12; k++) {
        i64 w = 10 * g.rr(2, 4); i64 h = 10 * g.rr(2, 4); i64 aw = 10 * g.rr(1, w / 10 - 1); i64 ah = 10 * g.rr(1, h / 10 - 1);
        Ring L = ccw(Ring{{0, 0}, {w, 0}, {w, ah}, {aw, ah}, {aw, h}, {0, h}});
        i64 x0 = 10 * g.rr(0, 2); i64 y0 = 10 * g.rr(0, 2); i64 x1 = x0 + 10 * g.rr(1, 3); i64 y1 = y0 + 10 * g.rr(1, 3);
        add("collinear", k, one(Poly{L}), one(rect(x0, y0, x1, y1)));
    }
    // 7. identical polygons (A against itself)
    {
        Ring st = starRing(g, d3, 500, 500, 10, 60, 140); Ring hole = starRing(g, d2, 500, 500, 5, 8, 10); i64 N = 1LL << 30;
        std::vector<Set> v = {one(Poly{st}), one(frame(0, 0, 100, 100, 20, 20, 80, 80)), one(Poly{st, cw(hole)}), one(rect(0, 0, 7, 9)), one(tri({0, 0}, {10, 0}, {3, 7})),
            one(Poly{ccw(Ring{{0, 0}, {30, 0}, {30, 10}, {10, 10}, {10, 30}, {0, 30}})}), one(tri({0, 0}, {N, N + 1}, {2 * N, 2 * N})), one(rect((1LL << 40), (1LL << 40), (1LL << 40) + 50, (1LL << 40) + 70))};
        for (size_t k = 0; k < v.size(); k++) add("identical", (int)k, v[k], v[k]);
    }
    // 8. nesting around a frame
    for (int k = 0; k < 10; k++) {
        i64 c = g.rr(1, 7); Set A = one(frame(0, 0, 100 * c, 100 * c, 20 * c, 20 * c, 80 * c, 80 * c)), B;
        switch (k) {
            case 0: B = one(rect(30 * c, 30 * c, 50 * c, 50 * c)); break;
            case 1: B = one(rect(20 * c, 20 * c, 80 * c, 80 * c)); break;
            case 2: B = one(rect(10 * c, 10 * c, 90 * c, 90 * c)); break;
            case 3: B = one(rect(20 * c, 30 * c, 40 * c, 50 * c)); break;
            case 4: B = one(rect(0, 0, 100 * c, 100 * c)); break;
            case 5: B = one(rect(5 * c, 5 * c, 15 * c, 15 * c)); break;
            case 6: B = one(rect(10 * c, 40 * c, 50 * c, 60 * c)); break;
            case 7: B = one(frame(10 * c, 0, 110 * c, 100 * c, 30 * c, 20 * c, 90 * c, 80 * c)); break;
            case 8: { Ring s = starRing(g, d2, 50 * c, 50 * c, 6, 3 * c, 4 * c); B = one(Poly{s}); break; }
            default: B = one(frame(20 * c, 20 * c, 80 * c, 80 * c, 30 * c, 30 * c, 70 * c, 70 * c)); break;
        }
        add("nested", k, A, B);
    }
    // 9. slivers: width 2^-30 and 2^-40 of the extent E = 2^50
    {
        const i64 E = 1LL << 50;
        for (int wi = 0; wi < 2; wi++) {
            i64 w = wi == 0 ? (E >> 30) : (E >> 40);
            std::vector<std::pair<Set, Set>> v = {
                {one(rect(0, 0, E, E)), one(rect(-E / 4, E / 2, E + E / 4, E / 2 + w))},
                {one(rect(0, 0, E, E)), one(rect(w, 0, E + w, E))},
                {one(rect(0, 0, E, E)), one(rect(0, 0, E, E - w))},
                {one(rect(0, 0, E, E)), one(tri({0, E / 2}, {E, E / 2}, {E, E / 2 + w}))},
                {one(rect(0, 0, E, E)), one(tri({0, 0}, {E, E - w}, {E, E}))},
                {one(rect(0, 0, E, E)), one(Poly{ccw(Ring{{0, 0}, {E, w}, {E, E + w}, {0, E}})})},
                {one(rect(0, 0, E, w)), one(rect(E / 3, -E / 4, E / 3 + w, E / 4))},
                {one(frame(0, 0, E, E, w, w, E - w, E - w)), one(rect(0, E / 4, E / 2, 3 * E / 4))},
                {one(tri({0, 0}, {E, 0}, {E, w})), one(tri({0, 0}, {E, 0}, {E, 2 * w}))},
                {one(tri({0, 0}, {E, 0}, {E / 2, w})), one(rect(E / 4, 0, 3 * E / 4, E))}};
            for (size_t k = 0; k < v.size(); k++) add(wi == 0 ? "sliver30" : "sliver40", (int)k, v[k].first, v[k].second);
        }
    }
    // 10. near-degenerate triangles (area N for a triangle of extent N)
    for (int ni = 0; ni < 3; ni++) {
        i64 N = ni == 0 ? (1LL << 20) : ni == 1 ? (1LL << 30) : (1LL << 40);
        Poly T1 = tri({0, 0}, {N, N + 1}, {2 * N, 2 * N});
        add("neardeg", ni * 4 + 0, one(T1), one(shifted(T1, 1, 0)));
        add("neardeg", ni * 4 + 1, one(T1), one(tri({0, 1}, {N, N + 2}, {2 * N, 2 * N + 1})));
        add("neardeg", ni * 4 + 2, one(T1), one(rect(N / 2, 0, N, N)));
        add("neardeg", ni * 4 + 3, one(T1), one(tri({0, 0}, {2 * N, 2 * N}, {2 * N, 0})));
    }
    // 11. huge offsets: translated copies (by 2^40) of the first rectangle cases and star cases; re-centring is NOT applied
    {
        const i64 H = 1LL << 40; int nr = 0, ns = 0; std::vector<Pair> add2;
        for (auto& p : pairs) { if (p.fam == "rects" && nr < 6) { nr++; add2.push_back({"huge_" + p.name, "huge", shiftedS(p.a, H, H), shiftedS(p.b, H, H)}); }
                                if (p.fam == "star" && ns < 6) { ns++; add2.push_back({"huge_" + p.name, "huge", shiftedS(p.a, H, -H), shiftedS(p.b, H, -H)}); } }
        for (auto& p : add2) pairs.push_back(p);
    }
    // 12. holes touching the outer ring or each other at a vertex
    for (int k = 0; k < 10; k++) {
        i64 s = g.rr(2, 6); i64 L = 12 * s; Poly A;
        switch (k % 4) {
            case 0: A = Poly{ccw(rectR(0, 0, L, L)), cw(Ring{{L / 2, 0}, {L / 2 + 2 * s, 2 * s}, {L / 2, 4 * s}, {L / 2 - 2 * s, 2 * s}})}; break;                    // diamond hole, vertex on the bottom edge
            case 1: A = Poly{ccw(rectR(0, 0, L, L)), cw(Ring{{L / 2, L}, {L / 2 + 2 * s, L - 3 * s}, {L / 2 - 2 * s, L - 3 * s}})}; break;                           // triangular hole, vertex on the top edge
            case 2: A = Poly{ccw(rectR(0, 0, L, L)), cw(rectR(2 * s, 2 * s, 5 * s, 5 * s)), cw(rectR(5 * s, 5 * s, 8 * s, 8 * s))}; break;                           // two holes sharing a corner
            default: A = Poly{ccw(rectR(0, 0, L, L)), cw(Ring{{0, L / 2}, {2 * s, L / 2 - 2 * s}, {4 * s, L / 2}, {2 * s, L / 2 + 2 * s}}), cw(Ring{{4 * s, L / 2}, {6 * s, L / 2 - s}, {6 * s, L / 2 + s}})}; break; // left-edge vertex hole plus a hole touching it
        }
        i64 x0 = g.rr(0, 6 * s); i64 y0 = g.rr(0, 6 * s); i64 x1 = g.rr(x0 + 1, L + 4 * s); i64 y1 = g.rr(y0 + 1, L + 4 * s);
        add("selftouch", k, one(A), one(rect(x0, y0, x1, y1)));
    }
    // 13. multi-polygons: two disjoint polygons (one with a hole) against a rectangle or star
    for (int k = 0; k < 12; k++) {
        Set A = {frame(0, 0, 40, 40, 10, 10, 20, 20), rect(60, 0, 100, 40)};
        Set B;
        if (k % 2 == 0) { i64 x0 = g.rr(-10, 60); i64 y0 = g.rr(-10, 30); i64 x1 = g.rr(x0 + 1, 110); i64 y1 = g.rr(y0 + 1, 50); B = one(rect(x0, y0, x1, y1)); }
        else { int m = (int)g.rr(5, 10); i64 dx = g.rr(-20, 20); Ring b = starRing(g, d3, 50 + dx, 20, m, 6, 14); B = one(Poly{b}); }
        add("multi", k, A, B);
    }
    for (auto& p : pairs) out.write(p.name, p.fam, 1, 0, {p.a, p.b});
    int nPairs = out.count;

    // 14. offset (miter) of rectilinear polygons; oracle = polygon union the edge-by-d rectangles (the Minkowski sum with the square)
    for (int k = 0; k < 14; k++) {
        i64 d = g.rr(1, 6); i64 u = g.rr(4, 12); Poly P;
        switch (k % 7) {
            case 0: P = rect(0, 0, 3 * u, 2 * u); break;
            case 1: P = Poly{ccw(Ring{{0, 0}, {3 * u, 0}, {3 * u, u}, {u, u}, {u, 3 * u}, {0, 3 * u}})}; break;                                           // L
            case 2: P = Poly{ccw(Ring{{0, 0}, {3 * u, 0}, {3 * u, 3 * u}, {2 * u, 3 * u}, {2 * u, u}, {u, u}, {u, 3 * u}, {0, 3 * u}})}; break;          // U, gap u
            case 3: P = Poly{ccw(Ring{{u, 0}, {2 * u, 0}, {2 * u, u}, {3 * u, u}, {3 * u, 2 * u}, {2 * u, 2 * u}, {2 * u, 3 * u}, {u, 3 * u}, {u, 2 * u}, {0, 2 * u}, {0, u}, {u, u}})}; break; // plus
            case 4: P = frame(0, 0, 4 * u, 4 * u, u, u, 3 * u, 3 * u); break;                                                                             // frame, hole 2u
            case 5: P = frame(0, 0, 6 * u, 6 * u, 2 * u, 2 * u, 2 * u + 3, 2 * u + 3); break;                                                             // tiny hole (vanishes for d>=2)
            default: P = Poly{ccw(Ring{{0, 0}, {6 * u, 0}, {6 * u, u}, {4 * u, u}, {4 * u, 3 * u}, {2 * u, 3 * u}, {2 * u, u}, {0, u}})}; break;          // T
        }
        out.write("offset_" + std::to_string(k), "offset", 1, d, {one(P)});
    }
    int nOffset = out.count - nPairs;

    // 15. invalid inputs (A invalid, B = valid square; the checker verifies each is actually invalid)
    Set sq = one(rect(0, 0, 100, 100)); int nInv = 0;
    auto inv = [&](const std::string& name, const Set& A) { out.write("inv_" + name, "invalid", 0, 0, {A, sq}); nInv++; };
    inv("bowtie", one(Poly{Ring{{0, 0}, {80, 80}, {80, 0}, {0, 80}}}));
    inv("pentagram", one(Poly{Ring{{50, 0}, {80, 90}, {5, 35}, {95, 35}, {20, 90}}}));
    inv("two_vertex_ring", one(Poly{Ring{{0, 0}, {50, 50}}}));
    inv("one_vertex_ring", one(Poly{Ring{{10, 10}}}));
    inv("dup_vertex", one(Poly{Ring{{0, 0}, {50, 0}, {50, 0}, {50, 50}, {0, 50}}}));
    inv("zero_area", one(Poly{Ring{{0, 0}, {10, 10}, {20, 20}}}));
    inv("hole_outside", one(Poly{ccw(rectR(0, 0, 50, 50)), cw(rectR(60, 60, 80, 80))}));
    inv("hole_crossing", one(Poly{ccw(rectR(0, 0, 50, 50)), cw(rectR(40, 40, 70, 70))}));
    inv("overlap_outers", Set{rect(0, 0, 50, 50), rect(25, 25, 75, 75)});
    inv("spike", one(Poly{Ring{{0, 0}, {50, 0}, {50, 50}, {70, 50}, {50, 50}, {0, 50}}}));
    inv("duplicate_ring", Set{rect(0, 0, 50, 50), rect(0, 0, 50, 50)});
    inv("outer_clockwise", one(Poly{cw(rectR(0, 0, 50, 50))}));
    inv("hole_ccw", one(Poly{ccw(rectR(0, 0, 50, 50)), ccw(rectR(10, 10, 20, 20))}));
    inv("figure8_vertex", one(Poly{Ring{{0, 0}, {50, 0}, {25, 25}, {0, 50}, {50, 50}, {25, 25}}}));
    inv("closed_by_repeat", one(Poly{Ring{{0, 0}, {50, 0}, {50, 50}, {0, 50}, {0, 0}}}));
    inv("hole_edge_overlap", one(Poly{ccw(rectR(0, 0, 50, 50)), cw(rectR(0, 10, 20, 30))}));
    inv("huge_bowtie", one(Poly{Ring{{0, 0}, {1LL << 50, 1LL << 50}, {1LL << 50, 0}, {0, 1LL << 50}}}));
    inv("hole_in_hole", one(Poly{ccw(rectR(0, 0, 90, 90)), cw(rectR(10, 10, 80, 80)), cw(rectR(30, 30, 50, 50))}));
    inv("all_same_vertex", one(Poly{Ring{{5, 5}, {5, 5}, {5, 5}}}));
    inv("overlapping_holes", one(Poly{ccw(rectR(0, 0, 90, 90)), cw(rectR(10, 10, 50, 50)), cw(rectR(30, 30, 70, 70))}));
    fprintf(stderr, "pairs=%d offset=%d invalid=%d\n", nPairs, nOffset, nInv);

    // 16. speed polygons: star-shaped about a centre, n vertices from the sorted primitive directions (K=300), slowly varying radius (7 lobes for A, 5 for B, factor 0.8..1.0) plus jitter below one edge spacing (R/n)
    {
        std::vector<P> D = dirTable(300);
        const i64 R = 1LL << 30;
        for (int n : {1000, 100000}) {
            Out sp; sp.dir = argv[n == 1000 ? 1 : 2];
            std::vector<Set> sets;
            for (int which = 0; which < 2; which++) {
                size_t stride = D.size() / (size_t)n; size_t off = which == 0 ? 0 : stride / 2;
                i64 cx = which == 0 ? 0 : R / 3, cy = which == 0 ? 0 : R / 5; Ring r;
                for (int i = 0; i < n; i++) {
                    const P& dd = D[off + (size_t)i * stride]; i64 m = std::max(dd.x < 0 ? -dd.x : dd.x, dd.y < 0 ? -dd.y : dd.y);
                    i64 lobes = which == 0 ? 7 : 5; i64 period = (i64)n / lobes; i64 half = period / 2; i64 ph = (i64)i % period; i64 val = ph < half ? ph : period - ph;
                    i64 s = (R / m) * (4 * half + val) / (5 * half); i64 j = g.rr(0, R / n); s -= j;
                    r.push_back({cx + s * dd.x, cy + s * dd.y});
                }
                sets.push_back(one(Poly{r}));
            }
            char nm[32]; snprintf(nm, sizeof nm, "spd%s", n == 1000 ? "1k" : "100k");
            FILE* f = nullptr; (void)f;
            std::string path = std::string(argv[n == 1000 ? 1 : 2]) + "/" + nm + ".p2d";
            FILE* fo = fopen(path.c_str(), "w"); if (!fo) { perror(path.c_str()); return 1; }
            fprintf(fo, "P2D1\nname %s\nfamily speed\nvalid 1\noffset 0\n", nm);
            for (auto& s : sets) { fprintf(fo, "set 1\npoly 1\n%zu", s[0][0].size()); for (auto& q : s[0][0]) fprintf(fo, " %lld %lld", q.x, q.y); fprintf(fo, "\n"); }
            fclose(fo);
        }
    }
    return 0;
}
