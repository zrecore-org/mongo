// MP08a exact 2D checker. Everything is exact (Boost.Multiprecision cpp_int / cpp_rational through harness/exact.h), no floating-point tolerance is
// used inside the oracle or the validity checks; tolerances are applied afterwards to exactly computed areas.
//   Oracle:   exact area (and symmetric-difference area) of any boolean combination of polygon sets, by a vertical-slab sweep:
//             breakpoints = all vertex x and all pairwise edge-intersection x (rationals); inside a slab no two edges cross, so the edges are
//             totally ordered by y at the slab midpoint; winding numbers per set accumulate bottom to top; each "inside" gap contributes an
//             exact trapezoid. Inputs may be integers or doubles (dyadic rationals).
//   Validity: exact orient2d on doubles (exactOrient2d): ring size, duplicate vertices, zero-area ring, proper crossings, collinear overlaps,
//             spikes, repeated vertex in a ring (non-simple), containment depth parity, orientation (outer CCW / hole CW).
// Modes: selftest | validate-corpus <dir> | eval <case.p2d> <op> <result.res> (op: union intersection difference offset hull triangulate area intersects isvalid)
// -DMUTANT=n compiles a planted bug (1 area sign, 2 skipped hole, 3 union predicate, 4 collinear-overlap check removed, 5 crossing check removed, 6 edge direction ignored).
#include "../../harness/exact.h"
#include "common2d.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <dirent.h>
#ifndef MUTANT
#define MUTANT 0
#endif
using namespace mesh;
using boost::multiprecision::cpp_rational;
typedef std::pair<double, double> DP;
typedef std::vector<DP> DRing;
typedef std::vector<DRing> DRings;

static cpp_rational toRat(double v) {
    if (v == 0) return 0;
    int e; double f = std::frexp(v, &e); int64_t m = (int64_t)std::ldexp(f, 53); int e2 = e - 53;
    BigInt num = m, den = 1; if (e2 >= 0) num <<= e2; else den <<= -e2;
    return cpp_rational(num, den);
}
static double toDouble(const cpp_rational& r) { return r.convert_to<double>(); }

static DRings fromSet(const p2d::Set& s) { DRings r; for (auto& poly : s) for (auto& ring : poly) { DRing d; for (auto& p : ring) d.push_back({(double)p.x, (double)p.y}); r.push_back(d); } return r; }

// ---------------------------------------------------------------- oracle
struct Edge { BigInt x0, y0, x1, y1; int set, dir; cpp_rational m, rx0, rx1, ry0; };
typedef std::function<bool(const bool*)> Pred;

