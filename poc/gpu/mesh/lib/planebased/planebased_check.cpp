// MP17 step 2: exactness harness for fixedint.h / predicates.h against Boost cpp_int (harness/exact.h style oracle).
//   planebased_check [N]         N random cases per primary config (plan's bar: 10000000); extreme/degenerate counts scale with N
//   planebased_check mut         mutant mode: small counts, exit code != 0 iff any difference is seen (every mutant must be caught)
// Per case: 13 points (4 planes of 3 points + one query point). Checked against the oracle, value by value (not just sign):
//   the 16 plane coefficients, W (3x3 normal determinant), Num (4x4 determinant), the vertex side, n.p+d for two planes.
// Both the full-width mul variant and the zero-limb-skipping mulMag variant of the predicates are checked.
// Oracle: cpp_int; plane via cross product, d via the triple-product determinant, Num/W via recursive Laplace expansion on the full 4x4 (a
// different evaluation path from the fixed-width code's shared-minor expansion). A separate decomposition mirrors the fixed code to record
// the true bit length of each intermediate.
#define FIXEDINT_WITH_BIG
#define FIXEDINT_THREADS
#include "predicates.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace pb;
using boost::multiprecision::abs;

// ---------------------------------------------------------------------------------------------------------------- rng
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) { next(); next(); }
    uint64_t next() {   // splitmix64
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int64_t range(int64_t lo, int64_t hi) { return lo + (int64_t)(next() % (uint64_t)(hi - lo + 1)); }
    int pick(int n) { return (int)(next() % (uint64_t)n); }
};

// ---------------------------------------------------------------------------------------------------------------- case
struct Case {
    int64_t P[13][3];
    int comb2 = 0; int64_t a2[2] = {0, 0};          // plane 2 := a2[0]*plane0 + a2[1]*plane1 (planes share a line)
    int comb3 = 0; int64_t a3[3] = {0, 0, 0};       // plane 3 := sum a3[i]*plane i (vertex lies on plane 3)
};

// ---------------------------------------------------------------------------------------------------------------- oracle
struct OPlane { Big c[4]; };

static OPlane oPlane(const int64_t* a, const int64_t* b, const int64_t* c) {
    Big e1[3], e2[3];
    for (int k = 0; k < 3; k++) { e1[k] = Big(b[k]) - Big(a[k]); e2[k] = Big(c[k]) - Big(a[k]); }
    OPlane p;
    p.c[0] = e1[1] * e2[2] - e1[2] * e2[1];
    p.c[1] = e1[2] * e2[0] - e1[0] * e2[2];
    p.c[2] = e1[0] * e2[1] - e1[1] * e2[0];
    // d = -(a . (b x c)) = -det[a;b;c] by a different route than n.a
    Big A[3] = {Big(a[0]), Big(a[1]), Big(a[2])}, B[3] = {Big(b[0]), Big(b[1]), Big(b[2])}, C[3] = {Big(c[0]), Big(c[1]), Big(c[2])};
    Big det = A[0] * (B[1] * C[2] - B[2] * C[1]) - A[1] * (B[0] * C[2] - B[2] * C[0]) + A[2] * (B[0] * C[1] - B[1] * C[0]);
    p.c[3] = -det;
    return p;
}

// Laplace expansion along the first row, on an n x n matrix given as rows of Big.
static Big detRec(const std::vector<std::vector<Big>>& m) {
    size_t n = m.size();
    if (n == 1) return m[0][0];
    if (n == 2) return m[0][0] * m[1][1] - m[0][1] * m[1][0];
    Big r = 0;
    for (size_t j = 0; j < n; j++) {
        std::vector<std::vector<Big>> sub;
        for (size_t i = 1; i < n; i++) {
            std::vector<Big> row;
            for (size_t k = 0; k < n; k++) if (k != j) row.push_back(m[i][k]);
            sub.push_back(row);
        }
        Big t = m[0][j] * detRec(sub);
        if (j & 1) r -= t; else r += t;
    }
    return r;
}

