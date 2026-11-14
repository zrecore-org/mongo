// MP18 held-out family generator (new file; gen_touch.cpp and gen_corpus.cpp are not modified).
//   gen_touch_heldout <outdir>
// Classes (tag heldout=<class>): nested_vertex / nested_edge / nested_face (smaller tetrahedron inside a box or large tetrahedron, sharing exactly
// one vertex / one edge / one face), nonconvex_vv (L- and U-shaped prisms, vertex-vertex), threecomp_vv (A = two disjoint boxes, B touches one at a
// vertex), randvv (>= 300 convex vertex-vertex placements under random signed axis permutations and integer shears, sizes not used in gen_touch),
// twovertex (A = two disjoint boxes, B touches both at one vertex each). All solids have integer coordinates; expected volumes are exact.
// Verification inside the generator: convex touching pairs use the exact SAT + contact-set code of gen_touch.cpp; nested pairs check every B vertex
// of the (convex) outer solid with exact point-in-solid and count the boundary ones; non-convex pairs check a separating coordinate plane.
#define main gen_touch_unused_main
#include "gen_touch.cpp"
#undef main
#include "../../harness/pip.h"
#include "../../harness/topology.h"
#include <random>

extern "C" void exactinit(void);
static std::mt19937_64 rng(0x4D5031385F48454CULL);   // fixed seed, never used by gen_touch
static int rnd(int n) { return (int)(rng() % (uint64_t)n); }

static Mesh shapeMesh(const Shape& s) { return toMesh(s); }
static Shape applyMap(Shape s, const long long M[3][3]) {
    for (auto& p : s.v) { I3 q; for (int i = 0; i < 3; i++) q[i] = M[i][0]*p[0] + M[i][1]*p[1] + M[i][2]*p[2]; p = q; }
    long long d = M[0][0]*(M[1][1]*M[2][2]-M[1][2]*M[2][1]) - M[0][1]*(M[1][0]*M[2][2]-M[1][2]*M[2][0]) + M[0][2]*(M[1][0]*M[2][1]-M[1][1]*M[2][0]);
    if (d < 0) for (auto& f : s.t) std::swap(f[1], f[2]);
    if (d != 1 && d != -1) { fprintf(stderr, "map not unimodular\n"); exit(1); }
    return s;
}
static Shape joined(const Shape& a, const Shape& b) { Shape s = a; int o = (int)a.v.size(); for (auto& p : b.v) s.v.push_back(p); for (auto f : b.t) s.t.push_back({f[0]+o, f[1]+o, f[2]+o}); return s; }
static Shape prism(const std::vector<std::array<long long, 2>>& P, const std::vector<std::array<int, 3>>& tri, long long z0, long long z1) {
    Shape s; int n = (int)P.size();
    for (auto& p : P) s.v.push_back({p[0], p[1], z0});
    for (auto& p : P) s.v.push_back({p[0], p[1], z1});
    for (auto& t : tri) { s.t.push_back({t[0], t[2], t[1]}); s.t.push_back({n+t[0], n+t[1], n+t[2]}); }
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; s.t.push_back({i, j, n+j}); s.t.push_back({i, n+j, n+i}); }
    return s;
}
static Shape lPrism() { return prism({{0,0},{4,0},{4,2},{2,2},{2,4},{0,4}}, {{0,1,2},{0,2,3},{0,3,4},{0,4,5}}, 0, 2); }
static Shape uPrism() { return prism({{0,0},{6,0},{6,4},{4,4},{4,2},{2,2},{2,4},{0,4}}, {{0,1,4},{0,4,5},{1,2,3},{1,3,4},{0,5,6},{0,6,7}}, 0, 2); }
static Shape tetV(I3 a, I3 b, I3 c, I3 d) { Shape s; s.v = {a, b, c, d}; s.t = {{0,2,1},{0,1,3},{0,3,2},{1,2,3}}; orient(s); return s; }
static Shape boxAt(long long x0, long long y0, long long z0, long long x1, long long y1, long long z1) {
    Shape s = makeBoxS(0, 1); for (auto& p : s.v) { p[0] = p[0] ? x1 : x0; p[1] = p[1] ? y1 : y0; p[2] = p[2] ? z1 : z0; } orient(s); return s; }