// Exact area (original units) of { points where pred(winding_i > 0 for each set) }.
static cpp_rational oracleArea(const std::vector<DRings>& sets, const Pred& pred) {
    // common scale: integer = value * 2^k
    int emin = INT32_MAX;
    auto expOf = [&](double v, int64_t& mant) { int e; double f = std::frexp(v, &e); mant = (int64_t)std::ldexp(f, 53); int e2 = e - 53; if (mant == 0) return 0; while ((mant & 1) == 0) { mant /= 2; e2++; } return e2; };
    for (auto& s : sets) for (auto& r : s) for (auto& p : r) { int64_t m; if (p.first != 0) emin = std::min(emin, expOf(p.first, m)); if (p.second != 0) emin = std::min(emin, expOf(p.second, m)); }
    if (emin == INT32_MAX || emin > 0) emin = 0;
    auto toI = [&](double v) { int64_t m; int e = expOf(v, m); BigInt b = m; if (m != 0) b <<= (e - emin); return b; };
    std::vector<Edge> E;
    for (size_t si = 0; si < sets.size(); si++) for (auto& r : sets[si]) for (size_t i = 0; i < r.size(); i++) {
        const DP &a = r[i], &b = r[(i + 1) % r.size()];
        if (a.first == b.first) continue;   // vertical or degenerate: no winding contribution
        Edge e; bool right = a.first < b.first; const DP& l = right ? a : b; const DP& rr = right ? b : a;
        e.x0 = toI(l.first); e.y0 = toI(l.second); e.x1 = toI(rr.first); e.y1 = toI(rr.second); e.set = (int)si; e.dir = right ? 1 : -1;
#if MUTANT == 6
        e.dir = 1;
#endif
        e.m = cpp_rational(e.y1 - e.y0, e.x1 - e.x0); e.rx0 = cpp_rational(e.x0); e.rx1 = cpp_rational(e.x1); e.ry0 = cpp_rational(e.y0); E.push_back(e);
    }
    std::vector<cpp_rational> xs;
    for (auto& e : E) { xs.push_back(cpp_rational(e.x0)); xs.push_back(cpp_rational(e.x1)); }
    for (size_t i = 0; i < E.size(); i++) for (size_t j = i + 1; j < E.size(); j++) {
        const Edge &e = E[i], &f = E[j];
        if (e.x1 < f.x0 || f.x1 < e.x0) continue;
        BigInt eylo = std::min(e.y0, e.y1), eyhi = std::max(e.y0, e.y1), fylo = std::min(f.y0, f.y1), fyhi = std::max(f.y0, f.y1);
        if (eyhi < fylo || fyhi < eylo) continue;
        BigInt rx = e.x1 - e.x0, ry = e.y1 - e.y0, sx = f.x1 - f.x0, sy = f.y1 - f.y0;
        BigInt den = rx * sy - ry * sx; if (den == 0) continue;
        BigInt qx = f.x0 - e.x0, qy = f.y0 - e.y0;
        BigInt tn = qx * sy - qy * sx, un = qx * ry - qy * rx;
        if (den < 0) { den = -den; tn = -tn; un = -un; }
        if (tn < 0 || tn > den || un < 0 || un > den) continue;
        xs.push_back(cpp_rational(e.x0) + cpp_rational(tn * rx, den));
    }
    std::sort(xs.begin(), xs.end()); xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    cpp_rational total = 0; std::vector<int> act; std::vector<cpp_rational> ym;
    std::vector<bool> inside(sets.size());
    bool w[16];
    for (size_t k = 0; k + 1 < xs.size(); k++) {
        const cpp_rational &xa = xs[k], &xb = xs[k + 1]; cpp_rational mid = (xa + xb) / 2;
        act.clear(); ym.clear();
        for (size_t i = 0; i < E.size(); i++) if (E[i].rx0 <= xa && E[i].rx1 >= xb) act.push_back((int)i);
        if (act.size() < 2) continue;
        std::vector<std::pair<cpp_rational, int>> ord;
        for (int i : act) ord.push_back({E[(size_t)i].ry0 + E[(size_t)i].m * (mid - E[(size_t)i].rx0), i});
        std::stable_sort(ord.begin(), ord.end(), [](const std::pair<cpp_rational, int>& a, const std::pair<cpp_rational, int>& b) { return a.first < b.first; });
        std::vector<int> wind(sets.size(), 0);
        for (size_t t = 0; t + 1 < ord.size(); t++) {
            const Edge& lo = E[(size_t)ord[t].second]; wind[(size_t)lo.set] += lo.dir;
            for (size_t s = 0; s < sets.size(); s++) w[s] = wind[s] > 0;
            if (!pred(w)) continue;
            const Edge& hi = E[(size_t)ord[t + 1].second];
            auto yat = [&](const Edge& e, const cpp_rational& x) { return e.ry0 + e.m * (x - e.rx0); };
            cpp_rational h = (yat(hi, xa) + yat(hi, xb)) - (yat(lo, xa) + yat(lo, xb));
            total += (xb - xa) * h / 2;
        }
    }
    cpp_rational scale = 1; for (int i = 0; i < -2 * emin; i++) scale *= 2;
    return total / scale;
}

// ---------------------------------------------------------------- exact helpers on double rings
static int orient(const DP& a, const DP& b, const DP& c) { double A[2] = {a.first, a.second}, B[2] = {b.first, b.second}, C[2] = {c.first, c.second}; return exactOrient2d(A, B, C); }
static bool onSeg(const DP& a, const DP& b, const DP& p) { if (orient(a, b, p) != 0) return false; return std::min(a.first, b.first) <= p.first && p.first <= std::max(a.first, b.first) && std::min(a.second, b.second) <= p.second && p.second <= std::max(a.second, b.second); }
static cpp_rational ringArea2(const DRing& r) {   // twice the signed area, exact
    cpp_rational s = 0; for (size_t i = 0; i < r.size(); i++) { const DP &p = r[i], &q = r[(i + 1) % r.size()]; s += toRat(p.first) * toRat(q.second) - toRat(q.first) * toRat(p.second); } return s;
}
struct RP { cpp_rational x, y; };
// 1 inside, 0 boundary, -1 outside; point rational
static int pip(const DRing& r, const RP& p) {
    int wn = 0;
    for (size_t i = 0; i < r.size(); i++) {
        cpp_rational ax = toRat(r[i].first), ay = toRat(r[i].second), bx = toRat(r[(i + 1) % r.size()].first), by = toRat(r[(i + 1) % r.size()].second);
        cpp_rational cr = (bx - ax) * (p.y - ay) - (by - ay) * (p.x - ax);
        if (cr == 0 && std::min(ax, bx) <= p.x && p.x <= std::max(ax, bx) && std::min(ay, by) <= p.y && p.y <= std::max(ay, by)) return 0;
        if (ay <= p.y) { if (by > p.y && cr > 0) wn++; } else { if (by <= p.y && cr < 0) wn--; }
    }
    return wn != 0 ? 1 : -1;
}
static bool segTouch(const DP& a, const DP& b, const DP& c, const DP& d) {   // closed segments intersect
    int o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (o1 * o2 < 0 && o3 * o4 < 0) return true;
    return onSeg(a, b, c) || onSeg(a, b, d) || onSeg(c, d, a) || onSeg(c, d, b);
}