struct BitStats {
    // max bit length of |value| seen, per named intermediate
    int e = 0, n = 0, d = 0, m2nn = 0, m2nd = 0, m3W = 0, m3d = 0, term = 0, num = 0, ps = 0;
    void merge(const BitStats& o) {
        e = std::max(e, o.e); n = std::max(n, o.n); d = std::max(d, o.d); m2nn = std::max(m2nn, o.m2nn); m2nd = std::max(m2nd, o.m2nd);
        m3W = std::max(m3W, o.m3W); m3d = std::max(m3d, o.m3d); term = std::max(term, o.term); num = std::max(num, o.num); ps = std::max(ps, o.ps);
    }
};
static void upd(int& m, const Big& x) { m = std::max(m, bitlen(x)); }

struct Oracle {
    OPlane pl[4];
    Big W, Num, ps0, ps3;
    int side = 0, s0 = 0, s3 = 0;
    bool selfOk = true;
};

static OPlane oCombo(const OPlane* base, const int64_t* al, int cnt) {
    OPlane r;
    for (int k = 0; k < 4; k++) { r.c[k] = 0; for (int i = 0; i < cnt; i++) r.c[k] += Big(al[i]) * base[i].c[k]; }
    return r;
}

static Oracle oracleEval(const Case& cs, BitStats* bs) {
    Oracle o;
    o.pl[0] = oPlane(cs.P[0], cs.P[1], cs.P[2]);
    o.pl[1] = oPlane(cs.P[3], cs.P[4], cs.P[5]);
    o.pl[2] = cs.comb2 ? oCombo(o.pl, cs.a2, 2) : oPlane(cs.P[6], cs.P[7], cs.P[8]);
    o.pl[3] = cs.comb3 ? oCombo(o.pl, cs.a3, 3) : oPlane(cs.P[9], cs.P[10], cs.P[11]);
    std::vector<std::vector<Big>> m4(4, std::vector<Big>(4)), m3(3, std::vector<Big>(3));
    for (int i = 0; i < 4; i++) for (int k = 0; k < 4; k++) m4[i][k] = o.pl[i].c[k];
    for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) m3[i][k] = o.pl[i].c[k];
    o.Num = detRec(m4);
    o.W = detRec(m3);
    o.side = o.W == 0 ? 2 : o.Num.sign() * o.W.sign();
    auto ps = [&](const OPlane& p) { return p.c[0] * Big(cs.P[12][0]) + p.c[1] * Big(cs.P[12][1]) + p.c[2] * Big(cs.P[12][2]) + p.c[3]; };
    o.ps0 = ps(o.pl[0]); o.ps3 = ps(o.pl[3]);
    o.s0 = o.ps0.sign(); o.s3 = o.ps3.sign();
    if (bs) {   // mirror of the fixed-width evaluation, to record true bit lengths and to cross-check the Laplace result
        const Big *A0 = o.pl[0].c, *A1 = o.pl[1].c, *A2 = o.pl[2].c, *q = o.pl[3].c;
        for (int p = 0; p < 13; p++) for (int k = 0; k < 3; k++) if (p < 12 && p % 3 != 0) upd(bs->e, Big(cs.P[p][k]) - Big(cs.P[p - p % 3][k]));
        for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) upd(bs->n, o.pl[i].c[k]);
        for (int i = 0; i < 4; i++) upd(bs->d, o.pl[i].c[3]);
        Big m[4][4];
        for (int c = 0; c < 4; c++) for (int d = c + 1; d < 4; d++) {
            m[c][d] = A1[c] * A2[d] - A1[d] * A2[c];
            upd(d == 3 ? bs->m2nd : bs->m2nn, m[c][d]);
        }
        Big M0 = A0[1] * m[2][3] - A0[2] * m[1][3] + A0[3] * m[1][2];
        Big M1 = A0[0] * m[2][3] - A0[2] * m[0][3] + A0[3] * m[0][2];
        Big M2 = A0[0] * m[1][3] - A0[1] * m[0][3] + A0[3] * m[0][1];
        Big Wm = A0[0] * m[1][2] - A0[1] * m[0][2] + A0[2] * m[0][1];
        upd(bs->m3d, M0); upd(bs->m3d, M1); upd(bs->m3d, M2); upd(bs->m3W, Wm);
        Big t0 = q[0] * M0, t1 = q[1] * M1, t2 = q[2] * M2, t3 = q[3] * Wm;
        upd(bs->term, t0); upd(bs->term, t1); upd(bs->term, t2); upd(bs->term, t3);
        Big nm = -t0 + t1 - t2 + t3;
        upd(bs->num, nm);
        upd(bs->ps, o.ps0); upd(bs->ps, o.ps3);
        if (nm != o.Num || Wm != o.W) o.selfOk = false;
    }
    return o;
}

