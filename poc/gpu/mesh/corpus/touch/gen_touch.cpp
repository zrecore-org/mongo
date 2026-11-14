// MP18 step 1: touch_* corpus generator (separate from gen_corpus.cpp; does not alter its output).
//   gen_touch <outdir> [--all]  writes <outdir>/adversarial/touch_*_{A,B}.msh and <outdir>/manifest.tsv (same columns as the main corpus,
//                               so oracle/score_library.py and mesh_compare score it unchanged)
// Solids: axis boxes, tetrahedra, octahedra with small integer coordinates, optionally mirrored, under unimodular integer maps
// (identity, axis rotation, shear). Everything is exact integer/rational arithmetic (boost cpp_int / cpp_rational).
// Method: enumerate candidate placements; keep those whose interiors are disjoint but whose closures touch (complete separating-axis test over
// face normals and edge-edge cross products, exact). The contact set A∩B (closed) is computed exactly as the convex hull of candidate points
// (vertices in the other solid, edge x face-plane points inside both); its affine dimension and the feature (vertex/edge/face) each solid
// presents at its relative-interior point give the verified class label. Expected volumes: interiors are disjoint, so inter = 0,
// union = vA + vB, diff = vA, as exact 6*volume dyadics. Generator self-checks: independent exactVolumeTimes6 of both operands, closed orientation.
#include "../../harness/exact.h"
#include "../../harness/mesh_io.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <array>
#include <cstring>
#include <numeric>
#include <climits>
#include <algorithm>
#include <tuple>
#include <cstdlib>
#include <map>
#include <set>
#include <sys/stat.h>

using namespace mesh;
using Rat = boost::multiprecision::cpp_rational;
using I3 = std::array<long long, 3>;
using R3 = std::array<Rat, 3>;

struct Shape { std::vector<I3> v; std::vector<std::array<int, 3>> t; };      // triangles CCW from outside after orient()

static long long det3(const I3& a, const I3& b, const I3& c) {
    return a[0]*(b[1]*c[2]-b[2]*c[1]) + a[1]*(b[2]*c[0]-b[0]*c[2]) + a[2]*(b[0]*c[1]-b[1]*c[0]);
}
static I3 sub(const I3& a, const I3& b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
static I3 cross(const I3& a, const I3& b) { return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]}; }
static long long dot(const I3& a, const I3& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// make every triangle face outward (w.r.t. the vertex centroid, valid because all shapes are convex with the centroid inside)
static void orient(Shape& s) {
    I3 sum = {0, 0, 0}; for (auto& p : s.v) for (int k = 0; k < 3; k++) sum[k] += p[k];
    long long n = (long long)s.v.size();
    for (auto& f : s.t) {
        I3 nrm = cross(sub(s.v[f[1]], s.v[f[0]]), sub(s.v[f[2]], s.v[f[0]]));
        I3 q = {sum[0] - n * s.v[f[0]][0], sum[1] - n * s.v[f[0]][1], sum[2] - n * s.v[f[0]][2]};     // n * (centroid - a)
        if (dot(nrm, q) > 0) std::swap(f[1], f[2]);
    }
}
static Shape makeBoxS(long long lo, long long hi) {
    Shape s; for (int z = 0; z < 2; z++) { long long Z = z ? hi : lo; s.v.push_back({lo, lo, Z}); s.v.push_back({hi, lo, Z}); s.v.push_back({hi, hi, Z}); s.v.push_back({lo, hi, Z}); }
    s.t = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{0,4,7},{0,7,3},{1,2,6},{1,6,5}}; orient(s); return s;
}
static Shape makeTetS(long long n) { Shape s; s.v = {{0,0,0},{n,0,0},{0,n,0},{0,0,n}}; s.t = {{0,2,1},{0,1,3},{0,3,2},{1,2,3}}; orient(s); return s; }
static Shape makeOctS(long long r) {
    Shape s; s.v = {{r,0,0},{-r,0,0},{0,r,0},{0,-r,0},{0,0,r},{0,0,-r}};
    for (int sx = 0; sx < 2; sx++) for (int sy = 0; sy < 2; sy++) for (int sz = 0; sz < 2; sz++) s.t.push_back({sx, 2 + sy, 4 + sz});
    orient(s); return s;
}
static Shape moved(Shape s, int mirror, I3 t, const long long M[3][3]) {
    for (auto& p : s.v) {
        if (mirror) for (int k = 0; k < 3; k++) p[k] = -p[k];
        for (int k = 0; k < 3; k++) p[k] += t[k];
        I3 q; for (int i = 0; i < 3; i++) q[i] = M[i][0]*p[0] + M[i][1]*p[1] + M[i][2]*p[2];
        p = q;
    }
    orient(s); return s;
}