enum Code { OK = 0, TOO_SHORT = 1, DUP_VERTEX = 2, ZERO_AREA = 3, CROSSING = 4, COLLINEAR_OVERLAP = 5, SPIKE = 6, REPEATED_VERTEX = 8 };
static const char* codeName(int c) { static const char* n[] = {"ok", "too_short", "dup_vertex", "zero_area", "crossing", "collinear_overlap", "spike", "", "repeated_vertex"}; return n[c]; }
struct Valid { int code = OK; bool orientOK = true; std::vector<int> depth; std::vector<cpp_rational> area2; cpp_rational area = 0; };

static Valid validate(const DRings& R) {
    Valid v; size_t n = R.size(); v.depth.assign(n, 0); v.area2.assign(n, 0); bool fZero = false;
    for (size_t i = 0; i < n; i++) {
        const DRing& r = R[i];
        if (r.size() < 3) { v.code = TOO_SHORT; return v; }
        for (size_t k = 0; k < r.size(); k++) if (r[k] == r[(k + 1) % r.size()]) { v.code = DUP_VERTEX; return v; }
        v.area2[i] = ringArea2(r); if (v.area2[i] == 0) fZero = true;
    }
    // edge pairs
    struct Seg { DP a, b; size_t ring, idx; };
    std::vector<Seg> S;
    for (size_t i = 0; i < n; i++) for (size_t k = 0; k < R[i].size(); k++) S.push_back({R[i][k], R[i][(k + 1) % R[i].size()], i, k});
    bool fCross = false, fColl = false, fSpike = false, fRep = false;
    for (size_t i = 0; i < S.size() && !fCross; i++) for (size_t j = i + 1; j < S.size() && !fCross; j++) {
        const Seg &s = S[i], &t = S[j];
        bool same = s.ring == t.ring; size_t m = R[s.ring].size();
        bool adj = same && (t.idx == s.idx + 1 || (s.idx == 0 && t.idx == m - 1));
        if (std::max(s.a.first, s.b.first) < std::min(t.a.first, t.b.first) || std::max(t.a.first, t.b.first) < std::min(s.a.first, s.b.first)) continue;
        if (std::max(s.a.second, s.b.second) < std::min(t.a.second, t.b.second) || std::max(t.a.second, t.b.second) < std::min(s.a.second, s.b.second)) continue;
        int o1 = orient(s.a, s.b, t.a), o2 = orient(s.a, s.b, t.b), o3 = orient(t.a, t.b, s.a), o4 = orient(t.a, t.b, s.b);
        if (adj) {   // shared endpoint: only a backtracking spike is wrong
            const DP &p0 = (t.idx == s.idx + 1) ? s.a : t.a, &p1 = (t.idx == s.idx + 1) ? s.b : t.b, &p2 = (t.idx == s.idx + 1) ? t.b : s.b;
            if (orient(p0, p1, p2) == 0) { BigInt q[6]; toCommonInts({p0.first, p0.second, p1.first, p1.second, p2.first, p2.second}, q); BigInt dot = (q[2] - q[0]) * (q[4] - q[2]) + (q[3] - q[1]) * (q[5] - q[3]); if (dot < 0) fSpike = true; }
            continue;
        }
#if MUTANT != 5
        if (o1 * o2 < 0 && o3 * o4 < 0) { fCross = true; continue; }
#endif
#if MUTANT != 4
        if (o1 == 0 && o2 == 0) {   // collinear: positive-length overlap?
            bool useX = s.a.first != s.b.first;
            double slo = useX ? std::min(s.a.first, s.b.first) : std::min(s.a.second, s.b.second), shi = useX ? std::max(s.a.first, s.b.first) : std::max(s.a.second, s.b.second);
            double tlo = useX ? std::min(t.a.first, t.b.first) : std::min(t.a.second, t.b.second), thi = useX ? std::max(t.a.first, t.b.first) : std::max(t.a.second, t.b.second);
            if (std::max(slo, tlo) < std::min(shi, thi)) { fColl = true; continue; }
        }
#endif
        // touching at a point is allowed between distinct edges; within one ring a repeated vertex is a non-simple ring
        if (same && (s.a == t.a || s.a == t.b || s.b == t.a || s.b == t.b)) fRep = true;
    }
    int code = fCross ? CROSSING : fColl ? COLLINEAR_OVERLAP : fSpike ? SPIKE : fRep ? REPEATED_VERTEX : fZero ? ZERO_AREA : OK;
    if (code != OK) { v.code = code; return v; }
    // containment depth
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) { if (i == j) continue;
            int res = 0; bool decided = false;
            for (size_t k = 0; k < R[i].size() && !decided; k++) { RP p{toRat(R[i][k].first), toRat(R[i][k].second)}; int c = pip(R[j], p); if (c != 0) { res = c; decided = true; } }
            for (size_t k = 0; k < R[i].size() && !decided; k++) { const DP &a = R[i][k], &b = R[i][(k + 1) % R[i].size()]; RP p{(toRat(a.first) + toRat(b.first)) / 2, (toRat(a.second) + toRat(b.second)) / 2}; int c = pip(R[j], p); if (c != 0) { res = c; decided = true; } }
            if (res == 1) v.depth[i]++;
        }
    }
    cpp_rational tot = 0;
    for (size_t i = 0; i < n; i++) {
        bool even = v.depth[i] % 2 == 0; bool isCcw = v.area2[i] > 0;
        if (even != isCcw) v.orientOK = false;
        cpp_rational a = v.area2[i] < 0 ? -v.area2[i] : v.area2[i];
#if MUTANT == 1
        tot += even ? -a : a;
#elif MUTANT == 2
        tot += a;
#else
        tot += even ? a : -a;
#endif
    }
    v.area = tot / 2; return v;
}
static DRings normalized(const DRings& R, const Valid& v) {   // outer CCW at even depth, CW at odd depth
    DRings o = R; for (size_t i = 0; i < R.size(); i++) { bool even = v.depth[i] % 2 == 0; if ((v.area2[i] > 0) != even) std::reverse(o[i].begin(), o[i].end()); } return o;
}
static double perimeter(const DRings& R) { double p = 0; for (auto& r : R) for (size_t i = 0; i < r.size(); i++) { double dx = r[i].first - r[(i + 1) % r.size()].first, dy = r[i].second - r[(i + 1) % r.size()].second; p += std::sqrt(dx * dx + dy * dy); } return p; }

