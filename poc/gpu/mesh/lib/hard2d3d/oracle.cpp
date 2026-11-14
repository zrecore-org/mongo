// MP08b exact oracle. Independent of every library under test (Clipper2, earcut, CDT, polypartition, Geogram): only Boost.Multiprecision
// (cpp_int / cpp_rational, exact) and brute-force definitions. usage: oracle <case.h2d> <result.res>   prints one line: VERDICT detail
//   hm   : parts valid (convex, simple, CCW, vertices from the polygon) + exact boundary-chain identity (sum of part boundaries == polygon boundary after
//          splitting every edge at every vertex on it) + HM-minimal (no two adjacent parts can merge into a convex part) + count <= 2r+1-h (own derivation).
//   mono : same, with y-monotone instead of convex, no minimality/count test.
//   vis  : exact classification of sample points (strictly visible / strictly invisible by definition, ambiguous touching samples skipped) against the
//          rational result polygon.
//   alpha: full triangulation tiles the hull exactly (chain identity) with empty circumcircles (exact in-circle), selected set == {R^2 <= alpha}, boundary edges
//          == edges used by exactly one selected triangle.
//   repair: result rings valid (no crossing / overlap) and exact symmetric-difference area vs the even-odd / non-zero region of the input (slab sweep).
#include "hard2d.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
using namespace h2d; namespace mp = boost::multiprecision;
typedef mp::cpp_int Z; typedef mp::cpp_rational Q;
typedef mp::number<mp::cpp_int_backend<640, 640, mp::signed_magnitude, mp::unchecked, void>> W;  // fixed-width exact (no allocation), sized for rational orient tests
static int sg(i128 v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static int orientI(P a, P b, P c) { return sg((i128)(b.x - a.x) * (c.y - a.y) - (i128)(b.y - a.y) * (c.x - a.x)); }
static bool onSegI(P a, P b, P c) { return orientI(a, b, c) == 0 && std::min(a.x, b.x) <= c.x && c.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= c.y && c.y <= std::max(a.y, b.y); }
static i128 area2(const Ring& r) { i128 a = 0; for (size_t i = 0; i < r.size(); i++) { P p = r[i], q = r[(i + 1) % r.size()]; a += (i128)p.x * q.y - (i128)q.x * p.y; } return a; }
static int fail(const std::string& m) { printf("WRONG %s\n", m.c_str()); return 0; }
typedef std::pair<i64, i64> PK; static PK key(P p) { return {p.x, p.y}; }
// net directed elementary-segment chain of a set of rings over vertex set V (edges split at every vertex of V lying on them)
static std::map<std::pair<int, int>, int> chainOf(const std::vector<Ring>& rings, const std::map<PK, int>& id, const std::vector<P>& V) {
    std::map<std::pair<int, int>, int> net;
    for (auto& r : rings) for (size_t i = 0; i < r.size(); i++) { P a = r[i], b = r[(i + 1) % r.size()]; if (a.x == b.x && a.y == b.y) continue;
        std::vector<std::pair<i128, P>> on; for (auto& v : V) if (onSegI(a, b, v)) on.push_back({(i128)(v.x - a.x) * (b.x - a.x) + (i128)(v.y - a.y) * (b.y - a.y), v});
        std::sort(on.begin(), on.end(), [](auto& x, auto& y) { return x.first < y.first; });
        for (size_t k = 0; k + 1 < on.size(); k++) { int u = id.at(key(on[k].second)), w = id.at(key(on[k + 1].second)); if (u < w) net[{u, w}]++; else net[{w, u}]--; } }
    for (auto it = net.begin(); it != net.end();) it = it->second == 0 ? net.erase(it) : std::next(it);
    return net; }
static int quad(P d) { return d.x > 0 && d.y >= 0 ? 0 : d.x <= 0 && d.y > 0 ? 1 : d.x < 0 && d.y <= 0 ? 2 : 3; }
// ring is a simple weakly convex CCW polygon without repeated vertices or spikes: 1 yes
static bool simpleConvex(const Ring& r) { size_t n = r.size(); if (n < 3 || area2(r) <= 0) return false; std::set<PK> seen; for (auto& p : r) if (!seen.insert(key(p)).second) return false;
    int tot = 0; for (size_t i = 0; i < n; i++) { P a = r[i], b = r[(i + 1) % n], c = r[(i + 2) % n]; P d1{b.x - a.x, b.y - a.y}, d2{c.x - b.x, c.y - b.y}; i128 cr = (i128)d1.x * d2.y - (i128)d1.y * d2.x; i128 dt = (i128)d1.x * d2.x + (i128)d1.y * d2.y;
        if (cr < 0) return false; if (cr == 0 && dt < 0) return false; tot += ((quad(d2) - quad(d1)) % 4 + 4) % 4; } return tot == 4; }
static bool segCross(P a, P b, P c, P d, bool& overlap) { overlap = false; int o1 = orientI(a, b, c), o2 = orientI(a, b, d), o3 = orientI(c, d, a), o4 = orientI(c, d, b);
    if (o1 * o2 < 0 && o3 * o4 < 0) return true;
    if (o1 == 0 && o2 == 0) { // collinear: overlap of positive length?
        i128 t0 = (i128)(c.x - a.x) * (b.x - a.x) + (i128)(c.y - a.y) * (b.y - a.y), t1 = (i128)(d.x - a.x) * (b.x - a.x) + (i128)(d.y - a.y) * (b.y - a.y), L = (i128)(b.x - a.x) * (b.x - a.x) + (i128)(b.y - a.y) * (b.y - a.y);
        i128 lo = std::min(t0, t1), hi = std::max(t0, t1); if (std::max<i128>(lo, 0) < std::min<i128>(hi, L)) { overlap = true; return true; } }
    return false; }
static bool simpleAny(const Ring& r, bool allowTouch = false) { size_t n = r.size(); if (n < 3) return false; for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) { bool ov; P a = r[i], b = r[(i + 1) % n], c = r[j], d = r[(j + 1) % n];
        bool adj = (j == i + 1) || (i == 0 && j == n - 1); if (adj) { P sh = (j == i + 1) ? b : a; P o1 = (j == i + 1) ? a : b, o2 = (j == i + 1) ? d : c; (void)sh; if (onSegI(o1, sh, o2) && !(o2.x == sh.x && o2.y == sh.y)) return false; if (onSegI(sh, o2, o1)) return false; continue; }
        if (segCross(a, b, c, d, ov)) return false; if (!allowTouch && (onSegI(a, b, c) || onSegI(a, b, d) || onSegI(c, d, a) || onSegI(c, d, b))) return false; } return true; }
