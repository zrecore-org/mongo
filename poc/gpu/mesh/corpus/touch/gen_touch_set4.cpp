// MP20 set 4: the acceptance family for the patched Geogram (lib/geogram/mp20). Written AFTER geogram_mp20.patch was frozen
// (sha256 in lib/geogram/mp20/FROZEN_SHA256.txt); new seed, new sizes, new classes. Hashes of the output are recorded before the run
// (EXPECTED_SHA256_set4.txt), the run is made once, and the patch is not touched afterwards.
//   gen_touch_set4 <outdir>
// RANGE STATEMENT (written before any run): every coordinate of every polyhedral input is an integer of absolute value < 50000 (spheres: |x| < 4 for A, < 10 for B);
// all inputs are far below the library's fixed 1e6 classification ray length, so the out-of-scope huge-offset defect cannot be involved.
// Expectations: operands that touch only on a set of measure zero have interiors that are disjoint, so union = vA + vB, intersection = 0,
// difference = vA (exact 6*volume dyadics). Nested classes (B inside A): union = vA, intersection = vB, difference = vA - vB.
// EVERY pair is verified exactly by this generator before it is written (a failed check aborts the run, nothing is dropped silently):
//   * polyhedral multi-body classes: every pair of bodies is tested with the exact separating-axis test + exact contact set (satClass, contactOf):
//     bodies of one operand are strictly separated; a body pair of different operands is either strictly separated or in the declared contact
//     graph, and every declared contact is exactly a single vertex-vertex point; no other contact exists.
//   * sliver class: every pair with satClass == 2 (interiors disjoint, closures touch); the exact contact dimension and features are labelled.
//   * curved class (icospheres of different subdivisions and radii): the contact vertex is bit-exact (computed c + r*(u-u)), and an exact
//     separating plane through c with normal u (rational arithmetic on the binary64 inputs) has every A vertex on one side and every B vertex on
//     the other, with equality only at c, so A and B meet at exactly one point. Volumes are the exact dyadic volumes of the binary64 meshes.
//   * nonconvex prisms: tetrahedron whose other three vertices are strictly above the top plane of the prism; each convex piece of the prism is
//     checked against it (touching pieces must meet in a single point, dim 0).
//   * nested classes: strict interior containment of every non-contact vertex by an exact point-in-solid test (harness/pip.h), contact stated
//     exactly (one vertex, two lattice points inside one edge, three lattice points inside one face).
// Classes new in set 4 (not in sets 1-3): curved (icosphere vs icosphere of other subdivision and radius), star3 (one body touching three
// others, in both operand directions), star3deep and path4 (contact trees of 4 and 5 bodies spread over both operands), sliver (needles and
// wafers touching), plus fresh sizes and seeds of nested, nonconvex (T, S, C and staircase prisms), randvv, twopoint, threepoint, threecomp.
#define main gen_touch_unused_main
#include "gen_touch.cpp"
#undef main
#include "../../harness/pip.h"
#include "../../harness/topology.h"
#include <random>
extern "C" void exactinit(void);