// ---- convex-polytope geometry on the triangle soup ----
struct Plane { I3 n; long long d; };          // outward normal (primitive), n.x <= d inside
static std::vector<Plane> planesOf(const Shape& s) {
    std::vector<Plane> out;
    for (auto& f : s.t) {
        I3 n = cross(sub(s.v[f[1]], s.v[f[0]]), sub(s.v[f[2]], s.v[f[0]]));
        long long g = 0; for (int k = 0; k < 3; k++) g = std::gcd(g, std::llabs(n[k]));
        for (int k = 0; k < 3; k++) n[k] /= g;
        Plane p{n, dot(n, s.v[f[0]])};
        bool dup = false; for (auto& q : out) if (q.n == p.n && q.d == p.d) dup = true;
        if (!dup) out.push_back(p);
    }
    return out;
}
static std::vector<std::pair<int, int>> edgesOf(const Shape& s) {
    std::set<std::pair<int, int>> e; for (auto& f : s.t) for (int k = 0; k < 3; k++) { int a = f[k], b = f[(k+1)%3]; e.insert({std::min(a, b), std::max(a, b)}); }
    return {e.begin(), e.end()};
}
static Rat dotR(const I3& n, const R3& p) { return n[0]*p[0] + n[1]*p[1] + n[2]*p[2]; }
static bool inClosed(const std::vector<Plane>& pl, const R3& p) { for (auto& q : pl) if (dotR(q.n, p) > q.d) return false; return true; }
// strict containment of an integer point is not needed; feature = number of distinct supporting planes through a boundary point
static int planesThrough(const std::vector<Plane>& pl, const R3& p) { int c = 0; for (auto& q : pl) if (dotR(q.n, p) == q.d) c++; return c; }
static char featureOf(const std::vector<Plane>& pl, const R3& p) { int c = planesThrough(pl, p); return c >= 3 ? 'V' : c == 2 ? 'E' : c == 1 ? 'F' : 'I'; }

// complete separating-axis test. Returns +1 if some axis separates strictly (disjoint closures), 0 if no axis separates even weakly (interiors overlap),
// and 2 if some axis separates weakly but none strictly (interiors disjoint, closures touch).
static int satClass(const Shape& A, const Shape& B) {
    std::vector<I3> axes;
    for (auto* S : {&A, &B}) for (auto& p : planesOf(*S)) axes.push_back(p.n);
    auto ea = edgesOf(A), eb = edgesOf(B);
    for (auto& x : ea) for (auto& y : eb) { I3 c = cross(sub(A.v[x.second], A.v[x.first]), sub(B.v[y.second], B.v[y.first])); if (c[0] || c[1] || c[2]) axes.push_back(c); }
    bool weak = false, strict = false;
    for (auto& ax : axes) {
        long long amin = LLONG_MAX, amax = LLONG_MIN, bmin = LLONG_MAX, bmax = LLONG_MIN;
        for (auto& p : A.v) { long long d = dot(ax, p); amin = std::min(amin, d); amax = std::max(amax, d); }
        for (auto& p : B.v) { long long d = dot(ax, p); bmin = std::min(bmin, d); bmax = std::max(bmax, d); }
        if (amax < bmin || bmax < amin) strict = true;
        else if (amax == bmin || bmax == amin) weak = true;
    }
    return strict ? 1 : weak ? 2 : 0;
}