// ---------------------------------------------------------------------------------------------------------------- fixed-width evaluation
struct ClassStats {
    uint64_t cases = 0, diffs = 0, diffNaive = 0, diffTrim = 0, oracleSelf = 0;
    uint64_t vside[4] = {0, 0, 0, 0};   // -1 -> [0], 0 -> [1], +1 -> [2], 2 (no vertex) -> [3]
    uint64_t psZero = 0;
    std::string firstDiff;
    BitStats bits, bitsCombo;   // bitsCombo: cases whose planes 2/3 are integer combinations of other planes (outside the derivation)
    FIStats fi;
    void merge(const ClassStats& o) {
        fi.overflows += o.fi.overflows; fi.mismatches += o.fi.mismatches; fi.macs += o.fi.macs; fi.maxbits = std::max(fi.maxbits, o.fi.maxbits);
        cases += o.cases; diffs += o.diffs; diffNaive += o.diffNaive; diffTrim += o.diffTrim; oracleSelf += o.oracleSelf;
        for (int i = 0; i < 4; i++) vside[i] += o.vside[i];
        psZero += o.psZero; bits.merge(o.bits); bitsCombo.merge(o.bitsCombo);
        if (firstDiff.empty()) firstDiff = o.firstDiff;
    }
};

template <int N, bool T> static bool checkVariant(const Case& cs, const Oracle& o, std::string* why) {
    using F = FixedInt<N>;
    Plane<N> p[4];
    p[0] = planeFromPoints<N, T>(cs.P[0], cs.P[1], cs.P[2]);
    p[1] = planeFromPoints<N, T>(cs.P[3], cs.P[4], cs.P[5]);
    auto combo = [&](const int64_t* al, int cnt) {
        Plane<N> r;
        for (int k = 0; k < 4; k++) {
            F acc;
            for (int i = 0; i < cnt; i++) acc = add(acc, M<N, T>(F::fromI64(al[i]), p[i].c[k]));
            r.c[k] = acc;
        }
        return r;
    };
    p[2] = cs.comb2 ? combo(cs.a2, 2) : planeFromPoints<N, T>(cs.P[6], cs.P[7], cs.P[8]);
    p[3] = cs.comb3 ? combo(cs.a3, 3) : planeFromPoints<N, T>(cs.P[9], cs.P[10], cs.P[11]);
    bool ok = true;
    auto fail = [&](const char* what) { if (ok && why) *why = what; ok = false; };
    for (int i = 0; i < 4; i++) for (int k = 0; k < 4; k++) if (toBig(p[i].c[k]) != o.pl[i].c[k]) fail("plane coefficient");
    F W, Num;
    int side = vertexSide<N, T>(p[0], p[1], p[2], p[3], &W, &Num);
    if (toBig(W) != o.W) fail("W (3x3 determinant)");
    if (toBig(Num) != o.Num) fail("Num (4x4 determinant)");
    if (side != o.side) fail("vertex side");
    F v0, v3;
    int s0 = pointSide<N, T>(p[0], cs.P[12], &v0), s3 = pointSide<N, T>(p[3], cs.P[12], &v3);
    if (toBig(v0) != o.ps0 || s0 != o.s0) fail("point side plane 0");
    if (toBig(v3) != o.ps3 || s3 != o.s3) fail("point side plane 3");
    return ok;
}

