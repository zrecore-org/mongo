// MP18b set 3: third touch family, created after geogram_guard2.cpp was frozen. New seed, new classes and sizes.
//   gen_touch_set3 <outdir>
// Classes (tag set3=<class>): nested_vertex / nested_edge / nested_face (random tetrahedra inside a box, tetrahedron or octahedron, sharing exactly one
// vertex / one edge / three face vertices), nonconvex_vv (L, U and plus-shaped prisms, tetrahedron on a random top vertex), threecomp_vv (A = three
// disjoint solids, B touches one at a vertex), twopoint (A = two boxes, B touches both), threepoint (A = three boxes, B a tetrahedron touching all
// three), randvv (convex vertex-vertex placements, sizes unused before, random signed permutations + shears), sphere_reflect (icosphere and its
// point reflection through one of its vertices), sphere_box (icosphere under/next to a box face). Exact volumes for all but the sphere classes.
#define main gen_touch_unused_main
#include "gen_touch.cpp"
#undef main
#include "../../harness/pip.h"
#include "../../harness/topology.h"
#include <random>
extern "C" void exactinit(void);

static std::mt19937_64 rng(0x53455433474D5033ULL);
static int rnd(int n) { return (int)(rng() % (uint64_t)n); }
static const long long ID[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
static Shape applyMap(Shape s, const long long M[3][3]) {
    for (auto& p : s.v) { I3 q; for (int i = 0; i < 3; i++) q[i] = M[i][0]*p[0] + M[i][1]*p[1] + M[i][2]*p[2]; p = q; }
    long long d = M[0][0]*(M[1][1]*M[2][2]-M[1][2]*M[2][1]) - M[0][1]*(M[1][0]*M[2][2]-M[1][2]*M[2][0]) + M[0][2]*(M[1][0]*M[2][1]-M[1][1]*M[2][0]);
    if (d != 1 && d != -1) { fprintf(stderr, "map not unimodular\n"); exit(1); }
    if (d < 0) for (auto& f : s.t) std::swap(f[1], f[2]);
    return s;
}
static void randomMap(long long M[3][3]) {
    if (rnd(4) == 0) { memcpy(M, ID, sizeof(long long) * 9); return; }
    int perm[3] = {0, 1, 2}; for (int i = 2; i > 0; i--) std::swap(perm[i], perm[rnd(i + 1)]);
    long long R[3][3] = {{0,0,0},{0,0,0},{0,0,0}}; for (int i = 0; i < 3; i++) R[i][perm[i]] = rnd(2) ? 1 : -1;
    int ns = rnd(4);
    for (int k = 0; k < ns; k++) {
        int i = rnd(3), j = (i + 1 + rnd(2)) % 3; long long c = (rnd(2) ? 1 : -1) * (1 + rnd(2));
        long long E[3][3] = {{1,0,0},{0,1,0},{0,0,1}}; E[i][j] = c; long long T[3][3];
        for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) { T[a][b] = 0; for (int m = 0; m < 3; m++) T[a][b] += E[a][m] * R[m][b]; }
        memcpy(R, T, sizeof R);
    }
    memcpy(M, R, sizeof R);
}
static Shape joined(const Shape& a, const Shape& b) { Shape s = a; int o = (int)a.v.size(); for (auto& p : b.v) s.v.push_back(p); for (auto f : b.t) s.t.push_back({f[0]+o, f[1]+o, f[2]+o}); return s; }
static Shape prism(const std::vector<std::array<long long, 2>>& P, const std::vector<std::array<int, 3>>& tri, long long z0, long long z1) {
    Shape s; int n = (int)P.size();
    for (auto& p : P) s.v.push_back({p[0], p[1], z0}); for (auto& p : P) s.v.push_back({p[0], p[1], z1});
    for (auto& t : tri) { s.t.push_back({t[0], t[2], t[1]}); s.t.push_back({n+t[0], n+t[1], n+t[2]}); }
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; s.t.push_back({i, j, n+j}); s.t.push_back({i, n+j, n+i}); }
    return s;
}
static Shape lPrism() { return prism({{0,0},{4,0},{4,2},{2,2},{2,4},{0,4}}, {{0,1,2},{0,2,3},{0,3,4},{0,4,5}}, 0, 2); }
static Shape uPrism() { return prism({{0,0},{6,0},{6,4},{4,4},{4,2},{2,2},{2,4},{0,4}}, {{0,1,4},{0,4,5},{1,2,3},{1,3,4},{0,5,6},{0,6,7}}, 0, 2); }
static Shape plusPrism() { return prism({{2,0},{4,0},{4,2},{6,2},{6,4},{4,4},{4,6},{2,6},{2,4},{0,4},{0,2},{2,2}},
    {{11,0,1},{11,1,2},{2,3,4},{2,4,5},{11,2,5},{11,5,8},{8,5,6},{8,6,7},{10,11,8},{10,8,9}}, 0, 2); }
