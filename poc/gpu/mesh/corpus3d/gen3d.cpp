// MP07 3D corpus generator. Integer-only (no libm, no floating point): output bytes do not depend on compiler, libm or architecture.
// RNG splitmix64 with EVERY rr() call in its own statement (GCC and clang order call arguments differently).   gen3d <cases-dir> <speed-dir>
#include "../lib/gap3d/c3d.h"
#include "../harness/mesh_io.h"
#include <algorithm>
#include <sys/stat.h>
using namespace c3d;
typedef __int128 i128;
struct Rng { uint64_t s;
    uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ULL); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); }
    i64 rr(i64 lo, i64 hi) { uint64_t r = next(); return lo + (i64)(r % (uint64_t)(hi - lo + 1)); } };
typedef std::array<i64, 2> P2; typedef std::vector<P2> R2;
static i128 area2(const R2& r) { i128 a = 0; for (size_t i = 0; i < r.size(); i++) { const P2& p = r[i]; const P2& q = r[(i + 1) % r.size()]; a += (i128)p[0] * q[1] - (i128)q[0] * p[1]; } return a; }
static R2 ccw(R2 r) { if (area2(r) < 0) std::reverse(r.begin(), r.end()); return r; }
static R2 cw(R2 r) { if (area2(r) > 0) std::reverse(r.begin(), r.end()); return r; }
static int half(const P2& p) { return (p[1] > 0 || (p[1] == 0 && p[0] > 0)) ? 0 : 1; }
static bool angLess(const P2& a, const P2& b) { int ha = half(a), hb = half(b); if (ha != hb) return ha < hb; return (i128)a[0] * b[1] - (i128)a[1] * b[0] > 0; }
// star-shaped simple polygon about the origin: anchors on the four axes (angular gaps <= 90 deg) plus random vertices with radius in [R/2, R]
static R2 star(Rng& g, i64 R, int nrand, i64 rminNum = 1, i64 rminDen = 2) {
    R2 pts; i64 lo = R * rminNum / rminDen; if (lo < 2) lo = 2;
    i64 a0 = g.rr(lo, R); i64 a1 = g.rr(lo, R); i64 a2 = g.rr(lo, R); i64 a3 = g.rr(lo, R);
    pts.push_back({a0, 0}); pts.push_back({0, a1}); pts.push_back({-a2, 0}); pts.push_back({0, -a3});
    for (int k = 0; k < nrand; k++) {
        i64 x = g.rr(-R, R); i64 y = g.rr(-R, R);
        i128 r2 = (i128)x * x + (i128)y * y; if (r2 < (i128)lo * lo || r2 > (i128)R * R) continue;
        bool dup = false; for (auto& q : pts) if ((i128)q[0] * y - (i128)q[1] * x == 0 && (i128)q[0] * x + (i128)q[1] * y > 0) dup = true;
        if (!dup) pts.push_back({x, y});
    }
    std::sort(pts.begin(), pts.end(), angLess); return ccw(pts);
}
static R2 rect(i64 x0, i64 y0, i64 x1, i64 y1) { return R2{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }
struct Prof { std::vector<R2> rings; int holes = 0; };     // rings[0] outer ccw, then holes cw
static Prof starProf(Rng& g, i64 R, int nrand, int withHole) { Prof p; p.rings.push_back(star(g, R, nrand)); if (withHole) { p.rings.push_back(cw(star(g, R * 3 / 10, 4, 1, 4))); p.holes = 1; } return p; }
static Prof gridProf(i64 cell, int kx, int ky) {   // rectangle with kx*ky square holes
    Prof p; i64 W = (3 * kx + 1) * cell, H = (3 * ky + 1) * cell; p.rings.push_back(rect(0, 0, W, H));
    for (int i = 0; i < kx; i++) for (int j = 0; j < ky; j++) p.rings.push_back(cw(rect((3 * i + 1) * cell + cell / 2, (3 * j + 1) * cell + cell / 2, (3 * i + 2) * cell + cell / 2, (3 * j + 2) * cell + cell / 2)));
    p.holes = kx * ky; return p; }
static Prof shiftProf(Prof p, i64 dx, i64 dy) { for (auto& r : p.rings) for (auto& q : r) { q[0] += dx; q[1] += dy; } return p; }
static void addProf2(Case& c, const Prof& p) { for (auto& r : p.rings) { Ring o; o.dim = 2; for (auto& q : r) o.p.push_back({q[0], q[1], 0}); c.rings.push_back(o); } c.param["holes"] = {p.holes}; }
// ---- solids (outward ccw) ----
static IMesh boxM(i64 x0, i64 y0, i64 z0, i64 x1, i64 y1, i64 z1) {
    IMesh m; for (int i = 0; i < 8; i++) m.v.push_back({(i & 1) ? x1 : x0, (i & 2) ? y1 : y0, (i & 4) ? z1 : z0});
    int q[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};   // -x +x -y +y -z +z
    for (auto& f : q) { m.t.push_back({f[0], f[1], f[2]}); m.t.push_back({f[0], f[2], f[3]}); } return m; }
static i128 det3(const V3& a, const V3& b, const V3& c) { return (i128)a[0] * ((i128)b[1] * c[2] - (i128)b[2] * c[1]) + (i128)a[1] * ((i128)b[2] * c[0] - (i128)b[0] * c[2]) + (i128)a[2] * ((i128)b[0] * c[1] - (i128)b[1] * c[0]); }
static i128 vol6(const IMesh& m) { i128 s = 0; for (auto& t : m.t) s += det3(m.v[t[0]], m.v[t[1]], m.v[t[2]]); return s; }
static void fixOrient(IMesh& m) { if (vol6(m) < 0) for (auto& t : m.t) std::swap(t[1], t[2]); }
static IMesh tetraM(V3 a, V3 b, V3 c, V3 d) { IMesh m; m.v = {a, b, c, d}; m.t = {{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {0, 2, 3}}; fixOrient(m); return m; }
static IMesh octaM(i64 a, i64 b, i64 c) { IMesh m; m.v = {{a, 0, 0}, {-a, 0, 0}, {0, b, 0}, {0, -b, 0}, {0, 0, c}, {0, 0, -c}};
    for (int sx = 0; sx < 2; sx++) for (int sy = 0; sy < 2; sy++) for (int sz = 0; sz < 2; sz++) { if ((sx + sy + sz) & 1) m.t.push_back({sx, 4 + sz, 2 + sy}); else m.t.push_back({sx, 2 + sy, 4 + sz}); } fixOrient(m); return m; }
static R2 parab(i64 s, int k) { R2 r; for (int i = -k; i <= k; i++) r.push_back({i * s, i * i * s}); return ccw(r); }   // convex: (i, i^2) chain closed by its chord
static IMesh prismM(const R2& poly, i64 h) {
    IMesh m; int n = (int)poly.size(); for (int i = 0; i < n; i++) m.v.push_back({poly[i][0], poly[i][1], 0}); for (int i = 0; i < n; i++) m.v.push_back({poly[i][0], poly[i][1], h});
    for (int i = 1; i + 1 < n; i++) { m.t.push_back({0, i + 1, i}); m.t.push_back({n, n + i, n + i + 1}); }
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; m.t.push_back({i, j, n + j}); m.t.push_back({i, n + j, n + i}); } fixOrient(m); return m; }
static IMesh pyramidM(const R2& poly, i64 h, i64 ax, i64 ay) {
    IMesh m; int n = (int)poly.size(); for (int i = 0; i < n; i++) m.v.push_back({poly[i][0], poly[i][1], 0}); m.v.push_back({ax, ay, h});
    for (int i = 1; i + 1 < n; i++) m.t.push_back({0, i + 1, i}); for (int i = 0; i < n; i++) m.t.push_back({i, (i + 1) % n, n}); fixOrient(m); return m; }
static IMesh xform(const IMesh& m, Rng& g, bool shear, i64 tr) {
    i64 a = g.rr(1, 3), d = g.rr(1, 3), f = g.rr(1, 3), b = shear ? g.rr(-2, 2) : 0, cc = shear ? g.rr(-2, 2) : 0, e = shear ? g.rr(-2, 2) : 0;
    i64 tx = g.rr(-tr, tr), ty = g.rr(-tr, tr), tz = g.rr(-tr, tr); IMesh o = m;
    for (auto& v : o.v) { i64 x = v[0], y = v[1], z = v[2]; v = {a * x + b * y + cc * z + tx, d * y + e * z + ty, f * z + tz}; } return o; }
static IMesh anySolid(Rng& g, int kind, i64 sc) {
    switch (kind % 6) {
        case 0: { i64 a = g.rr(1, sc); i64 b = g.rr(1, sc); i64 c = g.rr(1, sc); return boxM(0, 0, 0, a, b, c); }
        case 1: { i64 a = g.rr(1, sc); i64 b = g.rr(1, sc); i64 c = g.rr(1, sc); return tetraM({0, 0, 0}, {a, 0, 0}, {0, b, 0}, {0, 0, c}); }
        case 2: { i64 a = g.rr(1, sc); i64 b = g.rr(1, sc); i64 c = g.rr(1, sc); return octaM(a, b, c); }
        case 3: { i64 s = g.rr(1, 3); i64 h = g.rr(1, sc); int k = (int)g.rr(1, 3); return prismM(parab(s, k + 1), h); }
        case 4: { i64 s = g.rr(1, 3); i64 h = g.rr(1, sc); int k = (int)g.rr(1, 3); return pyramidM(parab(s, k + 1), h, 0, s); }
        default: { i64 a = g.rr(1, sc); i64 b = g.rr(1, sc); i64 c = g.rr(1, sc); i64 d = g.rr(1, sc); return tetraM({0, 0, 0}, {a, 0, 0}, {b, c, 0}, {0, d, a + 1}); }
    } }
static IMesh shifted(IMesh m, i64 dx, i64 dy, i64 dz) { for (auto& v : m.v) { v[0] += dx; v[1] += dy; v[2] += dz; } return m; }
static IMesh scaled(IMesh m, i64 k) { for (auto& v : m.v) for (int i = 0; i < 3; i++) v[i] *= k; return m; }

static V3 rv(Rng& g, i64 lo, i64 hi) { V3 r; r[0] = g.rr(lo, hi); r[1] = g.rr(lo, hi); r[2] = g.rr(lo, hi); return r; }
static std::string outDir; static int counter = 0;
static void emit(Case& c) { char nm[64]; snprintf(nm, sizeof nm, "%s_%03d", c.kind.c_str(), counter++); c.name = nm; std::string p = outDir + "/" + c.name + ".c3d"; FILE* f = fopen(p.c_str(), "w"); if (!f) { perror(p.c_str()); exit(1); } write(f, c); fclose(f); }
static Case mk(const char* kind, const char* cls, int exp = 0) { Case c; c.kind = kind; c.cls = cls; c.exp = exp; return c; }
static void sliverPoly(Prof& p) { p.rings.clear(); p.rings.push_back(rect(0, 0, 1LL << 30, 1)); p.holes = 0; }

static void genExtrude(Rng& g) {
    auto vecP = [&](Case& c, bool shear, i64 hmax) { i64 h = g.rr(1, hmax); i64 dx = 0, dy = 0; if (shear) { dx = g.rr(-30, 30); dy = g.rr(-30, 30); } c.param["vec"] = {dx, dy, h}; };
    for (int i = 0; i < 20; i++) { Case c = mk("extrude", "star"); i64 R = g.rr(20, 1000); int nr = (int)g.rr(3, 12); Prof p = starProf(g, R, nr, 0); i64 sx = g.rr(-50, 50); i64 sy = g.rr(-50, 50); addProf2(c, shiftProf(p, sx, sy)); vecP(c, i % 2, 100); emit(c); }
    for (int i = 0; i < 16; i++) { Case c = mk("extrude", "star_hole"); i64 R = g.rr(40, 1000); int nr = (int)g.rr(3, 12); Prof p = starProf(g, R, nr, 1); addProf2(c, p); vecP(c, i % 2, 100); emit(c); }
    int gk[8][2] = {{2, 2}, {3, 2}, {1, 3}, {2, 1}, {3, 3}, {1, 1}, {2, 3}, {4, 1}};
    for (int i = 0; i < 8; i++) { Case c = mk("extrude", "grid_holes"); i64 cell = g.rr(2, 20); addProf2(c, gridProf(cell, gk[i][0], gk[i][1])); vecP(c, i % 2, 60); emit(c); }
    for (int i = 0; i < 6; i++) { Case c = mk("extrude", "box_shear"); i64 w = g.rr(1, 50); i64 hh = g.rr(1, 50); Prof p; p.rings.push_back(rect(0, 0, w, hh)); addProf2(c, p); vecP(c, true, 50); emit(c); }
    for (int i = 0; i < 4; i++) { Case c = mk("extrude", "collinear"); Ring r; r.dim = 2; i64 k = g.rr(1, 5); int n = (int)g.rr(3, 6); for (int j = 0; j < n; j++) r.p.push_back({j * k, j * k, 0}); c.rings.push_back(r); c.param["holes"] = {0}; vecP(c, false, 10); emit(c); }
    for (int i = 0; i < 6; i++) { Case c = mk("extrude", "sliver", -30); Prof p; sliverPoly(p); if (i >= 3) { p.rings[0] = rect(0, 0, 1, 1LL << 30); } addProf2(c, p); c.param["vec"] = {i % 2 ? 1LL << 29 : 0, 0, (i % 3 == 0) ? 1 : (1LL << 30)}; emit(c); }
    for (int i = 0; i < 4; i++) { Case c = mk("extrude", "convex"); Prof p; i64 ps = g.rr(1, 5); int pk = (int)g.rr(1, 4); p.rings.push_back(parab(ps, pk)); addProf2(c, p); vecP(c, i % 2, 30); emit(c); }
}
static void genRevolve(Rng& g) {
    auto prof = [&](Case& c, int type, i64 s) {
        Prof p;
        switch (type) {
            case 0: { i64 x0 = g.rr(1, 5) * s; i64 x1 = x0 + g.rr(1, 6) * s; i64 y0 = g.rr(-5, 5); i64 y1 = y0 + g.rr(1, 8); p.rings.push_back(rect(x0, y0, x1, y1)); c.cls = "rect_offaxis"; break; }
            case 1: { i64 x1 = g.rr(1, 8) * s; i64 y0 = g.rr(-5, 5); i64 y1 = y0 + g.rr(1, 8); p.rings.push_back(rect(0, y0, x1, y1)); c.cls = "rect_on_axis_edge"; break; }
            case 2: { i64 x1 = g.rr(1, 8) * s; i64 y0 = g.rr(-5, 5); i64 y1 = y0 + g.rr(2, 9); p.rings.push_back(ccw(R2{{0, y0}, {x1, y0 + 1}, {0, y1}})); c.cls = "tri_axis_vertices"; break; }
            case 3: { i64 R = g.rr(10, 40) * s; int nr = (int)g.rr(2, 8); Prof q = starProf(g, R, nr, 0); p = shiftProf(q, R + g.rr(1, 10), 0); c.cls = "star_offaxis"; break; }
            case 4: { i64 R = g.rr(40, 100) * s; Prof q = starProf(g, R, 4, 1); p = shiftProf(q, R + g.rr(1, 10), 0); c.cls = "star_hole_torus"; break; }
            case 5: { i64 a = g.rr(1, 6) * s; i64 b = g.rr(1, 8) * s; i64 y0 = g.rr(-5, 5); i64 y1 = y0 + g.rr(1, 8); p.rings.push_back(rect(-a, y0, b, y1)); c.cls = "through_axis_rect"; break; }
            case 6: { i64 a = g.rr(1, 4); i64 sl = g.rr(-2, 2); i64 y0 = g.rr(-5, 5); i64 b = g.rr(1, 6); i64 y1 = y0 + 2 * a * sl;   // edge from (-a,y0) to (a,y1): crossing y=(y0+y1)/2 integer, slope integer
                     p.rings.push_back(ccw(R2{{-a, y0}, {a, y1}, {a, y1 + b + 6}, {-a, y0 + b + 6}})); c.cls = "through_axis_slanted"; break; }
            default: { i64 x0 = g.rr(1, 4); p.rings.push_back(rect(x0, 0, x0 + 1, 1LL << 12)); c.cls = "tall_thin"; break; }
        }
        addProf2(c, p); };
    for (int i = 0; i < 40; i++) { Case c = mk("revolve", "x"); prof(c, i % 8, 1); c.param["seg"] = {4}; c.param["deg"] = {360}; emit(c); }
    for (int i = 0; i < 8; i++) { Case c = mk("revolve", "x"); prof(c, i % 5, 1); c.param["seg"] = {3}; c.param["deg"] = {270}; emit(c); }
    int ns[8] = {3, 5, 6, 8, 12, 36, 64, 128};
    for (int i = 0; i < 8; i++) { Case c = mk("revolve", "x"); prof(c, i % 6, 1); c.param["seg"] = {ns[i]}; c.param["deg"] = {360}; c.cls += "_generic"; emit(c); }
}
static void addPlane(Case& c, const std::vector<R2>& rings, Rng& g, bool mirror) {
    i64 e1[3], e2[3];
    for (;;) { for (int k = 0; k < 3; k++) e1[k] = g.rr(-3, 3); for (int k = 0; k < 3; k++) e2[k] = g.rr(-3, 3);
        i64 n0 = e1[1] * e2[2] - e1[2] * e2[1], n1 = e1[2] * e2[0] - e1[0] * e2[2], n2 = e1[0] * e2[1] - e1[1] * e2[0]; if (n0 || n1 || n2) break; }
    i64 o[3]; for (int k = 0; k < 3; k++) o[k] = g.rr(-100, 100);
    for (auto& r : rings) { Ring o3; o3.dim = 3; for (auto& q : r) { std::array<i64, 3> p; for (int k = 0; k < 3; k++) p[k] = o[k] + q[0] * e1[k] + q[1] * e2[k]; o3.p.push_back(p); } if (mirror) std::reverse(o3.p.begin(), o3.p.end()); c.rings.push_back(o3); }
}
static void genTess(Rng& g) {
    for (int i = 0; i < 10; i++) { Case c = mk("tess", "star"); i64 R = g.rr(20, 500); int nr = (int)g.rr(3, 14); Prof p = starProf(g, R, nr, 0); addPlane(c, p.rings, g, false); emit(c); }
    for (int i = 0; i < 12; i++) { Case c = mk("tess", "star_hole"); i64 R = g.rr(40, 500); int nr = (int)g.rr(3, 14); Prof p = starProf(g, R, nr, 1); addPlane(c, p.rings, g, false); emit(c); }
    for (int i = 0; i < 8; i++) { Case c = mk("tess", "grid_holes"); i64 cell = g.rr(2, 20); Prof p = gridProf(cell, 1 + i % 3, 1 + (i / 2) % 3); addPlane(c, p.rings, g, false); emit(c); }
    for (int i = 0; i < 6; i++) { Case c = mk("tess", "collinear_vertices"); i64 w = g.rr(2, 8), h = g.rr(2, 8); R2 r; for (i64 x = 0; x < w; x++) r.push_back({x, 0}); for (i64 y = 0; y < h; y++) r.push_back({w, y}); for (i64 x = w; x > 0; x--) r.push_back({x, h}); for (i64 y = h; y > 0; y--) r.push_back({0, y}); addPlane(c, {r}, g, false); emit(c); }
    for (int i = 0; i < 6; i++) { Case c = mk("tess", "concave_comb"); int teeth = 2 + i; R2 r; r.push_back({0, 0}); r.push_back({4 * teeth + 1, 0}); for (int k = teeth - 1; k >= 0; k--) { r.push_back({4 * k + 4, 10}); r.push_back({4 * k + 3, 10}); r.push_back({4 * k + 3, 2}); r.push_back({4 * k + 1, 2}); r.push_back({4 * k + 1, 10}); r.push_back({4 * k, 10}); } R2 q; q.push_back({0, 0}); q.push_back({4 * teeth + 1, 0}); q.push_back({4 * teeth + 1, 12}); q.push_back({0, 12});
        for (int k = 0; k < teeth; k++) {} addPlane(c, {ccw(q), cw(rect(2, 4, 4 * teeth - 1, 8))}, g, false); emit(c); }
    for (int i = 0; i < 6; i++) { Case c = mk("tess", "sliver", -30); R2 r = (i % 2) ? R2{{0, 0}, {1LL << 30, 0}, {1LL << 30, 1}} : R2{{0, 0}, {1LL << 30, 1}, {0, 1}}; if (i >= 4) r = ccw(R2{{0, 0}, {1LL << 30, 0}, {1LL << 30, 1}, {0, 1}}); addPlane(c, {ccw(r)}, g, false); emit(c); }
    for (int i = 0; i < 4; i++) { Case c = mk("tess", "collinear_zero_area"); R2 r; i64 k = g.rr(1, 5); for (int j = 0; j < 3 + i; j++) r.push_back({j * k, 2 * j * k}); addPlane(c, {r}, g, false); emit(c); }
    for (int i = 0; i < 6; i++) { Case c = mk("tess", "mirror_cw_outer"); i64 R = g.rr(20, 300); int nr = (int)g.rr(3, 10); Prof p = starProf(g, R, nr, i % 2); addPlane(c, p.rings, g, true); emit(c); }
}
static void genMink(Rng& g) {
    for (int i = 0; i < 40; i++) { Case c = mk("mink", "random"); IMesh a = xform(anySolid(g, (int)g.rr(0, 5), 8), g, i % 2, 20); IMesh b = xform(anySolid(g, (int)g.rr(0, 5), 8), g, i % 3 == 0, 20); c.mesh["A"] = a; c.mesh["B"] = b; emit(c); }
    for (int i = 0; i < 8; i++) { Case c = mk("mink", "boxes_coplanar"); i64 a = g.rr(1, 6), b = g.rr(1, 6), d = g.rr(1, 6); i64 a2 = g.rr(1, 6), b2 = (i % 2) ? b : g.rr(1, 6), d2 = (i % 3) ? d : g.rr(1, 6); c.mesh["A"] = boxM(0, 0, 0, a, b, d); c.mesh["B"] = boxM(-a2, 0, 0, 0, b2, d2); emit(c); }
    for (int i = 0; i < 4; i++) { Case c = mk("mink", "sliver", -30); c.mesh["A"] = boxM(0, 0, 0, 1LL << 30, 1LL << 30, 1); c.mesh["B"] = (i % 2) ? boxM(0, 0, 0, 1, 1, 1LL << 30) : octaM(1, 1, 1LL << 30); emit(c); }
    for (int i = 0; i < 4; i++) { Case c = mk("mink", "flat_summand"); c.mesh["A"] = xform(anySolid(g, i, 8), g, true, 10); i64 s = g.rr(1, 5); c.pts["B"] = {{0, 0, 0}, {s, 0, 0}, {s, s, 0}, {0, s, 0}}; emit(c); }
}
struct Pair { IMesh a, b; };
static void genDist(Rng& g) {
    // point-mesh
    for (int i = 0; i < 24; i++) { Case c = mk("dist_pm", "rand_out"); IMesh m = xform(anySolid(g, (int)g.rr(0, 5), 10), g, i % 2, 10); c.mesh["A"] = m; c.hasPt = true; c.pt = rv(g, -40, 60); emit(c); }
    for (int i = 0; i < 12; i++) { Case c = mk("dist_pm", "on_surface"); IMesh m = scaled(xform(anySolid(g, (int)g.rr(0, 5), 10), g, i % 2, 10), 2); c.mesh["A"] = m; c.hasPt = true;
        if (i % 3 == 0) { c.pt = m.v[(size_t)g.rr(0, (i64)m.v.size() - 1)]; c.cls = "on_vertex"; }
        else { const auto& t = m.t[(size_t)g.rr(0, (i64)m.t.size() - 1)]; int e = (int)g.rr(0, 2); const V3 &p = m.v[t[e]], &q = m.v[t[(e + 1) % 3]]; c.pt = {(p[0] + q[0]) / 2, (p[1] + q[1]) / 2, (p[2] + q[2]) / 2}; c.cls = "on_edge_mid"; } emit(c); }
    for (int i = 0; i < 8; i++) { Case c = mk("dist_pm", "inside"); i64 bx = g.rr(8, 20); i64 by = g.rr(8, 20); i64 bz = g.rr(8, 20); IMesh m = boxM(0, 0, 0, bx, by, bz); c.mesh["A"] = m; c.hasPt = true; c.pt = rv(g, 1, 7); emit(c); }
    for (int i = 0; i < 8; i++) { Case c = mk("dist_pm", "sliver", -30); c.mesh["A"] = boxM(0, 0, 0, 1LL << 30, 1LL << 30, 1); c.hasPt = true; i64 px = g.rr(0, 1LL << 30); i64 py = g.rr(0, 1LL << 30); i64 pz = (i % 2) ? g.rr(2, 5) : -g.rr(1, 4); c.pt = {px, py, pz}; emit(c); }
    for (int i = 0; i < 6; i++) { Case c = mk("dist_pm", "far_offset"); IMesh m = shifted(xform(anySolid(g, (int)g.rr(0, 5), 10), g, true, 10), 1LL << 28, 1LL << 28, 1LL << 28); c.mesh["A"] = m; c.hasPt = true; V3 o = rv(g, -20, 40); c.pt = {(1LL << 28) + o[0], (1LL << 28) + o[1], (1LL << 28) + o[2]}; emit(c); }
    for (int i = 0; i < 6; i++) { Case c = mk("dist_pm", "tie_symmetric"); i64 a = g.rr(1, 8); c.mesh["A"] = boxM(0, 0, 0, 2 * a, 2 * a, 2 * a); c.hasPt = true; i64 d = g.rr(1, 9); c.pt = {a, a, 2 * a + d}; if (i % 2) c.pt = {2 * a + d, a, 2 * a + d}; emit(c); }
    // mesh-mesh
    for (int i = 0; i < 24; i++) { Case c = mk("dist_mm", "rand_separated"); IMesh a = xform(anySolid(g, (int)g.rr(0, 5), 8), g, i % 2, 6); IMesh b0 = xform(anySolid(g, (int)g.rr(0, 5), 8), g, i % 3 == 0, 6); i64 sx = g.rr(20, 60); i64 sy = g.rr(-30, 60); i64 sz = g.rr(-30, 60); IMesh b = shifted(b0, sx, sy, sz); c.mesh["A"] = a; c.mesh["B"] = b; emit(c); }
    for (int i = 0; i < 12; i++) { Case c = mk("dist_mm", "aligned_boxes"); i64 a = g.rr(1, 8), b = g.rr(1, 8), d = g.rr(1, 8); i64 gx = g.rr(0, 6), gy = (i % 3 == 0) ? 0 : g.rr(0, 6), gz = (i % 2) ? 0 : g.rr(0, 6); if (!gx && !gy && !gz) gx = 3;
        i64 w1 = g.rr(1, 5); i64 w2 = g.rr(1, 5); i64 w3 = g.rr(1, 5); c.mesh["A"] = boxM(0, 0, 0, a, b, d); c.mesh["B"] = boxM(a + gx, b / 2 + gy - 1, gz, a + gx + w1, b / 2 + gy + w2, gz + w3); emit(c); }
    for (int i = 0; i < 12; i++) { Case c = mk("dist_mm", i % 3 == 0 ? "touch_face" : i % 3 == 1 ? "touch_edge" : "touch_vertex"); i64 a = g.rr(1, 6); c.mesh["A"] = boxM(0, 0, 0, a, a, a);
        i64 r1 = g.rr(-2, 1); i64 r2 = g.rr(-2, 1); i64 r3 = g.rr(1, 4); i64 r4 = g.rr(1, 4); i64 r5 = g.rr(1, 4); i64 r6 = g.rr(-3, 1); i64 r7 = g.rr(0, 3);
        if (i % 3 == 0) c.mesh["B"] = boxM(a, r1, r2, a + r3, a + r4, a + r5); else if (i % 3 == 1) c.mesh["B"] = boxM(a, a, r6, a + r3, a + r4, a + r7 + 1); else c.mesh["B"] = boxM(a, a, a, a + r3, a + r4, a + r5); emit(c); }
    for (int i = 0; i < 8; i++) { Case c = mk("dist_mm", "overlap"); i64 a = g.rr(4, 8); c.mesh["A"] = boxM(0, 0, 0, a, a, a); c.mesh["B"] = xform(anySolid(g, (int)g.rr(0, 5), 5), g, true, 3); emit(c); }
    for (int i = 0; i < 4; i++) { Case c = mk("dist_mm", "nested"); i64 a = g.rr(10, 20); c.mesh["A"] = boxM(0, 0, 0, a, a, a); V3 q = rv(g, 1, 5); c.mesh["B"] = boxM(2, 2, 2, 2 + q[0], 2 + q[1], 2 + q[2]); emit(c); }
    for (int i = 0; i < 4; i++) { Case c = mk("dist_mm", "sliver", -30); c.mesh["A"] = boxM(0, 0, 0, 1LL << 30, 1LL << 30, 1); c.mesh["B"] = boxM(0, 0, 1 + (i % 2 ? 1 : (1LL << 30)), 1LL << 30, 1LL << 30, 3 + (i % 2 ? 1 : (1LL << 30))); emit(c); }
}
static void genMeasure(Rng& g) {
    for (int i = 0; i < 30; i++) { Case c = mk("measure", "random"); c.mesh["A"] = xform(anySolid(g, i, 12), g, i % 2, 15); c.hasPt = true; c.pt = rv(g, -40, 60); emit(c); }
    for (int i = 0; i < 12; i++) { Case c = mk("measure", "pythagorean_box"); i64 a = g.rr(1, 9), b = g.rr(1, 9), d = g.rr(1, 9); IMesh m = boxM(0, 0, 0, 3 * a, 4 * b, 12 * d); for (auto& v : m.v) v[0] += (i % 2) ? (v[1] * 3) : 0; c.cls = (i % 2) ? "pythagorean_shear" : "pythagorean_box"; c.mesh["A"] = m; c.hasPt = true; c.pt = rv(g, -30, 60); c.pt[2] = g.rr(-30, 160); emit(c); }
    for (int i = 0; i < 8; i++) { Case c = mk("measure", "far_offset"); IMesh m = shifted(xform(anySolid(g, i, 8), g, true, 5), 1LL << 30, -(1LL << 30), 1LL << 29); c.mesh["A"] = m; c.hasPt = true; V3 o = rv(g, -20, 40); c.pt = {(1LL << 30) + o[0], -(1LL << 30) + o[1], (1LL << 29) + o[2]}; emit(c); }
    for (int i = 0; i < 8; i++) { Case c = mk("measure", "sliver", -30); i64 w = (i % 2) ? (1LL << 30) : 1LL << 20; c.mesh["A"] = (i % 4 < 2) ? boxM(0, 0, 0, w, w, 1) : prismM(ccw(R2{{0, 0}, {w, 0}, {w, 1}}), w); c.hasPt = true; i64 px = g.rr(0, w); i64 py = g.rr(0, w); i64 pz = g.rr(2, 9); c.pt = {px, py, pz}; emit(c); }
    for (int i = 0; i < 6; i++) { Case c = mk("measure", "tie_closest"); i64 a = g.rr(1, 8); c.mesh["A"] = boxM(0, 0, 0, 2 * a, 2 * a, 2 * a); c.hasPt = true; c.pt = {a, a, 2 * a + g.rr(1, 9)}; if (i % 2) c.pt = {2 * a + 2, 2 * a + 2, 2 * a + 2}; emit(c); }
}
static void genNonConvex() {     // appended after all other streams (own RNG) so the earlier cases keep their bytes; A = union of boxes (extruded L / staircase / long L; every polygon is star-shaped from its first vertex, which the fan triangulation needs), B = box
    Rng g{0xA0C0DE5EED000001ULL};
    for (int i = 0; i < 24; i++) {
        Case c = mk("mink_nc", "union_of_boxes"); i64 k = g.rr(1, 4); i64 h = g.rr(1, 4); int shape = i % 3; R2 poly; std::vector<std::array<i64, 6>> boxes;
        if (shape == 0) { poly = {{0, 0}, {2 * k, 0}, {2 * k, k}, {k, k}, {k, 2 * k}, {0, 2 * k}}; boxes = {{0, 0, 0, 2 * k, k, h}, {0, k, 0, k, 2 * k, h}}; c.cls = "L_shape"; }
        else if (shape == 1) { poly = {{0, 0}, {3 * k, 0}, {3 * k, k}, {2 * k, k}, {2 * k, 2 * k}, {k, 2 * k}, {k, 3 * k}, {0, 3 * k}}; boxes = {{0, 0, 0, 3 * k, k, h}, {0, k, 0, 2 * k, 2 * k, h}, {0, 2 * k, 0, k, 3 * k, h}}; c.cls = "staircase"; }
        else { poly = {{0, 0}, {3 * k, 0}, {3 * k, k}, {k, k}, {k, 3 * k}, {0, 3 * k}}; boxes = {{0, 0, 0, 3 * k, k, h}, {0, k, 0, k, 3 * k, h}}; c.cls = "L_long"; }
        i64 w = g.rr(0, 2 * k + 1); i64 d = g.rr(1, 3); i64 hh = g.rr(1, 3); i64 ox = g.rr(-2, 2); i64 oy = g.rr(-2, 2); i64 oz = g.rr(-1, 1);
        if (i % 4 == 0) w = k;       // touching: the box exactly closes the notch
        if (w < 1) w = 1;
        IMesh a = prismM(ccw(poly), h); c.mesh["A"] = a; c.mesh["B"] = boxM(ox, oy, oz, ox + w, oy + d, oz + hh);
        std::vector<i64> bx; for (auto& b : boxes) for (int j = 0; j < 6; j++) bx.push_back(b[j]); c.param["boxes"] = bx; emit(c);
    }
}
static void genRevolveExtra() {    // appended after every other stream (own RNG): the declared revolve degenerate classes that the first revolve set lacked (2^-30 sliver, zero-area profile)
    Rng g{0xBEEF0000A11CE005ULL};
    for (int i = 0; i < 6; i++) { Case c = mk("revolve", i % 2 ? "sliver_ring" : "sliver_axis_needle", -30); Prof p; if (i % 2) p.rings.push_back(rect(1LL << 30, 0, (1LL << 30) + 1, 1LL << 30)); else p.rings.push_back(rect(0, 0, 1, 1LL << 30)); addProf2(c, p);
        if (i < 4) { c.param["seg"] = {4}; c.param["deg"] = {360}; } else { c.param["seg"] = {3}; c.param["deg"] = {270}; } emit(c); }
    for (int i = 0; i < 4; i++) { Case c = mk("revolve", "collinear_zero_area"); Ring r; r.dim = 2; i64 k = g.rr(1, 5); int n = (int)g.rr(3, 6); for (int j = 0; j < n; j++) r.p.push_back({i == 3 ? 0 : (j + 1) * k, j * k, 0}); c.rings.push_back(r); c.param["holes"] = {0}; c.param["seg"] = {4}; c.param["deg"] = {360}; emit(c); }
}
static void genSpeed(const std::string& dir) {   // 6-face grid cube, N x N cells per face, integer normal noise on interior grid points (z of the +z/-z faces etc.)
    int N = 91; Rng g{0x5EEDFACE3D3D3D3DULL}; mesh::Mesh m; i64 S = 1000;
    std::map<std::array<i64, 3>, uint32_t> idx;
    auto vid = [&](i64 x, i64 y, i64 z) { std::array<i64, 3> k = {x, y, z}; auto it = idx.find(k); if (it != idx.end()) return it->second; uint32_t id = m.addV((double)x, (double)y, (double)z); idx[k] = id; return id; };
    for (int face = 0; face < 6; face++) {
        int ax = face / 2, side = face % 2; int u = (ax + 1) % 3, w = (ax + 2) % 3;
        auto pt = [&](int i, int j) { i64 c[3]; c[u] = i * S / N * 1; c[w] = j * S / N; c[ax] = side ? S : 0;
            if (i > 0 && i < N && j > 0 && j < N) { i64 nz = g.rr(-3, 3); c[ax] += side ? nz : nz; } return vid(c[0], c[1], c[2]); };
        std::vector<uint32_t> grid((size_t)(N + 1) * (N + 1)); for (int i = 0; i <= N; i++) for (int j = 0; j <= N; j++) grid[(size_t)i * (N + 1) + j] = pt(i, j);
        for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) { uint32_t a = grid[(size_t)i * (N + 1) + j], b = grid[(size_t)(i + 1) * (N + 1) + j], c2 = grid[(size_t)(i + 1) * (N + 1) + j + 1], d = grid[(size_t)i * (N + 1) + j + 1];
            // (u,w,ax) cyclic: ccw in (u,w) has normal +ax; outward for side=1, flip for side=0
            if (side) { m.addT(a, b, c2); m.addT(a, c2, d); } else { m.addT(a, c2, b); m.addT(a, d, c2); } }
    }
    (void)mesh::writeMesh(dir + "/speed_cube_1e5.msh", m);
    // second mesh: same shape shifted by (500, 500, 1100) so that it is separated by a gap of 100 along z
    mesh::Mesh m2 = m; for (size_t i = 0; i < m2.nv(); i++) { m2.v[3*i] += 300; m2.v[3*i+1] += 300; m2.v[3*i+2] += 1100; } (void)mesh::writeMesh(dir + "/speed_cube_1e5_B.msh", m2);
}
int main(int argc, char** argv) {
    if (argc < 3) return 2; outDir = argv[1]; mkdir(argv[1], 0755); mkdir(argv[2], 0755);
    Rng g{0x3D3D1F00C0FFEE07ULL};
    genExtrude(g); genRevolve(g); genTess(g); genMink(g); genDist(g); genMeasure(g); genNonConvex(); genRevolveExtra(); genSpeed(argv[2]);
    fprintf(stderr, "cases: %d\n", counter); return 0;
}