static const long long ID[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
static void randomMap(long long M[3][3]) {
    int perm[3] = {0, 1, 2}; for (int i = 2; i > 0; i--) std::swap(perm[i], perm[rnd(i + 1)]);
    long long P[3][3] = {{0,0,0},{0,0,0},{0,0,0}}; for (int i = 0; i < 3; i++) P[i][perm[i]] = rnd(2) ? 1 : -1;
    long long R[3][3]; memcpy(R, P, sizeof R);
    int ns = 1 + rnd(3);
    for (int k = 0; k < ns; k++) {
        int i = rnd(3), j = (i + 1 + rnd(2)) % 3; long long c = (rnd(2) ? 1 : -1) * (1 + rnd(2));
        long long E[3][3] = {{1,0,0},{0,1,0},{0,0,1}}; E[i][j] = c;
        long long T[3][3]; for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) { T[a][b] = 0; for (int m = 0; m < 3; m++) T[a][b] += E[a][m] * R[m][b]; }
        memcpy(R, T, sizeof R);
    }
    memcpy(M, R, sizeof R);
}

static std::string outdir; static FILE* mf; static std::map<std::string, int> counts;
static void emitPair(const std::string& cls, const std::string& name, const Shape& A, const Shape& B, int mode /*0 disjoint interiors, 1 B nested in A*/) {
    Mesh MA = toMesh(A), MB = toMesh(B); Dyadic vA = exactVolumeTimes6(MA), vB = exactVolumeTimes6(MB);
    if (signExact(vA) <= 0 || signExact(vB) <= 0) { fprintf(stderr, "non-positive volume %s\n", name.c_str()); exit(1); }
    Topology ta = analyzeTopology(MA), tb = analyzeTopology(MB);
    if (!ta.closedManifold() || !tb.closedManifold()) { fprintf(stderr, "operand not closed manifold %s\n", name.c_str()); exit(1); }
    Dyadic z{BigInt(0), 0}, U = mode ? vA : addExact(vA, vB), I = mode ? vB : z, D = mode ? subExact(vA, vB) : vA;
    std::string id = "held_" + cls + "_" + name;
    writeMesh(outdir + "/adversarial/" + id + "_A.msh", MA); writeMesh(outdir + "/adversarial/" + id + "_B.msh", MB);
    fprintf(mf, "%s\ttouch\tvalid,heldout=%s\t%zu\t%zu\t%zu\t%zu\t%s\t%s\t%s\t%s\t%s\n", id.c_str(), cls.c_str(), MA.nv(), MA.nt(), MB.nv(), MB.nt(),
            toString(vA).c_str(), toString(vB).c_str(), toString(U).c_str(), toString(I).c_str(), toString(D).c_str());
    counts[cls]++;
}
static std::vector<long long> flat(const Shape& s) { std::vector<long long> o; for (auto& p : s.v) for (int k = 0; k < 3; k++) o.push_back(p[k]); return o; }