static Shape tetV(I3 a, I3 b, I3 c, I3 d) { Shape s; s.v = {a, b, c, d}; s.t = {{0,2,1},{0,1,3},{0,3,2},{1,2,3}}; orient(s); return s; }
static Shape boxAt(long long x0, long long y0, long long z0, long long x1, long long y1, long long z1) {
    Shape s = makeBoxS(0, 1); for (auto& p : s.v) { p[0] = p[0] ? x1 : x0; p[1] = p[1] ? y1 : y0; p[2] = p[2] ? z1 : z0; } orient(s); return s; }
static Shape shifted(Shape s, long long dx, long long dy, long long dz) { for (auto& p : s.v) { p[0] += dx; p[1] += dy; p[2] += dz; } return s; }
static bool volZero(const I3& a, const I3& b, const I3& c, const I3& d) { return det3(sub(b, a), sub(c, a), sub(d, a)) == 0; }

static std::string outdir; static FILE* mf; static std::map<std::string, int> counts;
static void emitRaw(const std::string& cls, const std::string& id, const Mesh& MA, const Mesh& MB, const char* ex[5]) {
    writeMesh(outdir + "/adversarial/" + id + "_A.msh", MA); writeMesh(outdir + "/adversarial/" + id + "_B.msh", MB);
    fprintf(mf, "%s\ttouch\tvalid,set3=%s\t%zu\t%zu\t%zu\t%zu", id.c_str(), cls.c_str(), MA.nv(), MA.nt(), MB.nv(), MB.nt());
    for (int i = 0; i < 5; i++) fprintf(mf, "\t%s", ex[i]); fprintf(mf, "\n"); counts[cls]++;
}
static void emitPair(const std::string& cls, const std::string& name, const Shape& A, const Shape& B, int mode) {
    Mesh MA = toMesh(A), MB = toMesh(B); Dyadic vA = exactVolumeTimes6(MA), vB = exactVolumeTimes6(MB);
    if (signExact(vA) <= 0 || signExact(vB) <= 0) { fprintf(stderr, "non-positive volume %s\n", name.c_str()); exit(1); }
    if (!analyzeTopology(MA).closedManifold() || !analyzeTopology(MB).closedManifold()) { fprintf(stderr, "not closed manifold %s\n", name.c_str()); exit(1); }
    Dyadic z{BigInt(0), 0}, U = mode ? vA : addExact(vA, vB), I = mode ? vB : z, D = mode ? subExact(vA, vB) : vA;
    std::string s[5] = {toString(vA), toString(vB), toString(U), toString(I), toString(D)}; const char* ex[5] = {s[0].c_str(), s[1].c_str(), s[2].c_str(), s[3].c_str(), s[4].c_str()};
    emitRaw(cls, "s3_" + cls + "_" + name, MA, MB, ex);
}
static std::string two(int n) { char b[16]; snprintf(b, sizeof b, "%03d", n); return b; }
static bool strictlyInside(const Mesh& MA, const I3& p) { double q[3] = {(double)p[0], (double)p[1], (double)p[2]}; Pip r = classifyPoint(MA, q); return !r.boundary && r.inside; }
static bool onBoundary(const Mesh& MA, const I3& p) { double q[3] = {(double)p[0], (double)p[1], (double)p[2]}; return classifyPoint(MA, q).boundary; }
static I3 randI(int lo, int hi) { return {lo + rnd(hi - lo + 1), lo + rnd(hi - lo + 1), lo + rnd(hi - lo + 1)}; }