static Pred opPred(const std::string& op, int ia, int ib) {
    if (op == "union") {
#if MUTANT == 3
        return [=](const bool* w) { return w[ia] && w[ib]; };
#else
        return [=](const bool* w) { return w[ia] || w[ib]; };
#endif
    }
    if (op == "intersection") return [=](const bool* w) { return w[ia] && w[ib]; };
    return [=](const bool* w) { return w[ia] && !w[ib]; };   // difference A minus B
}

// ---------------------------------------------------------------- selftest
static int fails = 0;
#define EXPECT(c, msg) do { if (!(c)) { fails++; fprintf(stderr, "SELFTEST FAIL: %s\n", msg); } } while (0)
static DRing rc(double x0, double y0, double x1, double y1, bool ccwOrder) { DRing r = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; if (!ccwOrder) std::reverse(r.begin(), r.end()); return r; }
static int selftest() {
    // S1 frame area and orientation
    { DRings f = {rc(0, 0, 10, 10, true), rc(3, 3, 7, 7, false)}; Valid v = validate(f); EXPECT(v.code == OK && v.orientOK, "S1 frame valid"); EXPECT(v.area == 84, "S1 frame area 84"); }
    // S2 oracle booleans
    { std::vector<DRings> s = {{rc(0, 0, 10, 10, true)}, {rc(5, 5, 15, 15, true)}};
      EXPECT(oracleArea(s, opPred("union", 0, 1)) == 175, "S2 union 175"); EXPECT(oracleArea(s, opPred("intersection", 0, 1)) == 25, "S2 inter 25"); EXPECT(oracleArea(s, opPred("difference", 0, 1)) == 75, "S2 diff 75"); }
    // S2b oracle with a hole and a diagonal: frame minus a triangle covering half of the hole region
    { std::vector<DRings> s = {{rc(0, 0, 10, 10, true), rc(3, 3, 7, 7, false)}, {DRing{{0, 0}, {10, 0}, {0, 10}}}};
      EXPECT(oracleArea(s, opPred("intersection", 0, 1)) == 50 - 8, "S2b frame inter triangle 42"); }
    // S3 two result rings sharing a collinear edge must be rejected
    { DRings f = {rc(0, 0, 10, 10, true), rc(10, 0, 20, 10, true)}; EXPECT(validate(f).code == COLLINEAR_OVERLAP, "S3 shared edge is a collinear overlap"); }
    // S4 bowtie
    { DRings f = {DRing{{0, 0}, {8, 8}, {8, 0}, {0, 8}}}; EXPECT(validate(f).code == CROSSING, "S4 bowtie crossing"); }
    // S5 symdiff of a region with itself is 0, with a shifted copy is not
    { std::vector<DRings> s = {{rc(0, 0, 10, 10, true), rc(3, 3, 7, 7, false)}, {rc(0, 0, 10, 10, true), rc(3, 3, 7, 7, false)}, {}};
      auto sd = [](const bool* w) { return w[0] != w[1]; }; EXPECT(oracleArea(s, sd) == 0, "S5 symdiff 0"); s[1] = {rc(1, 0, 11, 10, true), rc(4, 3, 8, 7, false)}; EXPECT(oracleArea(s, sd) == 28, "S5 symdiff of shifted frame 28"); }
    // S6 non-integer (dyadic) coordinates
    { std::vector<DRings> s = {{rc(0, 0, 0.5, 0.5, true)}, {rc(0.25, 0.25, 1, 1, true)}}; EXPECT(oracleArea(s, opPred("intersection", 0, 1)) == cpp_rational(1, 16), "S6 dyadic inter 1/16"); }
    // S7 spike and repeated vertex
    { DRings f = {DRing{{0, 0}, {10, 0}, {10, 10}, {14, 10}, {10, 10}, {0, 10}}}; EXPECT(validate(f).code == SPIKE, "S7 spike"); }
    printf(fails ? "selftest FAILED (%d)\n" : "selftest passed\n", fails); return fails ? 1 : 0;
}