static bool hasCollinear(const Ring& r) { size_t n = r.size(); for (size_t i = 0; i < n; i++) if (orientI(r[i], r[(i + 1) % n], r[(i + 2) % n]) == 0) return true; return false; }
// ---------------------------------------------------------------- hm / mono
static bool yMono(const Ring& r) { size_t n = r.size(); auto less = [&](P a, P b) { return a.y < b.y || (a.y == b.y && a.x < b.x); }; int ext = 0;
    for (size_t i = 0; i < n; i++) { P p = r[(i + n - 1) % n], c = r[i], q = r[(i + 1) % n]; bool a = less(p, c), b = less(c, q); if (a != b) ext++; } return ext == 2; }
static int doPart(const Case& c, const Res& R, bool convexMode) {
    std::vector<P> V; std::map<PK, int> id; for (auto& r : c.rings) for (auto& p : r) if (!id.count(key(p))) { id[key(p)] = (int)V.size(); V.push_back(p); }
    for (auto& pr : R.rings) for (auto& p : pr) if (!id.count(key(p))) return fail("part vertex not a polygon vertex");
    if (R.rings.empty()) return fail("no parts");
    for (size_t k = 0; k < R.rings.size(); k++) { const Ring& pr = R.rings[k]; if (convexMode ? !simpleConvex(pr) : !(area2(pr) > 0 && simpleAny(pr, true) && yMono(pr))) return fail(std::string(convexMode ? "part not simple convex CCW #" : "part not simple y-monotone CCW #") + std::to_string(k)); }
    auto a = chainOf(c.rings, id, V), b = chainOf(R.rings, id, V); if (a != b) return fail("boundary chain of parts != polygon boundary (gap/overlap/outside)");
    i128 pa = 0, sa = 0; for (auto& r : c.rings) pa += area2(r); for (auto& r : R.rings) sa += area2(r); if (pa != sa) return fail("area mismatch");
    if (!convexMode) { printf("CORRECT parts=%zu\n", R.rings.size()); return 1; }
    // HM-minimal: for parts sharing a directed edge a->b (in P) and b->a (in Q)
    size_t np = R.rings.size(); std::map<std::pair<int, int>, std::pair<int, int>> dir; // directed edge -> (part, index)
    for (size_t k = 0; k < np; k++) for (size_t i = 0; i < R.rings[k].size(); i++) dir[{id[key(R.rings[k][i])], id[key(R.rings[k][(i + 1) % R.rings[k].size()])]}] = {(int)k, (int)i};
    for (auto& e : dir) { auto o = dir.find({e.first.second, e.first.first}); if (o == dir.end() || e.first.first > e.first.second) continue; const Ring& Pp = R.rings[e.second.first]; const Ring& Qq = R.rings[o->second.first]; if (e.second.first == o->second.first) continue;
        size_t ip = e.second.second, iq = o->second.second; Ring m; // P: ... a b ...  (edge ip: a->b)   Q: ... b a ... (edge iq: b->a)
        size_t np2 = Pp.size(), nq = Qq.size(); for (size_t t = 1; t <= np2; t++) m.push_back(Pp[(ip + t) % np2]);        // b ... a
        for (size_t t = 2; t < nq; t++) m.push_back(Qq[(iq + t) % nq]);                                                    // after a, up to before b
        if (simpleConvex(m) && !(getenv("HM_STRICT") && hasCollinear(m))) return fail("not HM-minimal: two adjacent parts can merge into a convex part"); }
    int r = 0; for (size_t ri = 0; ri < c.rings.size(); ri++) { const Ring& q = c.rings[ri]; size_t n = q.size(); for (size_t i = 0; i < n; i++) if (orientI(q[(i + n - 1) % n], q[i], q[(i + 1) % n]) < 0) r++; }
    int h = (int)c.rings.size() - 1; long bound = 2L * r + 1 - h; if ((long)np > bound) return fail("part count " + std::to_string(np) + " > 2r+1-h = " + std::to_string(bound));
    printf("CORRECT parts=%zu r=%d h=%d lb=%ld\n", np, r, h, std::max<long>(1, (r + 1) / 2 + 1 - h)); return 1; }