template <int N> static void evalCase(const Case& cs, ClassStats& st) {
    Oracle o = oracleEval(cs, (cs.comb2 || cs.comb3) ? &st.bitsCombo : &st.bits);
    if (!o.selfOk) st.oracleSelf++;
    std::string w1, w2;
    bool a = checkVariant<N, false>(cs, o, &w1);
    bool b = checkVariant<N, true>(cs, o, &w2);
    st.cases++;
    if (!a) st.diffNaive++;
    if (!b) st.diffTrim++;
    if (!a || !b) {
        st.diffs++;
        if (st.firstDiff.empty()) {
            char buf[200];
            snprintf(buf, sizeof buf, "%s%s%s%s", !a ? "naive: " : "", !a ? w1.c_str() : "", !b ? " trim: " : "", !b ? w2.c_str() : "");
            st.firstDiff = buf;
        }
    }
    st.vside[o.side == 2 ? 3 : o.side + 1]++;
    if (o.s3 == 0) st.psZero++;
}

// ---------------------------------------------------------------------------------------------------------------- generators
enum Class { RANDOM, EXTREME_CORNER, EXTREME_MIXED, DEGENERATE };

static void genRandom(Rng& r, int64_t v, Case& c) {
    for (auto& p : c.P) for (int k = 0; k < 3; k++) p[k] = r.range(-v, v);
}
static void genCorner(Rng& r, int64_t v, Case& c) {
    for (auto& p : c.P) for (int k = 0; k < 3; k++) p[k] = (r.next() & 1) ? v : -v;
}
static void genMixed(Rng& r, int64_t v, Case& c) {
    int64_t pool[] = {v, -v, v - 1, -(v - 1), 0, 1, -1, v / 2, -(v / 2), (int64_t)1 << 20 > v ? 0 : ((int64_t)1 << 20)};
    for (auto& p : c.P) for (int k = 0; k < 3; k++) p[k] = pool[r.pick(10)];
}
// subtype in 0..7, see comments
static void genDegenerate(Rng& r, int64_t v, int sub, Case& c) {
    int64_t q = v / 10;
    genRandom(r, v, c);
    switch (sub) {
    case 0: {   // collinear triple: c = a + t (b - a)
        int k = r.pick(4);
        int64_t t = r.range(-2, 2);
        for (int j = 0; j < 3; j++) {
            c.P[3 * k][j] = r.range(-q, q);
            c.P[3 * k + 1][j] = r.range(-q, q);
        }
        for (int j = 0; j < 3; j++) c.P[3 * k + 2][j] = c.P[3 * k][j] + t * (c.P[3 * k + 1][j] - c.P[3 * k][j]);
        break;
    }
    case 1: {   // duplicate points in one triple
        int k = r.pick(4), which = r.pick(3);
        int a = 3 * k, b = 3 * k + 1, cc = 3 * k + 2;
        if (which == 0) for (int j = 0; j < 3; j++) c.P[b][j] = c.P[a][j];
        else if (which == 1) for (int j = 0; j < 3; j++) c.P[cc][j] = c.P[a][j];
        else for (int j = 0; j < 3; j++) c.P[cc][j] = c.P[b][j];
        break;
    }
    case 2: c.comb3 = 1; for (int i = 0; i < 3; i++) c.a3[i] = r.range(-3, 3); break;               // vertex exactly on plane 3
    case 3: c.comb2 = 1; for (int i = 0; i < 2; i++) c.a2[i] = r.range(-3, 3); break;               // three planes sharing a line (or fewer)
    case 4: {   // query point coplanar with plane 3's points
        int64_t s = r.range(-2, 2), t = r.range(-2, 2);
        for (int j = 0; j < 3; j++) {
            for (int i = 9; i < 12; i++) c.P[i][j] = r.range(-q, q);
            c.P[12][j] = c.P[9][j] + s * (c.P[10][j] - c.P[9][j]) + t * (c.P[11][j] - c.P[9][j]);
        }
        break;
    }
    case 5: for (auto& p : c.P) for (int k = 0; k < 3; k++) p[k] = r.range(-2, 2); break;           // tiny range, natural degeneracies
    case 6: { int64_t base[3] = {r.range(-v, v), r.range(-v, v), r.range(-v, v)}; for (auto& p : c.P) for (int k = 0; k < 3; k++) p[k] = base[k]; break; }   // all equal
    case 7: {   // plane 1 parallel to plane 0 (translate its points)
        int64_t t[3] = {r.range(-q, q), r.range(-q, q), r.range(-q, q)};
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) c.P[3 + i][j] = std::max(-v, std::min(v, c.P[i][j] / 2 + t[j]));
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) c.P[i][j] /= 2;
        break;
    }
    }
}