static std::mt19937_64 rng(0x5345543447454E31ULL);
static int rnd(int n) { return (int)(rng() % (uint64_t)n); }
static const long long ID[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
static long long det33(const long long M[3][3]) { return M[0][0]*(M[1][1]*M[2][2]-M[1][2]*M[2][1]) - M[0][1]*(M[1][0]*M[2][2]-M[1][2]*M[2][0]) + M[0][2]*(M[1][0]*M[2][1]-M[1][1]*M[2][0]); }
static Shape applyMap(Shape s, const long long M[3][3]) {
    for (auto& p : s.v) { I3 q; for (int i = 0; i < 3; i++) q[i] = M[i][0]*p[0] + M[i][1]*p[1] + M[i][2]*p[2]; p = q; }
    long long d = det33(M);
    if (d != 1 && d != -1) { fprintf(stderr, "map not unimodular\n"); exit(1); }
    if (d < 0) for (auto& f : s.t) std::swap(f[1], f[2]);
    return s;
}
static void randomMap(long long M[3][3], bool allowIdentity = true) {
    if (allowIdentity && rnd(5) == 0) { memcpy(M, ID, sizeof(long long) * 9); return; }
    int perm[3] = {0, 1, 2}; for (int i = 2; i > 0; i--) std::swap(perm[i], perm[rnd(i + 1)]);
    long long R[3][3] = {{0,0,0},{0,0,0},{0,0,0}}; for (int i = 0; i < 3; i++) R[i][perm[i]] = rnd(2) ? 1 : -1;
    int ns = rnd(4);
    for (int k = 0; k < ns; k++) {
        int i = rnd(3), j = (i + 1 + rnd(2)) % 3; long long c = (rnd(2) ? 1 : -1) * (1 + rnd(3));
        long long E[3][3] = {{1,0,0},{0,1,0},{0,0,1}}; E[i][j] = c; long long T[3][3];
        for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) { T[a][b] = 0; for (int m = 0; m < 3; m++) T[a][b] += E[a][m] * R[m][b]; }
        memcpy(R, T, sizeof R);
    }
    memcpy(M, R, sizeof R);
}
static Shape joined(const Shape& a, const Shape& b) { Shape s = a; int o = (int)a.v.size(); for (auto& p : b.v) s.v.push_back(p); for (auto f : b.t) s.t.push_back({f[0]+o, f[1]+o, f[2]+o}); return s; }
static Shape shifted(Shape s, long long dx, long long dy, long long dz) { for (auto& p : s.v) { p[0] += dx; p[1] += dy; p[2] += dz; } return s; }
static Shape tetV(I3 a, I3 b, I3 c, I3 d) { Shape s; s.v = {a, b, c, d}; s.t = {{0,2,1},{0,1,3},{0,3,2},{1,2,3}}; orient(s); return s; }
static Shape boxAt(long long x0, long long y0, long long z0, long long x1, long long y1, long long z1) {
    Shape s = makeBoxS(0, 1); for (auto& p : s.v) { p[0] = p[0] ? x1 : x0; p[1] = p[1] ? y1 : y0; p[2] = p[2] ? z1 : z0; } orient(s); return s; }
static Shape octAxes(long long a, long long b, long long c) {
    Shape s; s.v = {{a,0,0},{-a,0,0},{0,b,0},{0,-b,0},{0,0,c},{0,0,-c}};
    for (int sx = 0; sx < 2; sx++) for (int sy = 0; sy < 2; sy++) for (int sz = 0; sz < 2; sz++) s.t.push_back({sx, 2 + sy, 4 + sz});
    orient(s); return s;
}
static bool volZero(const I3& a, const I3& b, const I3& c, const I3& d) { return det3(sub(b, a), sub(c, a), sub(d, a)) == 0; }
static int maxAbs(const Shape& s) { long long m = 0; for (auto& p : s.v) for (int k = 0; k < 3; k++) m = std::max(m, std::llabs(p[k])); return (int)m; }