// ---------------------------------------------------------------- corpus validation
static int validateCorpus(const std::string& dir) {
    DIR* d = opendir(dir.c_str()); if (!d) return 2; std::vector<std::string> names; while (auto* e = readdir(d)) { std::string n = e->d_name; if (n.size() > 4 && n.substr(n.size() - 4) == ".p2d") names.push_back(n); } closedir(d);
    std::sort(names.begin(), names.end()); int bad = 0, nvalid = 0, ninv = 0;
    for (auto& n : names) {
        p2d::Case c; if (!p2d::readCase(dir + "/" + n, c)) { printf("UNREADABLE %s\n", n.c_str()); bad++; continue; }
        if (c.family == "speed") continue;
        for (size_t s = 0; s < c.sets.size(); s++) {
            Valid v = validate(fromSet(c.sets[s])); bool ok = v.code == OK && v.orientOK;
            // each polygon is also checked on its own: a polygon's holes must lie in its outer ring
            bool expect = c.valid != 0 || s == 1;
            if (expect != ok) { printf("MISMATCH %s set %zu expected %s got code=%s orientOK=%d\n", n.c_str(), s, expect ? "valid" : "invalid", codeName(v.code), (int)v.orientOK); bad++; }
            if (c.valid == 0 && s == 0) ninv++; else if (ok) nvalid++;
        }
    }
    printf("corpus check: %zu files, %d valid sets ok, %d invalid sets confirmed invalid, %d problems\n", names.size(), nvalid, ninv, bad);
    return bad ? 1 : 0;
}

// ---------------------------------------------------------------- eval
static std::vector<DP> hullExact(std::vector<DP> p) {
    std::sort(p.begin(), p.end()); p.erase(std::unique(p.begin(), p.end()), p.end()); if (p.size() < 3) return p;
    std::vector<DP> h; for (int pass = 0; pass < 2; pass++) { size_t start = h.size(); for (size_t i = 0; i < p.size(); i++) { while (h.size() >= start + 2 && orient(h[h.size() - 2], h[h.size() - 1], p[i]) <= 0) h.pop_back(); h.push_back(p[i]); } h.pop_back(); std::reverse(p.begin(), p.end()); }
    return h;
}
static int incircle(const DP& a, const DP& b, const DP& c, const DP& d) {   // +1 if d strictly inside the circle of CCW abc
    BigInt q[8]; toCommonInts({a.first, a.second, b.first, b.second, c.first, c.second, d.first, d.second}, q);
    BigInt ax = q[0] - q[6], ay = q[1] - q[7], bx = q[2] - q[6], by = q[3] - q[7], cx = q[4] - q[6], cy = q[5] - q[7];
    BigInt det = (ax * ax + ay * ay) * (bx * cy - cx * by) - (bx * bx + by * by) * (ax * cy - cx * ay) + (cx * cx + cy * cy) * (ax * by - bx * ay);
    return det.sign();
}