// Hill climb for maximal intermediates. objective: 0 Num, 1 W, 2 M0 (3x3 with the d column), 3 d of plane 0
static Big objective(const Case& cs, int obj) {
    OPlane p[4] = {oPlane(cs.P[0], cs.P[1], cs.P[2]), oPlane(cs.P[3], cs.P[4], cs.P[5]), oPlane(cs.P[6], cs.P[7], cs.P[8]), oPlane(cs.P[9], cs.P[10], cs.P[11])};
    const Big *A0 = p[0].c, *A1 = p[1].c, *A2 = p[2].c, *q = p[3].c;
    Big m12 = A1[1] * A2[2] - A1[2] * A2[1], m02 = A1[0] * A2[2] - A1[2] * A2[0], m01 = A1[0] * A2[1] - A1[1] * A2[0];
    Big m23 = A1[2] * A2[3] - A1[3] * A2[2], m13 = A1[1] * A2[3] - A1[3] * A2[1], m03 = A1[0] * A2[3] - A1[3] * A2[0];
    Big M0 = A0[1] * m23 - A0[2] * m13 + A0[3] * m12, M1 = A0[0] * m23 - A0[2] * m03 + A0[3] * m02, M2 = A0[0] * m13 - A0[1] * m03 + A0[3] * m01;
    Big W = A0[0] * m12 - A0[1] * m02 + A0[2] * m01;
    if (obj == 1) return abs(W);
    if (obj == 2) return abs(M0);
    if (obj == 3) return abs(A0[3]);
    return abs(-q[0] * M0 + q[1] * M1 - q[2] * M2 + q[3] * W);
}
static void hillClimb(uint64_t seed, int64_t v, int obj, int restarts, int steps, std::vector<Case>& out) {
    Rng r(seed);
    for (int rs = 0; rs < restarts; rs++) {
        Case c;
        genCorner(r, v, c);
        Big best = objective(c, obj);
        for (int s = 0; s < steps; s++) {
            Case t = c;
            int i = r.pick(12), k = r.pick(3);
            switch (r.pick(4)) {
            case 0: t.P[i][k] = v; break;
            case 1: t.P[i][k] = -v; break;
            case 2: t.P[i][k] = -t.P[i][k]; break;
            default: t.P[i][k] = r.range(-v, v); break;
            }
            Big o = objective(t, obj);
            if (o >= best) { best = o; c = t; }
        }
        out.push_back(c);
    }
}

// ---------------------------------------------------------------------------------------------------------------- driver
static unsigned g_threads = 8;