static std::string outdir; static FILE* mf; static std::map<std::string, int> counts;
static void emitRaw(const std::string& cls, const std::string& id, const std::string& extraTag, const Mesh& MA, const Mesh& MB, const std::string ex[5]) {
    writeMesh(outdir + "/adversarial/" + id + "_A.msh", MA); writeMesh(outdir + "/adversarial/" + id + "_B.msh", MB);
    fprintf(mf, "%s\ttouch\tvalid,set4=%s%s%s\t%zu\t%zu\t%zu\t%zu", id.c_str(), cls.c_str(), extraTag.empty() ? "" : ",", extraTag.c_str(), MA.nv(), MA.nt(), MB.nv(), MB.nt());
    for (int i = 0; i < 5; i++) fprintf(mf, "\t%s", ex[i].c_str()); fprintf(mf, "\n"); counts[cls]++;
}
// mode 0: closures meet on a set of measure zero (additive); mode 1: B inside A
static void emitMeshes(const std::string& cls, const std::string& name, const std::string& tag, const Mesh& MA, const Mesh& MB, int mode) {
    Dyadic vA = exactVolumeTimes6(MA), vB = exactVolumeTimes6(MB);
    if (signExact(vA) <= 0 || signExact(vB) <= 0) { fprintf(stderr, "non-positive volume %s\n", name.c_str()); exit(1); }
    if (!analyzeTopology(MA).closedManifold() || !analyzeTopology(MB).closedManifold()) { fprintf(stderr, "not closed manifold %s\n", name.c_str()); exit(1); }
    Dyadic z{BigInt(0), 0}, U = mode ? vA : addExact(vA, vB), I = mode ? vB : z, D = mode ? subExact(vA, vB) : vA;
    std::string s[5] = {toString(vA), toString(vB), toString(U), toString(I), toString(D)};
    emitRaw(cls, "s4_" + cls + "_" + name, tag, MA, MB, s);
}
static void emitPair(const std::string& cls, const std::string& name, const std::string& tag, const Shape& A, const Shape& B, int mode) {
    if (maxAbs(A) >= 50000 || maxAbs(B) >= 50000) { fprintf(stderr, "range statement violated %s\n", name.c_str()); exit(1); }
    emitMeshes(cls, name, tag, toMesh(A), toMesh(B), mode);
}
static std::string three(int n) { char b[16]; snprintf(b, sizeof b, "%03d", n); return b; }
static bool strictlyInside(const Mesh& MA, const I3& p) { double q[3] = {(double)p[0], (double)p[1], (double)p[2]}; Pip r = classifyPoint(MA, q); return !r.boundary && r.inside; }
static I3 randI(int lo, int hi) { return {lo + rnd(hi - lo + 1), lo + rnd(hi - lo + 1), lo + rnd(hi - lo + 1)}; }

// ---------------- random convex bodies ----------------
// kind 0 box, 1 tetrahedron, 2 octahedron, 3 sliver (flat tetrahedron / needle / wafer: strictly positive exact volume, extreme aspect ratio)
static int sliverBase = 20, sliverSpan = 180;
static Shape randBody(int kind) {
    if (kind == 0) return boxAt(0, 0, 0, 1 + rnd(5), 1 + rnd(5), 1 + rnd(5));
    if (kind == 1) { long long a = 1 + rnd(6), b = 1 + rnd(6), c = 1 + rnd(6); return tetV({0,0,0}, {a,0,0}, {0,b,0}, {0,0,c}); }
    if (kind == 2) return octAxes(1 + rnd(4), 1 + rnd(4), 1 + rnd(4));
    long long L = sliverBase + rnd(sliverSpan);
    switch (rnd(4)) {
        case 0: return tetV({0,0,0}, {L,0,0}, {0,L,0}, {rnd(L / 2) + 1, rnd(L / 2) + 1, 1});                  // flat sliver, height 1
        case 1: return tetV({0,0,0}, {L,0,0}, {0,1,0}, {0,0,1});                                              // needle
        case 2: return boxAt(0, 0, 0, L, L, 1);                                                                 // wafer
        default: return tetV({0,0,0}, {L,1,0}, {2,L,1}, {1,2,L});                                              // skew needle-ish sliver
    }
}
static Shape randOriented(int kind) { long long M[3][3]; randomMap(M, false); return applyMap(randBody(kind), M); }