int main(int argc, char** argv) {
    if (argc < 2) return 2; exactinit();
    outdir = argv[1]; mkdir(outdir.c_str(), 0755); mkdir((outdir + "/adversarial").c_str(), 0755);
    mf = fopen((outdir + "/manifest.tsv").c_str(), "wb");
    fprintf(mf, "id\tclass\ttags\tnvA\tntA\tnvB\tntB\tvol6_A\tvol6_B\tvol6_union\tvol6_inter\tvol6_diff\n");
    std::vector<std::array<long long, 9>> maps; // maps used by the non-random classes: identity plus two fixed-seed random maps each
    auto mapsFor = [&](int n) { std::vector<std::array<long long, 9>> v; v.push_back({1,0,0,0,1,0,0,0,1}); for (int i = 1; i < n; i++) { long long M[3][3]; randomMap(M); std::array<long long, 9> a; for (int k = 0; k < 9; k++) a[k] = M[k/3][k%3]; v.push_back(a); } return v; };
    auto M3 = [](const std::array<long long, 9>& a, long long M[3][3]) { for (int k = 0; k < 9; k++) M[k/3][k%3] = a[k]; };

    // ---- (1) nested: B strictly inside the convex A except for the stated contact ----
    {
        Shape outer[2] = {boxAt(0,0,0,4,4,4), tetV({0,0,0},{6,0,0},{0,6,0},{0,0,6})};
        const char* on[2] = {"box", "tet"};
        struct Inner { const char* cls; Shape s; int boundaryVerts; } inner[3] = {
            {"nested_vertex", tetV({0,0,0},{2,1,1},{1,2,1},{1,1,2}), 1},
            {"nested_edge",   tetV({1,0,0},{3,0,0},{2,2,1},{2,1,2}), 2},
            {"nested_face",   tetV({1,1,0},{3,1,0},{1,3,0},{1,1,2}), 3}};
        for (auto& in : inner) for (int o = 0; o < 2; o++) {
            Mesh MA = toMesh(outer[o]); int nb = 0;
            for (auto& p : in.s.v) { double q[3] = {(double)p[0], (double)p[1], (double)p[2]}; Pip r = classifyPoint(MA, q);
                if (r.boundary) nb++; else if (!r.inside) { fprintf(stderr, "nested B vertex outside A\n"); return 1; } }
            if (nb != in.boundaryVerts) { fprintf(stderr, "nested %s/%s: %d boundary vertices, wanted %d\n", in.cls, on[o], nb, in.boundaryVerts); return 1; }
            auto ms = mapsFor(4);
            for (size_t k = 0; k < ms.size(); k++) { long long M[3][3]; M3(ms[k], M); emitPair(in.cls, std::string(on[o]) + "_m" + std::to_string(k), applyMap(outer[o], M), applyMap(in.s, M), 1); }
        }
    }
    // ---- (2) non-convex vertex-vertex: A and B separated by the plane z = 2, B meets that plane in exactly one vertex that is a vertex of A ----
    {
        struct C { const char* n; Shape A, B; I3 contact; } cs[6] = {
            {"L_reflex_tet", lPrism(), tetV({2,2,2},{3,2,4},{2,3,4},{3,3,5}), {2,2,2}},
            {"L_corner_box", lPrism(), boxAt(4,2,2,6,4,4), {4,2,2}},
            {"L_tip_tet",    lPrism(), tetV({4,0,2},{5,1,4},{6,0,4},{5,0,5}), {4,0,2}},
            {"U_reflex_tet", uPrism(), tetV({2,2,2},{1,3,4},{2,3,5},{3,3,4}), {2,2,2}},
            {"U_corner_box", uPrism(), boxAt(6,4,2,8,6,4), {6,4,2}},
            {"U_inner_tet",  uPrism(), tetV({4,2,2},{5,3,4},{4,3,5},{5,2,5}), {4,2,2}}};
        for (auto& c : cs) {
            int onPlane = 0; bool ok = true;
            for (auto& p : c.A.v) if (p[2] > 2) ok = false;
            for (auto& p : c.B.v) { if (p[2] < 2) ok = false; if (p[2] == 2) onPlane++; }
            bool boxB = c.B.v.size() == 8; bool hasC = false; for (auto& p : c.B.v) if (p == c.contact) hasC = true;   // a box B has a 4-vertex face on z = 2; its xy rectangle meets the prism's polygon only at the contact vertex (checked by hand: derived)
            if (!hasC) ok = false;
            bool isV = false; for (auto& p : c.A.v) if (p == c.contact) isV = true;
            if (!ok || onPlane != (boxB ? 4 : 1) || !isV) { fprintf(stderr, "nonconvex construction check failed %s\n", c.n); return 1; }
            auto ms = mapsFor(3);
            for (size_t k = 0; k < ms.size(); k++) { long long M[3][3]; M3(ms[k], M); emitPair("nonconvex_vv", std::string(c.n) + "_m" + std::to_string(k), applyMap(c.A, M), applyMap(c.B, M), 0); }
        }
    }
    // ---- (2b) third component and (4) two-vertex contact: A = two disjoint boxes ----
    {
        Shape b1 = boxAt(0,0,0,2,2,2), b2 = boxAt(6,0,0,8,2,2), A = joined(b1, b2);
        struct C { const char* cls; const char* n; Shape B; int contacts; } cs[] = {
            {"threecomp_vv", "box_at_b1", boxAt(-2,-2,-2,0,0,0), 1},
            {"threecomp_vv", "tet_at_b2", tetV({8,2,2},{9,3,3},{8,4,3},{10,3,5}), 1},
            {"threecomp_vv", "box_at_b2", boxAt(8,2,2,10,4,4), 1},
            {"twovertex", "box_bridge", boxAt(2,2,2,6,4,4), 2},
            {"twovertex", "tet_bridge", tetV({2,2,2},{6,2,2},{4,4,3},{4,3,5}), 2}};
        for (auto& c : cs) {
            // exact check against each convex piece: SAT must say "touching", contact set a single point that is a vertex of both
            int pts = 0;
            for (const Shape* pc : {&b1, &b2}) { Shape pcs = *pc; int sc = satClass(pcs, c.B);
                if (sc == 0) { fprintf(stderr, "interiors overlap %s\n", c.n); return 1; }
                if (sc == 2) { Contact ct = contactOf(pcs, c.B); if (ct.dim != 0 || ct.fa != 'V' || ct.fb != 'V') { fprintf(stderr, "contact not VV %s\n", c.n); return 1; } pts++; } }
            if (pts != c.contacts) { fprintf(stderr, "contact count %d != %d for %s\n", pts, c.contacts, c.n); return 1; }
            auto ms = mapsFor(c.contacts == 2 ? 4 : 3);
            for (size_t k = 0; k < ms.size(); k++) { long long M[3][3]; M3(ms[k], M); emitPair(c.cls, std::string(c.n) + "_m" + std::to_string(k), applyMap(A, M), applyMap(c.B, M), 0); }
        }
    }
    // ---- (3) random vertex-vertex placements ----
    {
        Shape sh[4] = {makeBoxS(0, 3), makeTetS(3), makeOctS(3), makeTetS(5)}; const char* nm[4] = {"bx3", "te3", "oc3", "te5"};
        struct Cand { int ia, ib, mir; I3 t; }; std::vector<Cand> cand;
        for (int ia = 0; ia < 4; ia++) for (int ib = 0; ib < 4; ib++) for (int mir = 0; mir < 2; mir++)
            for (long long x = -8; x <= 8; x++) for (long long y = -8; y <= 8; y++) for (long long z = -8; z <= 8; z++) {
                Shape A = moved(sh[ia], 0, {0,0,0}, ID), B = moved(sh[ib], mir, {x,y,z}, ID);
                if (satClass(A, B) != 2) continue;
                Contact c = contactOf(A, B); if (c.dim == 0 && c.fa == 'V' && c.fb == 'V') cand.push_back({ia, ib, mir, {x, y, z}});
            }
        fprintf(stderr, "random-VV candidates: %zu\n", cand.size());
        for (int i = (int)cand.size() - 1; i > 0; i--) std::swap(cand[i], cand[rnd(i + 1)]);
        int want = 320; if ((int)cand.size() < want) want = (int)cand.size();
        for (int i = 0; i < want; i++) {
            long long M[3][3]; randomMap(M);
            Shape A = moved(sh[cand[i].ia], 0, {0,0,0}, M), B = moved(sh[cand[i].ib], cand[i].mir, cand[i].t, M);
            if (satClass(A, B) != 2) { fprintf(stderr, "class changed under map\n"); return 1; }
            Contact c = contactOf(A, B); if (!(c.dim == 0 && c.fa == 'V' && c.fb == 'V')) { fprintf(stderr, "label changed under map\n"); return 1; }
            char nmb[96]; snprintf(nmb, sizeof nmb, "%s_%s%s_%03d", nm[cand[i].ia], nm[cand[i].ib], cand[i].mir ? "m" : "", i);
            emitPair("randvv", nmb, A, B, 0);
        }
    }
    fclose(mf);
    for (auto& kv : counts) printf("%s: %d pairs\n", kv.first.c_str(), kv.second);
    return 0;
}