template <int N> static ClassStats runClass(Class cls, int L, uint64_t count, uint64_t seed) {
    int64_t v = (L >= 63) ? INT64_MAX : (((int64_t)1 << L) - 1);
    const uint64_t CH = 2048;
    uint64_t chunks = (count + CH - 1) / CH;
    std::atomic<uint64_t> next{0};
    std::vector<ClassStats> per(g_threads);
    std::vector<std::thread> th;
    for (unsigned t = 0; t < g_threads; t++) th.emplace_back([&, t]() {
        for (;;) {
            uint64_t ch = next++;
            if (ch >= chunks) break;
            Rng r(seed * 1000003ull + ch);
            uint64_t lo = ch * CH, hi = std::min(count, lo + CH);
            for (uint64_t i = lo; i < hi; i++) {
                Case c;
                switch (cls) {
                case RANDOM: genRandom(r, v, c); break;
                case EXTREME_CORNER: genCorner(r, v, c); break;
                case EXTREME_MIXED: genMixed(r, v, c); break;
                case DEGENERATE: genDegenerate(r, v, (int)(i % 8), c); break;
                }
                evalCase<N>(c, per[t]);
            }
        }
        per[t].fi = g_fi;
    });
    for (auto& x : th) x.join();
    ClassStats tot;
    for (auto& s : per) tot.merge(s);
    return tot;
}

template <int N> static ClassStats runCases(const std::vector<Case>& cs) {
    ClassStats st;
    g_fi = FIStats();
    for (const Case& c : cs) evalCase<N>(c, st);
    st.fi = g_fi;
    return st;
}

// primitives vs cpp_int
template <int N> static FixedInt<N> randFI(Rng& r) {
    FixedInt<N> a;
    for (int i = 0; i < N; i++) a.w[i] = (uint32_t)r.next();
    int nb = r.pick(32 * N + 1);   // random magnitude length
    for (int i = 0; i < N; i++) {
        int lo = 32 * i;
        if (lo >= nb) a.w[i] = 0;
        else if (lo + 32 > nb) a.w[i] &= (uint32_t)((1ull << (nb - lo)) - 1);
    }
    if (r.next() & 1) a = neg(a);
    if (r.pick(16) == 0) { int z = r.pick(N); for (int i = 0; i < N; i++) a.w[i] = i < z ? 0 : a.w[i]; }
    if (r.pick(40) == 0) for (int i = 0; i < N; i++) a.w[i] = 0xFFFFFFFFu;   // -1
    return a;
}
template <int N> static uint64_t primitiveTest(uint64_t count, uint64_t seed, std::string* first) {
    Rng r(seed);
    uint64_t bad = 0;
    auto rep = [&](const char* op) { bad++; if (first->empty()) *first = op; };
    for (uint64_t i = 0; i < count; i++) {
        FixedInt<N> a = randFI<N>(r), b = randFI<N>(r);
        Big A = toBig(a), B = toBig(b);
        if (toBig(add(a, b)) != wrapN<N>(A + B)) rep("add");
        if (toBig(sub(a, b)) != wrapN<N>(A - B)) rep("sub");
        if (toBig(neg(a)) != wrapN<N>(-A)) rep("neg");
        if (toBig(mul(a, b)) != wrapN<N>(A * B)) rep("mul");
        if (toBig(mulMag(a, b)) != wrapN<N>(A * B)) rep("mulMag");
        int c = cmp(a, b), ec = A < B ? -1 : (A > B ? 1 : 0);
        if (c != ec) rep("cmp");
        if (a.sign() != A.sign()) rep("sign");
        int n = r.pick(32 * N);
        if (toBig(shl(a, n)) != wrapN<N>(A << n)) rep("shl");
        if (toBig(shr(a, n)) != floorShr(A, n)) rep("shr");
    }
    return bad;
}

struct Totals { uint64_t diffs = 0; };