// ---- icospheres (same construction as gen_corpus.cpp: + - * / sqrt only) ----
static Mesh makeIcosphere(int sub, double radius) {
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    double base[12][3] = {{-1,phi,0},{1,phi,0},{-1,-phi,0},{1,-phi,0},{0,-1,phi},{0,1,phi},{0,-1,-phi},{0,1,-phi},{phi,0,-1},{phi,0,1},{-phi,0,-1},{-phi,0,1}};
    static const uint32_t F[20][3] = {{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
    Mesh m; auto norm = [&](double x, double y, double z) { double l = std::sqrt(x*x + y*y + z*z); return std::array<double, 3>{x/l, y/l, z/l}; };
    for (auto& b : base) { auto n = norm(b[0], b[1], b[2]); m.addV(n[0], n[1], n[2]); }
    std::vector<std::array<uint32_t, 3>> faces; for (auto& f : F) faces.push_back({f[0], f[1], f[2]});
    for (int s = 0; s < sub; s++) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
        auto midpoint = [&](uint32_t a, uint32_t b) { auto key = std::make_pair(std::min(a, b), std::max(a, b)); auto it = mid.find(key); if (it != mid.end()) return it->second;
            auto n = norm((m.v[3*a]+m.v[3*b])/2, (m.v[3*a+1]+m.v[3*b+1])/2, (m.v[3*a+2]+m.v[3*b+2])/2); return mid[key] = m.addV(n[0], n[1], n[2]); };
        std::vector<std::array<uint32_t, 3>> nf;
        for (auto& f : faces) { uint32_t a = midpoint(f[0], f[1]), b = midpoint(f[1], f[2]), c = midpoint(f[2], f[0]); nf.push_back({f[0], a, c}); nf.push_back({f[1], b, a}); nf.push_back({f[2], c, b}); nf.push_back({a, b, c}); }
        faces.swap(nf);
    }
    for (auto& f : faces) m.addT(f[0], f[1], f[2]);
    for (double& x : m.v) x *= radius;
    return m;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2; exactinit();
    outdir = argv[1]; mkdir(outdir.c_str(), 0755); mkdir((outdir + "/adversarial").c_str(), 0755);
    mf = fopen((outdir + "/manifest.tsv").c_str(), "wb");
    fprintf(mf, "id\tclass\ttags\tnvA\tntA\tnvB\tntB\tvol6_A\tvol6_B\tvol6_union\tvol6_inter\tvol6_diff\n");
    long long M[3][3];
    // ---- nested: B inside the convex A, exactly the stated boundary contact ----
    {
        Shape outer[3] = {boxAt(0,0,0,5,5,5), tetV({0,0,0},{8,0,0},{0,8,0},{0,0,8}), makeOctS(4)}; const char* on[3] = {"box5", "tet8", "oct4"};
        Mesh MO[3] = {toMesh(outer[0]), toMesh(outer[1]), toMesh(outer[2])};
        // vertex contact
        for (int n = 0; n < 60;) {
            int o = rnd(3); I3 v = outer[o].v[rnd((int)outer[o].v.size())]; I3 w[3]; bool ok = true;
            for (int i = 0; i < 3; i++) { I3 d = randI(-4, 4); w[i] = {v[0]+d[0], v[1]+d[1], v[2]+d[2]}; if (!strictlyInside(MO[o], w[i])) ok = false; }
            if (!ok || volZero(v, w[0], w[1], w[2])) continue;
            randomMap(M); emitPair("nested_vertex", std::string(on[o]) + "_" + two(n), applyMap(outer[o], M), applyMap(tetV(v, w[0], w[1], w[2]), M), 1); n++;
        }
        // edge contact: two lattice points strictly inside an edge of A
        for (int n = 0; n < 30;) {
            int o = rnd(3); auto edges = edgesOf(outer[o]); auto e = edges[rnd((int)edges.size())];
            I3 p = outer[o].v[e.first], q = outer[o].v[e.second], d = sub(q, p); long long g = std::gcd(std::gcd(std::llabs(d[0]), std::llabs(d[1])), std::llabs(d[2]));
            if (o == 2 && false) {}
            if (g < 3) continue;
            // only true edges of A (not triangulation diagonals): both endpoints must lie on two common face planes
            auto pl = planesOf(outer[o]); int common = 0; for (auto& P : pl) if (dot(P.n, p) == P.d && dot(P.n, q) == P.d) common++;
            if (common < 2) continue;
            long long k1 = 1 + rnd((int)g - 1), k2 = 1 + rnd((int)g - 1); if (k1 == k2) continue;
            I3 a = {p[0]+d[0]/g*k1, p[1]+d[1]/g*k1, p[2]+d[2]/g*k1}, b = {p[0]+d[0]/g*k2, p[1]+d[1]/g*k2, p[2]+d[2]/g*k2};
            I3 c = randI(-1, 8), e2 = randI(-1, 8); if (!strictlyInside(MO[o], c) || !strictlyInside(MO[o], e2) || volZero(a, b, c, e2)) continue;
            randomMap(M); emitPair("nested_edge", std::string(on[o]) + "_" + two(n), applyMap(outer[o], M), applyMap(tetV(a, b, c, e2), M), 1); n++;
        }
        // face contact: three points strictly inside the face z = 0 of the box or tetrahedron, apex strictly inside
        for (int n = 0; n < 30;) {
            int o = rnd(2); I3 a = {1 + rnd(3), 1 + rnd(3), 0}, b = {1 + rnd(3), 1 + rnd(3), 0}, c = {1 + rnd(3), 1 + rnd(3), 0}, d = randI(1, 4);
            if (!strictlyInside(MO[o], d)) continue;
            bool ok = true; for (auto* p : {&a, &b, &c}) { if (!onBoundary(MO[o], *p)) ok = false; else { double q[3] = {(double)(*p)[0], (double)(*p)[1], (double)(*p)[2]}; (void)q; } }
            if (!ok || a == b || a == c || b == c || volZero(a, b, c, d)) continue;
            // the three base vertices must be strictly inside the z = 0 face: x, y >= 1 and (tet) x + y < 8; x,y <= 3 holds by construction
            randomMap(M); emitPair("nested_face", std::string(on[o]) + "_" + two(n), applyMap(outer[o], M), applyMap(tetV(a, b, c, d), M), 1); n++;
        }
    }
    // ---- non-convex vertex-vertex ----
    {
        Shape As[3] = {lPrism(), uPrism(), plusPrism()}; const char* an[3] = {"L", "U", "plus"};
        for (int n = 0; n < 90;) {
            int s = n % 3; Shape& A = As[s]; std::vector<I3> top; for (auto& p : A.v) if (p[2] == 2) top.push_back(p);
            I3 v = top[rnd((int)top.size())]; I3 w[3]; for (int i = 0; i < 3; i++) w[i] = {v[0] + rnd(7) - 3, v[1] + rnd(7) - 3, v[2] + 1 + rnd(3)};
            if (volZero(v, w[0], w[1], w[2])) continue;
            randomMap(M); emitPair("nonconvex_vv", std::string(an[s]) + "_" + two(n), applyMap(A, M), applyMap(tetV(v, w[0], w[1], w[2]), M), 0); n++;
        }
    }
    // ---- three components, B touches one at a vertex (outward-octant construction) ----
    {
        for (int n = 0; n < 60;) {
            long long a = 2 + rnd(3), nt = 4 + rnd(3);
            Shape c0 = boxAt(0, 0, 0, a, a, a), c1 = shifted(tetV({0,0,0},{nt,0,0},{0,nt,0},{0,0,nt}), 40, 0, 0), c2 = shifted(boxAt(0, 0, 0, a + 1, a, a + 2), 0, 40, 0);
            Shape A = joined(joined(c0, c1), c2); int which = rnd(3); I3 v; int sg[3];
            if (which == 1) { int k = rnd(4); I3 vs[4] = {{40,0,0},{40+nt,0,0},{40,nt,0},{40,0,nt}}; int ss[4][3] = {{-1,-1,-1},{1,-1,-1},{-1,1,-1},{-1,-1,1}}; v = vs[k]; for (int i = 0; i < 3; i++) sg[i] = ss[k][i]; }
            else { const Shape& c = which == 0 ? c0 : c2; v = c.v[rnd(8)]; I3 lo = c.v[0], hi = c.v[6]; for (int i = 0; i < 3; i++) { sg[i] = v[i] == hi[i] ? 1 : -1; (void)lo; } }
            I3 w[3]; for (int i = 0; i < 3; i++) w[i] = {v[0] + sg[0] * rnd(4), v[1] + sg[1] * rnd(4), v[2] + sg[2] * rnd(4)};
            if (volZero(v, w[0], w[1], w[2])) continue;
            Shape B = tetV(v, w[0], w[1], w[2]);
            int hits = 0; for (const Shape* pc : {&c0, &c1, &c2}) { int sc = satClass(*pc, B); if (sc == 0) { hits = 99; break; } if (sc == 2) { Contact ct = contactOf(*pc, B); if (!(ct.dim == 0 && ct.fa == 'V' && ct.fb == 'V')) hits = 99; hits++; } }
            if (hits != 1) continue;
            randomMap(M); emitPair("threecomp_vv", two(n), applyMap(A, M), applyMap(B, M), 0); n++;
        }
    }
    // ---- two-point and three-point contact ----
    {
        for (int n = 0; n < 40;) {
            long long a = 2 + rnd(3), w = 2 * (2 + rnd(3)), k = 2 + rnd(2), h = 2 + rnd(2);
            Shape p1 = boxAt(0, 0, 0, a, a, a), p2 = boxAt(a + w, 0, 0, a + w + k, a, a), A = joined(p1, p2); Shape B;
            if (rnd(2)) B = boxAt(a, a, a, a + w, a + h, a + h);
            else B = tetV({a, a, a}, {a + w, a, a}, {a + w / 2, a + 2, a + 1}, {a + w / 2, a + 1, a + 3});
            int pts = 0; bool bad = false; for (const Shape* pc : {&p1, &p2}) { int sc = satClass(*pc, B); if (sc == 0) bad = true; if (sc == 2) { Contact ct = contactOf(*pc, B); if (!(ct.dim == 0 && ct.fa == 'V' && ct.fb == 'V')) bad = true; pts++; } }
            if (bad || pts != 2) continue;
            randomMap(M); emitPair("twopoint", two(n), applyMap(A, M), applyMap(B, M), 0); n++;
        }
        for (int n = 0; n < 30;) {
            long long t = 6 + 2 * rnd(3), a = 2 + rnd(2);
            Shape B = tetV({0,0,0}, {t,0,0}, {0,t,0}, {0,0,t});
            Shape b0 = boxAt(-a, -a, -a, 0, 0, 0), b1 = boxAt(t, -a, -a, t + a, 0, 0), b2 = boxAt(-a, t, -a, 0, t + a, 0), A = joined(joined(b0, b1), b2);
            int pts = 0; bool bad = false; for (const Shape* pc : {&b0, &b1, &b2}) { int sc = satClass(*pc, B); if (sc == 0) bad = true; if (sc == 2) { Contact ct = contactOf(*pc, B); if (!(ct.dim == 0 && ct.fa == 'V' && ct.fb == 'V')) bad = true; pts++; } }
            if (bad || pts != 3) { fprintf(stderr, "threepoint construction check failed\n"); return 1; }
            randomMap(M); emitPair("threepoint", two(n), applyMap(A, M), applyMap(B, M), 0); n++;
        }
    }
    // ---- random convex vertex-vertex placements (sizes not used before) ----
    {
        Shape sh[4] = {makeBoxS(0, 5), makeTetS(6), makeOctS(5), makeBoxS(0, 1)}; const char* nm[4] = {"bx5", "te6", "oc5", "bx1"};
        struct Cand { int ia, ib, mir; I3 t; }; std::vector<Cand> cand;
        for (int ia = 0; ia < 4; ia++) for (int ib = 0; ib < 4; ib++) for (int mir = 0; mir < 2; mir++)
            for (long long x = -9; x <= 9; x++) for (long long y = -9; y <= 9; y++) for (long long z = -9; z <= 9; z++) {
                Shape A = moved(sh[ia], 0, {0,0,0}, ID), B = moved(sh[ib], mir, {x,y,z}, ID);
                if (satClass(A, B) != 2) continue; Contact c = contactOf(A, B); if (c.dim == 0 && c.fa == 'V' && c.fb == 'V') cand.push_back({ia, ib, mir, {x, y, z}});
            }
        fprintf(stderr, "randvv candidates: %zu\n", cand.size());
        for (int i = (int)cand.size() - 1; i > 0; i--) std::swap(cand[i], cand[rnd(i + 1)]);
        int want = std::min<int>(100, (int)cand.size());
        for (int i = 0; i < want; i++) {
            randomMap(M); Shape A = moved(sh[cand[i].ia], 0, {0,0,0}, M), B = moved(sh[cand[i].ib], cand[i].mir, cand[i].t, M);
            if (satClass(A, B) != 2) { fprintf(stderr, "class changed\n"); return 1; }
            emitPair("randvv", std::string(nm[cand[i].ia]) + "_" + nm[cand[i].ib] + (cand[i].mir ? "m" : "") + "_" + two(i), A, B, 0);
        }
    }
    // ---- curved: icospheres. No exact volume oracle ('-'); scored by the existing point tests ----
    {
        const char* none[5] = {"-", "-", "-", "-", "-"};
        for (int sub = 1; sub <= 2; sub++) {
            Mesh S = makeIcosphere(sub, 1.0); size_t nv = S.nv(); std::vector<size_t> pick;
            for (size_t i = 0; i < nv; i++) pick.push_back(i);
            if (sub == 2) { for (size_t i = pick.size() - 1; i > 0; i--) std::swap(pick[i], pick[rnd((int)i + 1)]); pick.resize(40); }
            for (size_t vi : pick) {
                Mesh B; for (size_t i = 0; i < nv; i++) B.addV(2 * S.v[3*vi] - S.v[3*i], 2 * S.v[3*vi+1] - S.v[3*i+1], 2 * S.v[3*vi+2] - S.v[3*i+2]);
                for (size_t k = 0; k < S.nt(); k++) B.addT(S.t[3*k], S.t[3*k+2], S.t[3*k+1]);      // point reflection flips handedness
                emitRaw("sphere_reflect", "s3_sphere_reflect_sub" + std::to_string(sub) + "_v" + std::to_string(vi), S, B, none);
            }
            for (int ax = 0; ax < 3; ax++) for (int sgn = -1; sgn <= 1; sgn += 2) {
                double ext = 0; for (size_t i = 0; i < nv; i++) ext = std::max(ext, sgn * S.v[3*i+ax]);
                double lo[3] = {-1, -1, -1}, hi[3] = {1, 1, 1}; if (sgn > 0) { lo[ax] = ext; hi[ax] = ext + 1; } else { lo[ax] = -ext - 1; hi[ax] = -ext; }
                Mesh Bx = toMesh(boxAt(0, 0, 0, 1, 1, 1)); for (size_t i = 0; i < 8; i++) for (int k = 0; k < 3; k++) Bx.v[3*i+k] = Bx.v[3*i+k] != 0 ? hi[k] : lo[k];
                emitRaw("sphere_box", "s3_sphere_box_sub" + std::to_string(sub) + "_ax" + std::to_string(ax) + (sgn > 0 ? "p" : "n"), S, Bx, none);
            }
        }
    }
    fclose(mf);
    int total = 0; for (auto& kv : counts) { printf("%s: %d pairs\n", kv.first.c_str(), kv.second); total += kv.second; } printf("total %d pairs\n", total);
    return 0;
}