// ---------------------------------------------------------------- visibility
struct F { W n, d; };  // fraction, d > 0
static F fmk(i64 n, i64 d) { F f; f.n = n; f.d = d; if (f.d < 0) { f.n = -f.n; f.d = -f.d; } return f; }
static F fsub(const F& a, const F& b) { return {a.n * b.d - b.n * a.d, a.d * b.d}; }
static F fmul(const F& a, const F& b) { return {a.n * b.n, a.d * b.d}; }
static int fsgn(const F& a) { return a.n > 0 ? 1 : a.n < 0 ? -1 : 0; }
static int fcmp(const F& a, const F& b) { W l = a.n * b.d, r = b.n * a.d; return l < r ? -1 : l > r ? 1 : 0; }
struct FP { F x, y; };
static int forient(const FP& a, const FP& b, const FP& q) { F t1 = fmul(fsub(b.x, a.x), fsub(q.y, a.y)), t2 = fmul(fsub(b.y, a.y), fsub(q.x, a.x)); return fsgn(fsub(t1, t2)); }
// 1 strictly inside, 0 strictly outside, -1 boundary
static int fpip(const std::vector<FP>& poly, const FP& q) { int w = 0; size_t n = poly.size();
    for (size_t i = 0; i < n; i++) { const FP &a = poly[i], &b = poly[(i + 1) % n]; int o = forient(a, b, q);
        if (o == 0) { bool inx = (fcmp(a.x, q.x) <= 0 && fcmp(q.x, b.x) <= 0) || (fcmp(b.x, q.x) <= 0 && fcmp(q.x, a.x) <= 0), iny = (fcmp(a.y, q.y) <= 0 && fcmp(q.y, b.y) <= 0) || (fcmp(b.y, q.y) <= 0 && fcmp(q.y, a.y) <= 0); if (inx && iny) return -1; }
        int ya = fcmp(a.y, q.y), yb = fcmp(b.y, q.y); if (ya <= 0 && yb > 0 && o > 0) w++; else if (ya > 0 && yb <= 0 && o < 0) w--; }
    return w != 0 ? 1 : 0; }