static void printRow(const char* cls, const ClassStats& s) {
    printf("  %-22s cases %-10llu diffs %-6llu (naive %llu, trim %llu) oracleSelfFail %llu | vertex side -:%llu 0:%llu +:%llu none:%llu | query on plane3: %llu\n", cls,
           (unsigned long long)s.cases, (unsigned long long)s.diffs, (unsigned long long)s.diffNaive, (unsigned long long)s.diffTrim,
           (unsigned long long)s.oracleSelf, (unsigned long long)s.vside[0], (unsigned long long)s.vside[1], (unsigned long long)s.vside[2],
           (unsigned long long)s.vside[3], (unsigned long long)s.psZero);
    printf("      max bits seen in this class: n %d d %d W %d Num %d\n", std::max(s.bits.n, s.bitsCombo.n), std::max(s.bits.d, s.bitsCombo.d), std::max(s.bits.m3W, s.bitsCombo.m3W), std::max(s.bits.num, s.bitsCombo.num));
#ifdef FIXEDINT_CHECK
    printf("      debug mode: fixedint ops overflowing the width %llu, in-range mismatches vs cpp_int %llu, max true bit length of any op result %d\n", (unsigned long long)s.fi.overflows, (unsigned long long)s.fi.mismatches, s.fi.maxbits);
#endif
    if (!s.firstDiff.empty()) printf("      first diff: %s\n", s.firstDiff.c_str());
}

static void printBits(const char* tag, int L, const BitStats& b) {
    // derived bounds from predicates.h, computed exactly: magnitude bits = bit length of (coefficient * v^power)
    Big v = (Big(1) << L) - 1;
    auto bits = [&](long c, int pw) { Big x = c; for (int i = 0; i < pw; i++) x *= v; return bitlen(x); };
    printf("  %-14s L=%d  observed bits (derived bound): edge %d (%d) | n %d (%d) | d %d (%d) | 2x2nn %d (%d) | 2x2nd %d (%d) | W %d (%d) | 3x3d %d (%d) | term %d (%d) | Num %d (%d) | n.p+d %d (%d)\n",
           tag, L, b.e, bits(2, 1), b.n, bits(8, 2), b.d, bits(24, 3), b.m2nn, bits(128, 4), b.m2nd, bits(384, 5), b.m3W, bits(3072, 6), b.m3d, bits(9216, 7),
           b.term, bits(73728, 9), b.num, bits(294912, 9), b.ps, bits(48, 3));
}

template <int N> static uint64_t runConfig(int L, uint64_t nRandom, uint64_t seed, bool full, bool expectClean) {
    printf("config L=%d width=%d bits (%d limbs)%s\n", L, 32 * N, N, expectClean ? "" : "   [NEGATIVE CONTROL: width too small, differences expected]");
    uint64_t diffs = 0;
    BitStats all, allCombo;
    auto rec = [&](const char* name, const ClassStats& s) { printRow(name, s); diffs += s.diffs + s.fi.overflows + s.fi.mismatches; all.merge(s.bits); allCombo.merge(s.bitsCombo); if (s.oracleSelf) diffs += s.oracleSelf; };
    rec("random", runClass<N>(RANDOM, L, nRandom, seed + 1));
    uint64_t nExt = full ? std::max<uint64_t>(nRandom / 5, 1000) : nRandom / 4;
    uint64_t nDeg = full ? std::max<uint64_t>(nRandom / 5, 1000) : nRandom / 4;
    rec("extreme corners", runClass<N>(EXTREME_CORNER, L, nExt, seed + 2));
    rec("extreme mixed", runClass<N>(EXTREME_MIXED, L, nExt, seed + 3));
    // hill climb to maximal determinants
    std::vector<Case> hc;
    int64_t v = ((int64_t)1 << L) - 1;
    if (full) for (int obj = 0; obj < 4; obj++) hillClimb(seed + 10 + obj, v, obj, 12, 1500, hc);
    else for (int obj = 0; obj < 4; obj++) hillClimb(seed + 10 + obj, v, obj, 2, 100, hc);
    rec("extreme hill-climbed", runCases<N>(hc));
    rec("degenerate (8 kinds)", runClass<N>(DEGENERATE, L, nDeg, seed + 4));
    printBits("no-override", L, all);
    printBits("with-override", L, allCombo);
    return diffs;
}