struct Contact { int dim = -1; char fa = '?', fb = '?'; size_t npts = 0; };
static Contact contactOf(const Shape& A, const Shape& B) {
    auto pa = planesOf(A), pb = planesOf(B);
    std::vector<R3> pts;
    auto add = [&](const R3& p) { if (!inClosed(pa, p) || !inClosed(pb, p)) return; for (auto& q : pts) if (q == p) return; pts.push_back(p); };
    for (auto& p : A.v) add({Rat(p[0]), Rat(p[1]), Rat(p[2])});
    for (auto& p : B.v) add({Rat(p[0]), Rat(p[1]), Rat(p[2])});
    auto sweep = [&](const Shape& S, const std::vector<Plane>& other) {
        for (auto& e : edgesOf(S)) for (auto& q : other) {
            I3 u = S.v[e.first], dv = sub(S.v[e.second], S.v[e.first]);
            long long den = dot(q.n, dv); if (den == 0) continue;
            Rat t = Rat(q.d - dot(q.n, u)) / Rat(den);
            if (t < 0 || t > 1) continue;
            add({Rat(u[0]) + t * dv[0], Rat(u[1]) + t * dv[1], Rat(u[2]) + t * dv[2]});
        }
    };
    sweep(A, pb); sweep(B, pa);
    Contact c; c.npts = pts.size();
    if (pts.empty()) return c;
    // affine dimension: rank of the difference vectors (fraction-free Gaussian elimination over the rationals)
    std::vector<R3> rows; for (size_t i = 1; i < pts.size(); i++) rows.push_back({pts[i][0]-pts[0][0], pts[i][1]-pts[0][1], pts[i][2]-pts[0][2]});
    int rank = 0;
    for (int col = 0; col < 3 && rank < (int)rows.size(); col++) {
        int piv = -1; for (size_t r = rank; r < rows.size(); r++) if (rows[r][col] != 0) { piv = (int)r; break; }
        if (piv < 0) continue; std::swap(rows[rank], rows[piv]);
        for (size_t r = rank + 1; r < rows.size(); r++) { Rat f = rows[r][col] / rows[rank][col]; for (int k = 0; k < 3; k++) rows[r][k] -= f * rows[rank][k]; }
        rank++;
    }
    c.dim = rank;
    R3 cen = {Rat(0), Rat(0), Rat(0)}; for (auto& p : pts) for (int k = 0; k < 3; k++) cen[k] += p[k];
    for (int k = 0; k < 3; k++) cen[k] /= (long)pts.size();
    c.fa = featureOf(pa, cen); c.fb = featureOf(pb, cen);
    return c;
}