struct Rng { uint64_t s; uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ULL); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); } i64 rr(i64 lo, i64 hi) { uint64_t r = next(); return lo + (i64)(r % (uint64_t)(hi - lo + 1)); } };
static int doVis(const Case& c, const Res& R) {
    if (R.rrat.size() != 1) return fail("expected one result polygon"); const int S = 8;
    std::vector<FP> res; for (auto& v : R.rrat[0]) { if (v.xd <= 0 || v.yd <= 0) return fail("bad denominator"); res.push_back({fmk(v.xn * S, v.xd), fmk(v.yn * S, v.yd)}); }
    if (res.size() < 3) return fail("result has < 3 vertices");
    std::vector<Ring> sc = c.rings; for (auto& r : sc) for (auto& p : r) { p.x *= S; p.y *= S; } P p8{c.pt.x * S, c.pt.y * S};
    i64 xmin = 1 << 30, xmax = -(1 << 30), ymin = xmin, ymax = xmax; for (auto& p : sc[0]) { xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x); ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y); }
    std::vector<P> samples; Rng g{0x56495331ULL}; for (int i = 0; i < 3000; i++) { i64 x = g.rr(xmin - 8, xmax + 8); i64 y = g.rr(ymin - 8, ymax + 8); samples.push_back({x, y}); }
    for (auto& r : sc) for (auto& v : r) for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) samples.push_back({v.x + dx, v.y + dy});
    for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) samples.push_back({p8.x + dx, p8.y + dy});
    int nv = 0, ni = 0, amb = 0;
    for (P q : samples) { // exact definitional classification
        int cls = 1; // 1 strictly visible, 0 strictly invisible, -1 ambiguous
        bool touch = false, cross = false; for (auto& r : sc) for (size_t i = 0; i < r.size(); i++) { P a = r[i], b = r[(i + 1) % r.size()]; int o1 = orientI(p8, q, a), o2 = orientI(p8, q, b), o3 = orientI(a, b, p8), o4 = orientI(a, b, q);
            if (o1 * o2 < 0 && o3 * o4 < 0) cross = true; else if (onSegI(p8, q, a) || onSegI(p8, q, b) || onSegI(a, b, p8) || onSegI(a, b, q) || (o1 * o2 < 0 && o3 * o4 <= 0 && (o3 == 0 || o4 == 0))) touch = true; }
        if (cross) cls = 0; else if (touch) cls = -1;
        if (q.x == p8.x && q.y == p8.y) cls = -1; if (cls < 0) { amb++; continue; }
        FP fq{fmk(q.x, 1), fmk(q.y, 1)}; int in = fpip(res, fq);
        if (cls == 1) { nv++; if (in != 1) return fail("strictly visible sample (" + std::to_string(q.x) + "," + std::to_string(q.y) + ")/8 not strictly inside the result"); }
        else { ni++; if (in != 0) return fail("strictly invisible sample (" + std::to_string(q.x) + "," + std::to_string(q.y) + ")/8 inside or on the result"); } }
    printf("CORRECT visible=%d invisible=%d skipped=%d\n", nv, ni, amb); return 1; }