int main(int argc, char** argv) {
    bool mutMode = argc > 1 && !strcmp(argv[1], "mut");
    uint64_t N = mutMode ? 20000 : (argc > 1 ? strtoull(argv[1], nullptr, 10) : 1000000);
    unsigned hc = std::thread::hardware_concurrency();
    g_threads = hc ? hc : 4;
    printf("planebased_check: MUTANT=%d, random cases per primary config %llu, threads %u\n", MUTANT, (unsigned long long)N, g_threads);
#ifdef FIXEDINT_CHECK
    printf("FIXEDINT_CHECK is ON (every operation recomputed with cpp_int)\n");
#endif
    // sanity: known geometry. planes x=0 (normal +x), y=0, z=0 and the plane through (1,0,0),(0,1,0),(0,0,1); vertex = origin, n.x+d = -1 -> side -1
    {
        int64_t o[3] = {0, 0, 0}, x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, z[3] = {0, 0, 1};
        auto px = planeFromPoints<8, false>(o, y, z), py = planeFromPoints<8, false>(o, z, x), pz = planeFromPoints<8, false>(o, x, y), q = planeFromPoints<8, false>(x, y, z);
        int s = vertexSide<8, false>(px, py, pz, q);
        int s2 = vertexSide<8, false>(py, px, pz, q);   // orientation of the first three planes does not change the answer
        printf("sanity: origin vs plane x+y+z=1 -> %d, with two planes swapped -> %d (both must be -1)%s\n", s, s2, (s == -1 && s2 == -1) || MUTANT ? "" : "  FAILED");
        if (!MUTANT && !(s == -1 && s2 == -1)) return 2;
    }
    uint64_t totalDiffs = 0;
    // primitives
    {
        std::string f;
        uint64_t pc = mutMode ? 20000 : 1000000, bad = 0;
        bad += primitiveTest<8>(pc, 11, &f);
        bad += primitiveTest<12>(pc, 12, &f);
        bad += primitiveTest<16>(pc, 13, &f);
        printf("primitives (add, sub, neg, mul, mulMag, cmp, sign, shl, shr) vs cpp_int, %llu random pairs per width x 3 widths: %llu differences%s%s\n",
               (unsigned long long)pc, (unsigned long long)bad, f.empty() ? "" : "  first: ", f.c_str());
        totalDiffs += bad;
    }
    uint64_t seed = 20261114;
#ifdef FIXEDINT_CHECK
    // debug mode: smaller counts (every op is recomputed with cpp_int); overflow and mismatch counters are the point
    N = mutMode ? N : std::min<uint64_t>(N, 100000);
#endif
    totalDiffs += runConfig<8>(26, N, seed, !mutMode, true);
    totalDiffs += runConfig<12>(40, N, seed + 100, !mutMode, true);
    totalDiffs += runConfig<16>(53, N, seed + 200, !mutMode, true);
    if (!mutMode) {
        uint64_t n2 = std::max<uint64_t>(N / 10, 20000);
        // wider-than-needed widths (the benchmark uses 384/512 on small inputs): must also be exact
        totalDiffs += runConfig<12>(26, n2, seed + 300, true, true);
        totalDiffs += runConfig<16>(26, n2, seed + 400, true, true);
        totalDiffs += runConfig<16>(40, n2, seed + 500, true, true);
        // negative controls: the harness must see overflow when the width is too small
        uint64_t nc1 = runConfig<8>(40, n2, seed + 600, true, false);
        uint64_t nc2 = runConfig<12>(53, n2, seed + 700, true, false);
        uint64_t nc3 = runConfig<8>(53, n2, seed + 800, true, false);
        printf("negative controls: differences %llu / %llu / %llu (all must be > 0)%s\n", (unsigned long long)nc1, (unsigned long long)nc2, (unsigned long long)nc3,
               (nc1 && nc2 && nc3) ? "" : "  FAILED: control did not trigger");
        if (!(nc1 && nc2 && nc3)) totalDiffs += 1;
    }
#ifdef FIXEDINT_CHECK
    // note: g_fi is thread_local; worker-thread counters are not merged here. Report what the main thread saw only for primitives.
#endif
    printf("TOTAL DIFFERENCES (must-pass configs): %llu\n", (unsigned long long)totalDiffs);
    return totalDiffs ? 1 : 0;
}