// ---------------- exact contact graph checks ----------------
struct Body { Shape s; int op; };
static bool isVV(const Shape& a, const Shape& b) { Contact c = contactOf(a, b); return c.dim == 0 && c.npts == 1 && c.fa == 'V' && c.fb == 'V'; }
// edges: set of (i<j) body index pairs that must touch in exactly one vertex-vertex point; all other pairs must be strictly separated.
static bool checkGraph(const std::vector<Body>& B, const std::set<std::pair<int, int>>& edges) {
    for (size_t i = 0; i < B.size(); i++) for (size_t j = i + 1; j < B.size(); j++) {
        int sc = satClass(B[i].s, B[j].s); bool want = edges.count({(int)i, (int)j}) > 0;
        if (want) { if (sc != 2 || !isVV(B[i].s, B[j].s)) return false; }
        else if (sc != 1) return false;
    }
    return true;
}
// Bipartite contact tree: body k (k >= 1) is attached to body parent[k] (which must be in the other operand) by a vertex-vertex contact.
// floaters: extra bodies placed far away (strictly separated from everything). Returns false if the random placement fails the exact check.
struct TreeSpec { const char* name; std::vector<int> op, parent; std::vector<int> kinds; int floaters; };   // kinds: -1 = random 0..2
static bool buildTree(const TreeSpec& sp, std::vector<Body>& out, std::set<std::pair<int, int>>& edges, int sliverKind) {
    out.clear(); edges.clear(); int n = (int)sp.op.size();
    std::vector<std::set<int>> usedVerts(n);
    for (int k = 0; k < n; k++) {
        int kind = sp.kinds[k] >= 0 ? sp.kinds[k] : rnd(3); if (sliverKind && k == n - 1 && sp.parent[k] >= 0) kind = 3;
        Shape s = randOriented(kind);
        if (sp.parent[k] < 0) { out.push_back({s, sp.op[k]}); continue; }
        int p = sp.parent[k]; if (sp.op[k] == sp.op[p]) { fprintf(stderr, "tree spec joins one operand\n"); exit(1); }
        int pv = rnd((int)out[p].s.v.size()); if (usedVerts[p].count(pv)) return false; usedVerts[p].insert(pv);
        int sv = rnd((int)s.v.size()); I3 d = sub(out[p].s.v[pv], s.v[sv]);
        out.push_back({shifted(s, d[0], d[1], d[2]), sp.op[k]}); edges.insert({p, k}); usedVerts[k].insert(sv);
    }
    for (int f = 0; f < sp.floaters; f++) {
        Shape s = randOriented(rnd(3)); long long off = 120 * (f + 1); int ax = rnd(3); int sg = rnd(2) ? 1 : -1;
        out.push_back({shifted(s, ax == 0 ? sg * off : 0, ax == 1 ? sg * off : 0, ax == 2 ? sg * off : 0), 0});
    }
    return checkGraph(out, edges);
}
static void emitTree(const TreeSpec& sp, int count, int sliverKind) {
    for (int n = 0; n < count;) {
        std::vector<Body> bodies; std::set<std::pair<int, int>> edges; bool ok = false;
        for (int tries = 0; tries < 4000 && !ok; tries++) ok = buildTree(sp, bodies, edges, sliverKind);
        if (!ok) { fprintf(stderr, "tree %s: no valid placement\n", sp.name); exit(1); }
        long long M[3][3]; randomMap(M);
        for (auto& b : bodies) b.s = applyMap(b.s, M);
        if (!checkGraph(bodies, edges)) { fprintf(stderr, "tree %s: graph check failed after map\n", sp.name); exit(1); }
        Shape A, B; bool haveA = false, haveB = false;
        for (auto& b : bodies) { if (b.op == 0) { A = haveA ? joined(A, b.s) : b.s; haveA = true; } else { B = haveB ? joined(B, b.s) : b.s; haveB = true; } }
        char tag[64]; snprintf(tag, sizeof tag, "bodies=%zu,contacts=%zu", bodies.size(), edges.size());
        emitPair(sp.name, three(n), tag, A, B, 0); n++;
    }
}

// ---------------- polygons, prisms ----------------
static Shape prism(const std::vector<std::array<long long, 2>>& P, const std::vector<std::array<int, 3>>& tri, long long z0, long long z1) {
    Shape s; int n = (int)P.size();
    for (auto& p : P) s.v.push_back({p[0], p[1], z0}); for (auto& p : P) s.v.push_back({p[0], p[1], z1});
    for (auto& t : tri) { s.t.push_back({t[0], t[2], t[1]}); s.t.push_back({n+t[0], n+t[1], n+t[2]}); }
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; s.t.push_back({i, j, n+j}); s.t.push_back({i, n+j, n+i}); }
    return s;
}
// ear clipping for a simple CCW polygon with no collinear consecutive vertices
static std::vector<std::array<int, 3>> earClip(const std::vector<std::array<long long, 2>>& P) {
    std::vector<int> idx; for (int i = 0; i < (int)P.size(); i++) idx.push_back(i);
    auto cr = [&](int a, int b, int c) { return (P[b][0]-P[a][0])*(P[c][1]-P[a][1]) - (P[b][1]-P[a][1])*(P[c][0]-P[a][0]); };
    std::vector<std::array<int, 3>> out;
    while (idx.size() > 3) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size() && !clipped; i++) {
            int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
            if (cr(a, b, c) <= 0) continue;
            bool inside = false;
            for (int q : idx) { if (q == a || q == b || q == c) continue; if (cr(a, b, q) >= 0 && cr(b, c, q) >= 0 && cr(c, a, q) >= 0) inside = true; }
            if (inside) continue;
            out.push_back({a, b, c}); idx.erase(idx.begin() + i); clipped = true;
        }
        if (!clipped) { fprintf(stderr, "ear clipping failed\n"); exit(1); }
    }
    out.push_back({idx[0], idx[1], idx[2]}); return out;
}
struct Prism { const char* name; std::vector<std::array<long long, 2>> P; std::vector<std::array<long long, 4>> boxes; };   // boxes: x0,y0,x1,y1 (convex pieces)