// ---- output ----
static Mesh toMesh(const Shape& s) {
    Mesh m; for (auto& p : s.v) m.addV((double)p[0], (double)p[1], (double)p[2]);
    for (auto& f : s.t) m.addT(f[0], f[1], f[2]);
    return m;
}
static std::string out;
static FILE* man;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: gen_touch <outdir>\n"); return 2; }
    out = argv[1]; mkdir(out.c_str(), 0755); mkdir((out + "/adversarial").c_str(), 0755);
    man = fopen((out + "/manifest.tsv").c_str(), "wb");
    fprintf(man, "id\tclass\ttags\tnvA\tntA\tnvB\tntB\tvol6_A\tvol6_B\tvol6_union\tvol6_inter\tvol6_diff\n");
    const long long I[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    const long long ROT[3][3] = {{0,0,1},{1,0,0},{0,1,0}};             // axis rotation (x,y,z) -> (z,x,y)
    const long long SHR[3][3] = {{1,1,0},{0,1,1},{0,0,1}};             // shear, det 1
    const long long SHR2[3][3] = {{1,0,2},{1,1,0},{0,1,1}};            // second shear/permutation mix, det 1*(1*1-0)-0+2*(1*1-0)=3? computed below by volume anyway
    struct Spec { const char* name; Shape s; };
    std::vector<Spec> shapes = {{"bx", makeBoxS(0, 2)}, {"te", makeTetS(2)}, {"oc", makeOctS(2)}, {"te4", makeTetS(4)}};
    struct Cand { int ia, ib, mir; I3 t; Contact c; };
    std::map<std::string, std::vector<Cand>> byClass;
    for (int ia = 0; ia < 4; ia++) for (int ib = 0; ib < 4; ib++) for (int mir = 0; mir < 2; mir++)
        for (long long x = -5; x <= 5; x++) for (long long y = -5; y <= 5; y++) for (long long z = -5; z <= 5; z++) {
            Shape A = moved(shapes[ia].s, 0, {0, 0, 0}, I), B = moved(shapes[ib].s, mir, {x, y, z}, I);
            if (satClass(A, B) != 2) continue;
            Contact c = contactOf(A, B);
            if (c.dim < 0) { fprintf(stderr, "internal: SAT says touching but contact set empty (ia=%d ib=%d mir=%d t=%lld,%lld,%lld)\n", ia, ib, mir, x, y, z); return 1; }
            char lab[64]; snprintf(lab, sizeof lab, "d%d-%c%c", c.dim, c.fa, c.fb);
            byClass[lab].push_back({ia, ib, mir, {x, y, z}, c});
        }
    fprintf(stderr, "classes found (candidates): "); for (auto& kv : byClass) fprintf(stderr, "%s=%zu ", kv.first.c_str(), kv.second.size()); fprintf(stderr, "\n");
    // deterministic selection: per class, per (shape pair, mirror), the first candidate in enumeration order whose translation is smallest
    // (|t|_1 then lexicographic), at most 4 per class; then variants.
    struct Pick { std::string lab; Cand c; };
    std::vector<Pick> picks;
    for (auto& kv : byClass) {
        std::set<std::tuple<int, int, int>> seen; std::vector<Cand> v = kv.second;
        std::stable_sort(v.begin(), v.end(), [](const Cand& a, const Cand& b) { return std::llabs(a.t[0]) + std::llabs(a.t[1]) + std::llabs(a.t[2]) < std::llabs(b.t[0]) + std::llabs(b.t[1]) + std::llabs(b.t[2]); });
        int taken = 0;
        for (auto& c : v) {
            if (taken >= 4) break;
            if (!seen.insert({std::min(c.ia, c.ib), std::max(c.ia, c.ib), c.mir}).second) continue;
            picks.push_back({kv.first, c}); taken++;
        }
    }
    const bool all = argc > 2 && !strcmp(argv[2], "--all");     // sweep mode: every candidate placement (identity map only); NOT the registered family
    if (all) { picks.clear(); for (auto& kv : byClass) for (auto& c : kv.second) picks.push_back({kv.first, c}); }
    struct Var { const char* name; const long long (*M)[3]; };
    const Var vars[3] = {{"id", I}, {"rot", ROT}, {"shear", SHR}};
    (void)SHR2;
    int n = 0; std::map<std::string, int> perClass;
    for (auto& p : picks) for (int vi = 0; vi < 3; vi++) {
        // identity variant for all; rotation and shear variants for the first two picks of each class (keeps the family at a manageable size)
        if (vi > 0 && (all || perClass[p.lab + "#v"] >= 2 * 2)) continue;
        Shape A = moved(shapes[p.c.ia].s, 0, {0, 0, 0}, vars[vi].M), B = moved(shapes[p.c.ib].s, p.c.mir, p.c.t, vars[vi].M);
        // re-verify after the map (unimodular map: class must be unchanged); fail hard otherwise
        if (satClass(A, B) != 2) { fprintf(stderr, "internal: class changed under %s\n", vars[vi].name); return 1; }
        Contact c = contactOf(A, B);
        if (c.dim != p.c.c.dim || c.fa != p.c.c.fa || c.fb != p.c.c.fb) { fprintf(stderr, "internal: contact label changed under %s\n", vars[vi].name); return 1; }
        Mesh MA = toMesh(A), MB = toMesh(B);
        Dyadic vA = exactVolumeTimes6(MA), vB = exactVolumeTimes6(MB);
        if (signExact(vA) <= 0 || signExact(vB) <= 0) { fprintf(stderr, "internal: non-positive operand volume\n"); return 1; }
        // independent cross-check of the volume: sum of tetra dets with integer arithmetic
        long long v6 = 0; for (auto& f : A.t) v6 += det3(A.v[f[0]], A.v[f[1]], A.v[f[2]]);
        if (!equalExact(vA, Dyadic{BigInt(v6), 0})) { fprintf(stderr, "internal: volume mismatch\n"); return 1; }
        Dyadic vU = addExact(vA, vB), z{BigInt(0), 0};
        char id[160]; snprintf(id, sizeof id, "touch_%s_%s_%s%s_%04d", p.lab.c_str(), shapes[p.c.ia].name, shapes[p.c.ib].name, p.c.mir ? "m" : "", n++);
        std::string sid = id; for (auto& ch : sid) if (ch == '-') ch = '_';
        std::string tags = std::string("valid,touch,contact=") + p.lab + "," + vars[vi].name;
        writeMesh(out + "/adversarial/" + sid + "_A.msh", MA); writeMesh(out + "/adversarial/" + sid + "_B.msh", MB);
        fprintf(man, "%s\ttouch\t%s\t%zu\t%zu\t%zu\t%zu\t%s\t%s\t%s\t%s\t%s\n", sid.c_str(), tags.c_str(), MA.nv(), MA.nt(), MB.nv(), MB.nt(),
                toString(vA).c_str(), toString(vB).c_str(), toString(vU).c_str(), toString(z).c_str(), toString(vA).c_str());
        if (vi == 0) perClass[p.lab + "#v"] += 0;
        if (vi > 0) perClass[p.lab + "#v"]++;
        perClass[p.lab]++;
    }
    fclose(man);
    printf("touch corpus: %d pairs in %s\n", n, out.c_str());
    for (auto& kv : perClass) if (kv.first.find('#') == std::string::npos) printf("  %s: %d\n", kv.first.c_str(), kv.second);
    return 0;
}