static void printLine(const std::vector<std::pair<std::string, std::string>>& kv) { for (size_t i = 0; i < kv.size(); i++) printf("%s%s=%s", i ? "\t" : "", kv[i].first.c_str(), kv[i].second.c_str()); printf("\n"); }
static std::string fmt(double v) { char b[64]; snprintf(b, sizeof b, "%.6g", v); return b; }

static int eval(const std::string& casePath, const std::string& op, const std::string& resPath) {
    p2d::Case c; if (!p2d::readCase(casePath, c)) { printf("verdict=ERROR\tmsg=unreadable case\n"); return 2; }
    p2d::Res r; if (!p2d::readRes(resPath, r)) { printf("verdict=NORESULT\n"); return 0; }
    std::vector<std::pair<std::string, std::string>> kv;
    if (r.status != "OK") { kv.push_back({"verdict", "REFUSED"}); kv.push_back({"msg", r.msg}); printLine(kv); return 0; }
    DRings A = fromSet(c.sets[0]); DRings B = c.sets.size() > 1 ? fromSet(c.sets[1]) : DRings{};
    auto tolFlags = [&](double diff, double oracle, double perim) {
        double ad = std::fabs(diff);
        kv.push_back({"ok_rel", ad <= 1e-12 * std::max(std::fabs(oracle), 1.0) ? "1" : "0"});
        kv.push_back({"ok_grid1", ad <= 0.71 * perim + 1e-9 ? "1" : "0"});
        kv.push_back({"ok_grid8", ad <= 0.71e-8 * perim + 1e-9 * 0 ? "1" : "0"});
        kv.push_back({"exact", diff == 0 ? "1" : "0"});
    };
    if (op == "union" || op == "intersection" || op == "difference" || op == "offset") {
        cpp_rational oracle;
        if (op == "offset") {
            // oracle: polygon union the d-neighbourhood rectangles of each edge (Minkowski sum with the square, the miter offset of a rectilinear polygon)
            double d = (double)c.offset; DRings rects;
            for (auto& ring : A) for (size_t i = 0; i < ring.size(); i++) { const DP &p = ring[i], &q = ring[(i + 1) % ring.size()]; rects.push_back(rc(std::min(p.first, q.first) - d, std::min(p.second, q.second) - d, std::max(p.first, q.first) + d, std::max(p.second, q.second) + d, true)); }
            // rects: all CCW; set 1 windings may exceed 1 where they overlap; inside = winding > 0
            oracle = oracleArea({A, rects}, [](const bool* w) { return w[0] || w[1]; });
        } else oracle = oracleArea({A, B}, opPred(op, 0, 1));
        Valid v = validate(r.rings);
        kv.push_back({"verdict", "?"});
        kv.push_back({"nrings", std::to_string(r.rings.size())}); kv.push_back({"valid", codeName(v.code)}); kv.push_back({"orientOK", v.orientOK ? "1" : "0"});
        if (v.code != OK) {   // structurally invalid: still report the winding-number area of the rings as given (or reversed), for the breakdown
            cpp_rational aw = oracleArea({r.rings}, [](const bool* w) { return w[0]; });
            if (aw == 0) { DRings rev = r.rings; for (auto& q : rev) std::reverse(q.begin(), q.end()); aw = oracleArea({rev}, [](const bool* w) { return w[0]; }); }
            double dd = toDouble(aw - oracle); kv.push_back({"area_oracle", fmt(toDouble(oracle))}); kv.push_back({"area_wind", fmt(toDouble(aw))}); kv.push_back({"diff", fmt(dd)});
            kv.push_back({"ok_rel", std::fabs(dd) <= 1e-12 * std::max(std::fabs(toDouble(oracle)), 1.0) ? "1" : "0"}); kv.push_back({"ok_grid1", std::fabs(dd) <= 0.71 * perimeter(r.rings) + 1e-9 ? "1" : "0"});
            kv[0].second = "WRONG_INVALID"; printLine(kv); return 0; }
        cpp_rational diffR = v.area - oracle; double diff = toDouble(diffR), perim = perimeter(r.rings);
        kv.push_back({"area_res", fmt(toDouble(v.area))}); kv.push_back({"area_oracle", fmt(toDouble(oracle))}); kv.push_back({"diff", fmt(diff)}); kv.push_back({"perim", fmt(perim)});
        tolFlags(diff, toDouble(oracle), perim);
        // symmetric difference between the result region and the expected region
        DRings Rn = normalized(r.rings, v); cpp_rational sd;
        if (op == "offset") { DRings rects; double d = (double)c.offset; for (auto& ring : A) for (size_t i = 0; i < ring.size(); i++) { const DP &p = ring[i], &q = ring[(i + 1) % ring.size()]; rects.push_back(rc(std::min(p.first, q.first) - d, std::min(p.second, q.second) - d, std::max(p.first, q.first) + d, std::max(p.second, q.second) + d, true)); }
            sd = oracleArea({Rn, A, rects}, [](const bool* w) { return w[0] != (w[1] || w[2]); }); }
        else { Pred pp = opPred(op, 1, 2); sd = oracleArea({Rn, A, B}, [&](const bool* w) { return w[0] != pp(w); }); }
        kv.push_back({"symdiff", fmt(toDouble(sd))}); kv.push_back({"symdiff_zero", sd == 0 ? "1" : "0"});
        kv.push_back({"sd_ok_rel", toDouble(sd) <= 1e-12 * std::max(std::fabs(toDouble(oracle)), 1.0) ? "1" : "0"});
        kv.push_back({"sd_ok_grid1", toDouble(sd) <= 0.71 * perim + 1e-9 ? "1" : "0"});
        kv.push_back({"sd_ok_grid8", toDouble(sd) <= 0.71e-8 * perim + 1e-9 * 0 ? "1" : "0"});
        kv[0].second = "CHECKED"; printLine(kv); return 0;
    }
    if (op == "hull") {
        std::vector<DP> pts; for (auto& ring : A) for (auto& p : ring) pts.push_back(p);
        std::vector<DP> want = hullExact(pts); std::vector<DP> got; for (auto& ring : r.rings) for (auto& p : ring) got.push_back(p);
        // remove collinear (non-extreme) vertices from the result, then compare as sets
        std::vector<DP> g2; for (size_t i = 0; i < got.size(); i++) { const DP &a = got[(i + got.size() - 1) % got.size()], &b = got[i], &d = got[(i + 1) % got.size()]; if (got.size() < 3 || orient(a, b, d) != 0) g2.push_back(b); }
        std::set<DP> sw(want.begin(), want.end()), sg(g2.begin(), g2.end()); bool same = sw == sg;
        bool allInput = true; std::set<DP> inp(pts.begin(), pts.end()); for (auto& p : got) if (!inp.count(p)) allInput = false;
        kv.push_back({"verdict", "CHECKED"}); kv.push_back({"hull_same", same ? "1" : "0"}); kv.push_back({"all_vertices_from_input", allInput ? "1" : "0"}); kv.push_back({"nwant", std::to_string(want.size())}); kv.push_back({"ngot", std::to_string(got.size())});
        printLine(kv); return 0;
    }
    if (op == "triangulate" || op == "triangulate_delaunay") {
        DRings tris; int degenerate = 0; cpp_rational sumA = 0;
        for (auto& t : r.rings) { if (t.size() != 3) { kv.push_back({"verdict", "WRONG_INVALID"}); kv.push_back({"valid", "non_triangle"}); printLine(kv); return 0; }
            cpp_rational a2 = ringArea2(t); if (a2 == 0) { degenerate++; continue; } DRing u = t; if (a2 < 0) { std::reverse(u.begin(), u.end()); a2 = -a2; } tris.push_back(u); sumA += a2 / 2; }
        cpp_rational areaP = oracleArea({A, {}}, [](const bool* w) { return w[0]; });
        cpp_rational sd = oracleArea({tris, A}, [](const bool* w) { return w[0] != w[1]; });
        std::set<DP> inVerts; for (auto& ring : A) for (auto& p : ring) inVerts.insert(p); int steiner = 0; std::set<DP> seen;
        for (auto& t : tris) for (auto& p : t) if (!inVerts.count(p) && seen.insert(p).second) steiner++;
        kv.push_back({"verdict", "CHECKED"}); kv.push_back({"ntri", std::to_string(tris.size())}); kv.push_back({"degenerate", std::to_string(degenerate)}); kv.push_back({"steiner", std::to_string(steiner)});
        kv.push_back({"covers", sd == 0 ? "1" : "0"}); kv.push_back({"no_overlap", sumA == areaP ? "1" : "0"}); kv.push_back({"symdiff", fmt(toDouble(sd))}); kv.push_back({"area_sum_diff", fmt(toDouble(sumA - areaP))});
        // Delaunay (constrained): every interior edge shared by two triangles that is not a polygon edge must pass the empty-circle test, exactly
        std::set<std::pair<DP, DP>> cons; for (auto& ring : A) for (size_t i = 0; i < ring.size(); i++) { DP a = ring[i], b = ring[(i + 1) % ring.size()]; cons.insert({a, b}); cons.insert({b, a}); }
        std::map<std::pair<DP, DP>, std::pair<size_t, int>> dir; for (size_t t = 0; t < tris.size(); t++) for (int k = 0; k < 3; k++) dir[{tris[t][(size_t)k], tris[t][(size_t)(k + 1) % 3]}] = {t, k};
        int dviol = 0, dchecked = 0;
        for (auto& e : dir) { auto tw = dir.find({e.first.second, e.first.first}); if (tw == dir.end() || cons.count(e.first)) continue; if (e.second.first > tw->second.first) continue;
            const DRing& t1 = tris[e.second.first]; const DRing& t2 = tris[tw->second.first]; const DP& opp = t2[(size_t)(tw->second.second + 2) % 3]; dchecked++; if (incircle(t1[0], t1[1], t1[2], opp) > 0) dviol++; }
        kv.push_back({"delaunay_edges", std::to_string(dchecked)}); kv.push_back({"delaunay_violations", std::to_string(dviol)});
        printLine(kv); return 0;
    }
    if (op == "area") {
        DRings all = A; Valid v = validate(all); cpp_rational ex = v.area;
        if (!r.hasNum) { kv.push_back({"verdict", "NORESULT"}); printLine(kv); return 0; }
        // compare the library's double with the exact area: rel 1e-12
        cpp_rational got = toRat(r.num); double diff = toDouble(got - ex); double ea = toDouble(ex);
        kv.push_back({"verdict", "CHECKED"}); kv.push_back({"area_exact", fmt(ea)}); kv.push_back({"diff", fmt(diff)}); kv.push_back({"ok_rel", std::fabs(diff) <= 1e-12 * std::max(std::fabs(ea), 1.0) ? "1" : "0"}); kv.push_back({"exact", got == ex ? "1" : "0"});
        printLine(kv); return 0;
    }
    if (op == "intersects") {
        bool want = false;
        for (auto& ra : A) for (size_t i = 0; i < ra.size() && !want; i++) for (auto& rb : B) for (size_t j = 0; j < rb.size() && !want; j++) if (segTouch(ra[i], ra[(i + 1) % ra.size()], rb[j], rb[(j + 1) % rb.size()])) want = true;
        if (!want) { cpp_rational ia = oracleArea({A, B}, [](const bool* w) { return w[0] && w[1]; }); want = ia > 0; }
        kv.push_back({"verdict", "CHECKED"}); kv.push_back({"want", want ? "1" : "0"}); kv.push_back({"got", r.hasBool ? std::to_string(r.b) : "none"}); kv.push_back({"ok", r.hasBool && (r.b != 0) == want ? "1" : "0"});
        printLine(kv); return 0;
    }
    if (op == "isvalid") {
        // expected: set A is valid iff validate() says so (structure and orientation)
        Valid v = validate(A); bool want = v.code == OK && v.orientOK;
        kv.push_back({"verdict", "CHECKED"}); kv.push_back({"want", want ? "1" : "0"}); kv.push_back({"got", r.hasBool ? std::to_string(r.b) : "none"}); kv.push_back({"ok", r.hasBool && (r.b != 0) == want ? "1" : "0"}); kv.push_back({"why", codeName(v.code)});
        printLine(kv); return 0;
    }
    printf("verdict=ERROR\tmsg=unknown op\n"); return 2;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "selftest") return selftest();
    if (argc >= 3 && std::string(argv[1]) == "validate-corpus") return validateCorpus(argv[2]);
    if (argc >= 5 && std::string(argv[1]) == "eval") return eval(argv[2], argv[3], argv[4]);
    fprintf(stderr, "usage: check2d selftest | validate-corpus <dir> | eval <case> <op> <res>\n"); return 2;
}