// ---------------- icospheres (same construction as gen_corpus.cpp: + - * / sqrt only) ----------------
static Mesh makeIcosphere(int sub) {
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
    return m;
}
// exact sign of sum_i n_i * (q_i - c_i) for binary64 inputs
static int sideOfPlane(const double* n, const double* q, const double* c) {
    BigInt v[9]; if (!toCommonInts({n[0], n[1], n[2], q[0], q[1], q[2], c[0], c[1], c[2]}, v)) { fprintf(stderr, "non-finite\n"); exit(1); }
    BigInt s = v[0] * (v[3] - v[6]) + v[1] * (v[4] - v[7]) + v[2] * (v[5] - v[8]); return s > 0 ? 1 : s < 0 ? -1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2; exactinit();
    outdir = argv[1]; mkdir(outdir.c_str(), 0755); mkdir((outdir + "/adversarial").c_str(), 0755);
    mf = fopen((outdir + "/manifest.tsv").c_str(), "wb");
    fprintf(mf, "id\tclass\ttags\tnvA\tntA\tnvB\tntB\tvol6_A\tvol6_B\tvol6_union\tvol6_inter\tvol6_diff\n");
    long long M[3][3];

    // ---- 1. multi-body contact trees (op 0 = A, 1 = B) ----
    emitTree({"star3",      {0,1,1,1},       {-1,0,0,0},       {-1,-1,-1,-1}, 0}, 110, 0);      // one A body touches three B bodies
    emitTree({"star3r",     {1,0,0,0},       {-1,0,0,0},       {-1,-1,-1,-1}, 0}, 70, 0);       // one B body touches three A bodies
    emitTree({"twopoint4",  {1,0,0},         {-1,0,0},         {-1,-1,-1},    0}, 80, 0);       // B bridges two A bodies
    emitTree({"path4",      {0,1,0,1},       {-1,0,1,2},       {-1,-1,-1,-1}, 0}, 90, 0);       // chain A-B-A-B
    emitTree({"star3deep",  {0,1,1,1,0},     {-1,0,0,0,1},     {-1,-1,-1,-1,-1}, 0}, 70, 0);   // A touches three B bodies, one of them touches a second A body
    emitTree({"threepoint4",{1,0,0,0},       {-1,0,0,0},       {-1,-1,-1,-1}, 0}, 50, 0);       // B body touches three A bodies (new sizes/shapes)
    emitTree({"threecomp4", {0,1},           {-1,0},           {-1,-1},       2}, 60, 0);       // A = three bodies (one touches B), B = one body

    // ---- 2. sliver contact: the last attached body is a needle / flat tetrahedron / wafer ----
    emitTree({"sliver_vv",  {0,1},           {-1,0},           {-1,-1},       0}, 40, 1);       // normal body in A, sliver in B, vertex-vertex
    emitTree({"sliver_star",{0,1,1,1},       {-1,0,0,0},       {-1,-1,-1,-1}, 0}, 30, 1);       // central body, two normal and one sliver neighbour
    {   // sliver vs convex body by translation search, any exact contact class (point, edge, face)
        sliverBase = 6; sliverSpan = 8;
        for (int n = 0; n < 40;) {
            Shape A = randOriented(rnd(3)), B = randOriented(3); int range = 14; I3 t = randI(-range, range);
            Shape Bt = shifted(B, t[0], t[1], t[2]); if (satClass(A, Bt) != 2) continue;
            Contact c = contactOf(A, Bt); char tag[64]; snprintf(tag, sizeof tag, "contact=d%d-%c%c", c.dim, c.fa, c.fb);
            randomMap(M); emitPair("sliver_any", three(n), tag, applyMap(A, M), applyMap(Bt, M), 0); n++;
        }
        sliverBase = 20; sliverSpan = 180;
    }

    // ---- 3. curved: icosphere vs icosphere of another subdivision and radius, meeting at one bit-exact vertex ----
    {
        static Mesh S[4]; for (int s = 1; s <= 3; s++) S[s] = makeIcosphere(s);
        const double radii[6] = {1.0, 0.5, 0.75, 1.5, 2.0, 3.0};
        for (int n = 0; n < 100;) {
            int sa = 1 + rnd(3), sb = 1 + rnd(3); if (sa == 1 && sb == 1) continue;
            double r1 = radii[rnd(6)], r2 = radii[rnd(6)];
            size_t nu = std::min(S[sa].nv(), S[sb].nv()); size_t ui = (size_t)rnd((int)nu);
            const double* u = &S[sa].v[3 * ui]; double un[3] = {S[sb].v[3*ui], S[sb].v[3*ui+1], S[sb].v[3*ui+2]};
            if (u[0] != un[0] || u[1] != un[1] || u[2] != un[2]) { fprintf(stderr, "vertex %zu differs between subdivisions\n", ui); return 1; }
            double un3[3] = {u[0], u[1], u[2]};
            double c[3] = {r1 * un3[0], r1 * un3[1], r1 * un3[2]};
            Mesh A, B; for (size_t i = 0; i < S[sa].nv(); i++) A.addV(r1 * S[sa].v[3*i], r1 * S[sa].v[3*i+1], r1 * S[sa].v[3*i+2]);
            for (size_t k = 0; k < S[sa].nt(); k++) A.addT(S[sa].t[3*k], S[sa].t[3*k+1], S[sa].t[3*k+2]);
            for (size_t i = 0; i < S[sb].nv(); i++) {
                if (i == ui) { B.addV(c[0], c[1], c[2]); continue; }
                B.addV(c[0] + r2 * (un3[0] - S[sb].v[3*i]), c[1] + r2 * (un3[1] - S[sb].v[3*i+1]), c[2] + r2 * (un3[2] - S[sb].v[3*i+2]));
            }
            for (size_t k = 0; k < S[sb].nt(); k++) B.addT(S[sb].t[3*k], S[sb].t[3*k+2], S[sb].t[3*k+1]);     // point reflection flips handedness
            // exact check: plane through c with normal u; A on the non-positive side, B on the non-negative side, equality only at c itself
            bool ok = true; int onPlaneA = 0, onPlaneB = 0;
            for (size_t i = 0; i < A.nv() && ok; i++) { int s = sideOfPlane(un3, &A.v[3*i], c); if (s > 0) ok = false; if (s == 0) onPlaneA++; }
            for (size_t i = 0; i < B.nv() && ok; i++) { int s = sideOfPlane(un3, &B.v[3*i], c); if (s < 0) ok = false; if (s == 0) onPlaneB++; }
            if (!ok || onPlaneA != 1 || onPlaneB != 1) continue;        // contact must be exactly the single shared vertex
            if (B.v[3*ui] != c[0] || B.v[3*ui+1] != c[1] || B.v[3*ui+2] != c[2] || A.v[3*ui] != c[0] || A.v[3*ui+1] != c[1] || A.v[3*ui+2] != c[2]) { fprintf(stderr, "contact vertex not bit-exact\n"); return 1; }
            for (double x : A.v) if (std::fabs(x) >= 4) { fprintf(stderr, "sphere range\n"); return 1; }
            for (double x : B.v) if (std::fabs(x) >= 10) { fprintf(stderr, "sphere range\n"); return 1; }
            char tag[96]; snprintf(tag, sizeof tag, "sub=%d/%d,r=%g/%g", sa, sb, r1, r2);
            emitMeshes("curved", three(n) + "_s" + std::to_string(sa) + std::to_string(sb) + "_v" + std::to_string(ui), tag, A, B, 0); n++;
        }
    }

    // ---- 4. nonconvex prisms (T, S, C, staircase) with a tetrahedron on a top vertex ----
    {
        std::vector<Prism> PR = {
            {"T",     {{0,0},{6,0},{6,2},{4,2},{4,6},{2,6},{2,2},{0,2}},               {{0,0,6,2},{2,2,4,6}}},
            {"S",     {{0,0},{4,0},{4,2},{6,2},{6,4},{2,4},{2,2},{0,2}},               {{0,0,4,2},{2,2,6,4}}},
            {"C",     {{0,0},{6,0},{6,2},{2,2},{2,4},{6,4},{6,6},{0,6}},               {{0,0,6,2},{0,2,2,4},{0,4,6,6}}},
            {"stair", {{0,0},{6,0},{6,2},{4,2},{4,4},{2,4},{2,6},{0,6}},               {{0,0,6,2},{0,2,4,4},{0,4,2,6}}}};
        for (int n = 0; n < 100;) {
            Prism& p = PR[n % 4]; Shape A = prism(p.P, earClip(p.P), 0, 2); std::vector<Shape> pieces; for (auto& b : p.boxes) pieces.push_back(boxAt(b[0], b[1], 0, b[2], b[3], 2));
            int vi = rnd((int)p.P.size()); I3 v = {p.P[vi][0], p.P[vi][1], 2}; I3 w[3];
            for (int i = 0; i < 3; i++) w[i] = {v[0] + rnd(9) - 4, v[1] + rnd(9) - 4, 3 + rnd(3)};
            if (volZero(v, w[0], w[1], w[2])) continue;
            Shape B = tetV(v, w[0], w[1], w[2]); bool ok = true; int touching = 0;
            for (auto& pc : pieces) { int sc = satClass(pc, B); if (sc == 0) ok = false; if (sc == 2) { touching++; Contact c = contactOf(pc, B); if (c.dim != 0 || c.npts != 1) ok = false; } }
            if (!ok || touching < 1) continue;
            randomMap(M); emitPair("nonconvex", std::string(p.name) + "_" + three(n), "contact=top_vertex", applyMap(A, M), applyMap(B, M), 0); n++;
        }
    }

    // ---- 5. nested (B inside A), new outer sizes ----
    {
        Shape outer[3] = {boxAt(0,0,0,7,7,7), tetV({0,0,0},{10,0,0},{0,10,0},{0,0,10}), octAxes(6, 6, 6)}; const char* on[3] = {"box7", "tet10", "oct6"};
        Mesh MO[3] = {toMesh(outer[0]), toMesh(outer[1]), toMesh(outer[2])};
        for (int n = 0; n < 50;) {                                  // one shared vertex
            int o = rnd(3); I3 v = outer[o].v[rnd((int)outer[o].v.size())]; I3 w[3]; bool ok = true;
            for (int i = 0; i < 3; i++) { I3 d = randI(-5, 5); w[i] = {v[0]+d[0], v[1]+d[1], v[2]+d[2]}; if (!strictlyInside(MO[o], w[i])) ok = false; }
            if (!ok || volZero(v, w[0], w[1], w[2])) continue;
            randomMap(M); emitPair("nested_vertex", std::string(on[o]) + "_" + three(n), "contact=vertex", applyMap(outer[o], M), applyMap(tetV(v, w[0], w[1], w[2]), M), 1); n++;
        }
        for (int n = 0; n < 25;) {                                  // B shares a sub-segment of an edge of A (two lattice points strictly inside the edge)
            int o = rnd(3); auto edges = edgesOf(outer[o]); auto e = edges[rnd((int)edges.size())];
            I3 p = outer[o].v[e.first], q = outer[o].v[e.second], d = sub(q, p); long long g = std::gcd(std::gcd(std::llabs(d[0]), std::llabs(d[1])), std::llabs(d[2]));
            if (g < 3) continue;
            auto pl = planesOf(outer[o]); int common = 0; for (auto& P : pl) if (dot(P.n, p) == P.d && dot(P.n, q) == P.d) common++;
            if (common < 2) continue;                               // a true edge of A, not a diagonal of a face
            long long k1 = 1 + rnd((int)g - 1), k2 = 1 + rnd((int)g - 1); if (k1 == k2) continue;
            I3 a = {p[0]+d[0]/g*k1, p[1]+d[1]/g*k1, p[2]+d[2]/g*k1}, b = {p[0]+d[0]/g*k2, p[1]+d[1]/g*k2, p[2]+d[2]/g*k2};
            I3 c = randI(0, 7), e2 = randI(0, 7); if (!strictlyInside(MO[o], c) || !strictlyInside(MO[o], e2) || volZero(a, b, c, e2)) continue;
            randomMap(M); emitPair("nested_edge", std::string(on[o]) + "_" + three(n), "contact=edge", applyMap(outer[o], M), applyMap(tetV(a, b, c, e2), M), 1); n++;
        }
        for (int n = 0; n < 25;) {                                  // three base vertices strictly inside the face z = 0 (box7, tet10), apex strictly inside
            int o = rnd(2); I3 a = {1 + rnd(4), 1 + rnd(4), 0}, b = {1 + rnd(4), 1 + rnd(4), 0}, c = {1 + rnd(4), 1 + rnd(4), 0}, d = randI(1, 5);
            if (!strictlyInside(MO[o], d) || a == b || a == c || b == c || volZero(a, b, c, d)) continue;
            if (o == 1 && (a[0]+a[1] >= 10 || b[0]+b[1] >= 10 || c[0]+c[1] >= 10)) continue;
            randomMap(M); emitPair("nested_face", std::string(on[o]) + "_" + three(n), "contact=face", applyMap(outer[o], M), applyMap(tetV(a, b, c, d), M), 1); n++;
        }
    }

    // ---- 6. random convex vertex-vertex placements, sizes not used in sets 1-3 ----
    {
        Shape sh[5] = {boxAt(0,0,0,3,3,3), tetV({0,0,0},{5,0,0},{0,5,0},{0,0,5}), octAxes(3,3,3), tetV({0,0,0},{3,0,0},{0,3,0},{0,0,3}), boxAt(0,0,0,4,2,3)};
        const char* nm[5] = {"bx3", "te5", "oc3", "te3", "bx423"};
        struct Cand { int ia, ib, mir; I3 t; }; std::vector<Cand> cand;
        for (int ia = 0; ia < 5; ia++) for (int ib = 0; ib < 5; ib++) for (int mir = 0; mir < 2; mir++)
            for (long long x = -8; x <= 8; x++) for (long long y = -8; y <= 8; y++) for (long long z = -8; z <= 8; z++) {
                Shape B = sh[ib]; if (mir) for (auto& p : B.v) for (int k = 0; k < 3; k++) p[k] = -p[k];
                orient(B); B = shifted(B, x, y, z);
                if (satClass(sh[ia], B) != 2) continue; if (isVV(sh[ia], B)) cand.push_back({ia, ib, mir, {x, y, z}});
            }
        fprintf(stderr, "randvv candidates: %zu\n", cand.size());
        for (int i = (int)cand.size() - 1; i > 0; i--) std::swap(cand[i], cand[rnd(i + 1)]);
        int want = std::min<int>(110, (int)cand.size());
        for (int i = 0; i < want; i++) {
            Shape B = sh[cand[i].ib]; if (cand[i].mir) for (auto& p : B.v) for (int k = 0; k < 3; k++) p[k] = -p[k];
            orient(B); B = shifted(B, cand[i].t[0], cand[i].t[1], cand[i].t[2]);
            randomMap(M); Shape A2 = applyMap(sh[cand[i].ia], M), B2 = applyMap(B, M);
            if (satClass(A2, B2) != 2 || !isVV(A2, B2)) { fprintf(stderr, "randvv class changed\n"); return 1; }
            emitPair("randvv", std::string(nm[cand[i].ia]) + "_" + nm[cand[i].ib] + (cand[i].mir ? "m" : "") + "_" + three(i), "contact=VV", A2, B2, 0);
        }
    }
    fclose(mf);
    int total = 0; for (auto& kv : counts) { printf("%s: %d pairs\n", kv.first.c_str(), kv.second); total += kv.second; } printf("total %d pairs\n", total);
    return 0;
}