// ---------------------------------------------------------------- alpha
static int doAlpha(const Case& c, const Res& R) {
    const auto& pts = c.points; size_t n = pts.size(); std::vector<P> V = pts; std::map<PK, int> id; for (size_t i = 0; i < n; i++) id[key(pts[i])] = (int)i;
    auto okIdx = [&](const std::vector<int>& t) { for (int v : t) if (v < 0 || (size_t)v >= n) return false; return true; };
    if (R.ftri.empty()) return fail("empty triangulation");
    std::vector<Ring> tris; std::set<int> used;
    for (auto& t : R.ftri) { if (!okIdx(t)) return fail("bad index"); P a = pts[t[0]], b = pts[t[1]], d = pts[t[2]]; if (orientI(a, b, d) <= 0) return fail("triangle not CCW/non-degenerate"); tris.push_back({a, b, d}); for (int v : t) used.insert(v);
        // empty circumcircle: no point strictly inside (exact, cpp_int)
        for (size_t k = 0; k < n; k++) { if ((int)k == t[0] || (int)k == t[1] || (int)k == t[2]) continue; P q = pts[k]; Z m[3][3]; P tv[3] = {a, b, d};
            for (int i = 0; i < 3; i++) { Z dx = tv[i].x - q.x, dy = tv[i].y - q.y; m[i][0] = dx; m[i][1] = dy; m[i][2] = dx * dx + dy * dy; }
            Z det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]); if (det > 0) return fail("point strictly inside a circumcircle (not Delaunay)"); } }
    if (used.size() != n) return fail("a point is not a triangulation vertex");
    // hull (monotone chain, strict) and chain identity
    std::vector<P> s = pts; std::sort(s.begin(), s.end(), [](P a, P b) { return a.x < b.x || (a.x == b.x && a.y < b.y); }); std::vector<P> H;
    for (int pass = 0; pass < 2; pass++) { size_t base = H.size(); for (size_t i = 0; i < n; i++) { P p = s[pass ? n - 1 - i : i]; while (H.size() >= base + 2 && orientI(H[H.size() - 2], H.back(), p) <= 0) H.pop_back(); H.push_back(p); } H.pop_back(); }
    if (chainOf({H}, id, V) != chainOf(tris, id, V)) return fail("triangulation does not tile the convex hull exactly");
    // alpha selection by exact squared circumradius via circumcentre (independent formula)
    Q alpha = Q(Z(c.an), Z(c.ad)); std::set<std::vector<int>> want, got; std::map<std::pair<int, int>, int> ecount;
    for (auto& t : R.ftri) { P a = pts[t[0]], b = pts[t[1]], d = pts[t[2]]; Q bx = b.x - a.x, by = b.y - a.y, cx = d.x - a.x, cy = d.y - a.y; Q D = 2 * (bx * cy - by * cx);
        Q ux = (cy * (bx * bx + by * by) - by * (cx * cx + cy * cy)) / D, uy = (bx * (cx * cx + cy * cy) - cx * (bx * bx + by * by)) / D; Q r2 = ux * ux + uy * uy;
        if (r2 <= alpha) { std::vector<int> k = t; std::sort(k.begin(), k.end()); want.insert(k); for (int i = 0; i < 3; i++) { int u = t[i], v = t[(i + 1) % 3]; ecount[{std::min(u, v), std::max(u, v)}]++; } } }
    for (auto& t : R.btri) { if (!okIdx(t)) return fail("bad index in selection"); std::vector<int> k = t; std::sort(k.begin(), k.end()); got.insert(k); }
    if (want != got) return fail("selected triangle set != {R^2 <= alpha} (want " + std::to_string(want.size()) + " got " + std::to_string(got.size()) + ")");
    std::set<std::pair<int, int>> be, gb; for (auto& e : ecount) if (e.second == 1) be.insert(e.first); for (auto& e : R.bedge) gb.insert({std::min(e.first, e.second), std::max(e.first, e.second)});
    if (be != gb) return fail("boundary edge set mismatch"); printf("CORRECT tris=%zu selected=%zu boundary=%zu\n", R.ftri.size(), want.size(), be.size()); return 1; }
// ---------------------------------------------------------------- repair
struct Seg { P a, b; int src; int sgn; };  // a.x < b.x
static Q yAt(const Seg& s, const Q& x) { return Q(s.a.y) + Q(s.b.y - s.a.y) * (x - Q(s.a.x)) / Q(s.b.x - s.a.x); }
static int doRepair(const Case& c, const Res& R) {
    // validity of the result rings
    std::vector<std::pair<P, P>> edges; std::vector<int> ering; int touches = 0;
    for (size_t k = 0; k < R.rings.size(); k++) { const Ring& r = R.rings[k]; if (r.size() < 3 || area2(r) == 0) return fail("degenerate result ring"); for (size_t i = 0; i < r.size(); i++) { if (r[i].x == r[(i + 1) % r.size()].x && r[i].y == r[(i + 1) % r.size()].y) return fail("repeated vertex"); edges.push_back({r[i], r[(i + 1) % r.size()]}); ering.push_back((int)k); } }
    for (size_t i = 0; i < edges.size(); i++) for (size_t j = i + 1; j < edges.size(); j++) { bool ov; auto &e = edges[i], &f = edges[j]; bool adj = ering[i] == ering[j] && ((j == i + 1) || (ering[i] == ering[j] && i + 1 < edges.size() && false));
        bool shareEnd = (e.second.x == f.first.x && e.second.y == f.first.y) || (f.second.x == e.first.x && f.second.y == e.first.y) || (e.first.x == f.first.x && e.first.y == f.first.y) || (e.second.x == f.second.x && e.second.y == f.second.y); (void)adj;
        if (segCross(e.first, e.second, f.first, f.second, ov)) return fail(ov ? "result edges overlap" : "result edges cross");
        if (!shareEnd && (onSegI(e.first, e.second, f.first) || onSegI(e.first, e.second, f.second) || onSegI(f.first, f.second, e.first) || onSegI(f.first, f.second, e.second))) touches++; }
    // slab sweep
    std::vector<Seg> segs; auto add = [&](P a, P b, int src) { if (a.x == b.x) return; if (a.x < b.x) segs.push_back({a, b, src, 1}); else segs.push_back({b, a, src, -1}); };
    for (auto& r : c.rings) for (size_t i = 0; i < r.size(); i++) add(r[i], r[(i + 1) % r.size()], 0);
    for (auto& r : R.rings) for (size_t i = 0; i < r.size(); i++) add(r[i], r[(i + 1) % r.size()], 1);
    std::set<Q> xs; for (auto& s : segs) { xs.insert(Q(s.a.x)); xs.insert(Q(s.b.x)); }
    for (size_t i = 0; i < segs.size(); i++) for (size_t j = i + 1; j < segs.size(); j++) { const Seg &s = segs[i], &t = segs[j]; Q ms = Q(s.b.y - s.a.y) / Q(s.b.x - s.a.x), mt = Q(t.b.y - t.a.y) / Q(t.b.x - t.a.x); if (ms == mt) continue;
        Q x = (Q(t.a.y) - Q(s.a.y) + ms * Q(s.a.x) - mt * Q(t.a.x)) / (ms - mt); if (x < Q(std::max(s.a.x, t.a.x)) || x > Q(std::min(s.b.x, t.b.x))) continue; xs.insert(x); }
    std::vector<Q> X(xs.begin(), xs.end()); Q areaA = 0, areaSym = 0, areaR = 0;
    for (size_t k = 0; k + 1 < X.size(); k++) { Q xl = X[k], xr = X[k + 1], xm = (xl + xr) / 2; std::vector<std::pair<Q, const Seg*>> act;
        for (auto& s : segs) if (Q(s.a.x) <= xl && Q(s.b.x) >= xr) act.push_back({yAt(s, xm), &s});
        std::stable_sort(act.begin(), act.end(), [](auto& a, auto& b) { return a.first < b.first; });
        int wA = 0, parR = 0; for (size_t i = 0; i + 1 < act.size(); i++) { const Seg* s = act[i].second; if (s->src == 0) wA += s->sgn; else parR ^= 1; const Seg* t = act[i + 1].second;
            Q h = (yAt(*t, xl) - yAt(*s, xl) + yAt(*t, xr) - yAt(*s, xr)) / 2 * (xr - xl); bool inA = c.rule == 0 ? (wA & 1) != 0 : wA != 0, inR = parR != 0;
            if (inA) areaA += h; if (inR) areaR += h; if (inA != inR) areaSym += h; } }
    double len = 0; for (auto& e : edges) len += std::hypot((double)(e.second.x - e.first.x), (double)(e.second.y - e.first.y));
    std::string info = "touches=" + std::to_string(touches) + " areaA=" + std::to_string(areaA.convert_to<double>()) + " areaR=" + std::to_string(areaR.convert_to<double>()) + " sym=" + std::to_string(areaSym.convert_to<double>());
    if (areaSym == 0) { printf("CORRECT_EXACT %s\n", info.c_str()); return 1; }
    if (c.family == "orth" || c.family == "orth2") return fail("integer-intersection family must be exact: " + info);
    double sym = areaSym.convert_to<double>(); double bound = 2 * 0.7072 * len;
    double rel = sym / std::max(1.0, areaA.convert_to<double>()); if (sym <= bound || rel <= 1e-3) { printf("ROUNDED_OK rel=%.2e %s (bound %.0f)\n", rel, info.c_str(), bound); return 1; }
    return fail("symmetric difference exceeds snapping bound: " + info); }
int main(int argc, char** argv) { if (argc < 3) return 2; Case c; Res R; if (!readCase(argv[1], c)) { puts("WRONG cannot read case"); return 3; } if (!readRes(argv[2], R)) { puts("WRONG cannot read result"); return 3; }
    if (R.status != "OK") { printf("REFUSED %s\n", R.status.c_str()); return 0; }
    if (c.mode == "hm") doPart(c, R, true); else if (c.mode == "mono") doPart(c, R, false); else if (c.mode == "vis") doVis(c, R); else if (c.mode == "alpha") doAlpha(c, R); else if (c.mode == "repair") doRepair(c, R); else puts("WRONG unknown mode");
    return 0; }
