// MP18b: geogram_guard2, differential guard v2. CLI identical to geogram_runner:
//   geogram_guard2 boolean <union|intersection|difference> A.msh B.msh out.msh      exit 0 ok, 3 refused, 4 error
// Trigger (same exact test as v1): a vertex of A has exactly the coordinates of a vertex of B, or the bounding boxes overlap in a zero-extent
// set on some axis. Not triggered, or an operand is not a closed manifold with positive volume => Geogram's result is written unchanged.
// Triggered: run Geogram on (1) the original vertex order, (2) both operands' vertex order reversed, (3) both rotated by half (fixed deterministic
// permutation). Canonicalise each result: exact 6*volume (dyadic), exact sum of squared doubled triangle areas (an exact integer-valued proxy for
// area: sqrt of the area is not rational), Euler characteristic and component count (vertices welded by exact coordinates), and the canonical
// triangle set (each triangle as 9 doubles rotated to its lexicographically smallest cyclic start, set sorted). Accept only if all three are equal;
// write run (1), so an accepted result is byte-identical to the plain runner. Otherwise exit 3 (REFUSED). No test points, no tolerance.
// MP18_MUTANT: skipperm (third run omitted), majority (accept when 2 of 3 agree, writing the majority result), offbyone (v1's trigger mutant).
#include "../../../harness/exact.h"
#include "../../../harness/mesh_io.h"
#include "../../../harness/topology.h"

#include <geogram/basic/common.h>
#include <geogram/basic/assert.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/process.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_surface_intersection.h>

#include <unistd.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

extern "C" void exactinit(void);
using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
using mesh::Mesh;

static void toGeo(const Mesh& m, GEO::Mesh& g) {   // identical to runner.cpp
    g.clear(); g.vertices.set_dimension(3); g.vertices.create_vertices((GEO::index_t)m.nv());
    for (size_t i = 0; i < m.nv(); ++i) g.vertices.point((GEO::index_t)i) = GEO::vec3(m.v[3*i], m.v[3*i+1], m.v[3*i+2]);
    g.facets.create_triangles((GEO::index_t)m.nt());
    for (size_t f = 0; f < m.nt(); ++f) for (GEO::index_t lv = 0; lv < 3; ++lv) g.facets.set_vertex((GEO::index_t)f, lv, m.t[3*f+lv]);
}
static void fromGeo(const GEO::Mesh& g, Mesh& m) {   // identical to runner.cpp
    m.v.clear(); m.t.clear();
    for (GEO::index_t v = 0; v < g.vertices.nb(); ++v) { const double* p = g.vertices.point_ptr(v); m.v.push_back(p[0]); m.v.push_back(p[1]); m.v.push_back(p[2]); }
    for (GEO::index_t f = 0; f < g.facets.nb(); ++f) { GEO::index_t n = g.facets.nb_vertices(f);
        for (GEO::index_t k = 1; k + 1 < n; ++k) m.addT(g.facets.vertex(f, 0), g.facets.vertex(f, k), g.facets.vertex(f, k + 1)); }
}
static bool runGeogram(const Mesh& a, const Mesh& b, const char* opName, Mesh& r) {
    GEO::Mesh ga, gb, gr; toGeo(a, ga); toGeo(b, gb);
    try { GEO::mesh_boolean_operation(gr, ga, gb, opName, GEO::MESH_BOOL_OPS_DEFAULT); }
    catch (const std::exception& e) { fprintf(stderr, "REFUSED: geogram threw: %s\n", e.what()); return false; }
    fromGeo(gr, r); return true;
}
static Mesh reversedVertices(const Mesh& m) {      // same geometry, vertex indices reversed (triangle order and orientation unchanged)
    Mesh o; size_t n = m.nv();
    for (size_t i = 0; i < n; i++) o.addV(m.v[3*(n-1-i)], m.v[3*(n-1-i)+1], m.v[3*(n-1-i)+2]);
    for (uint32_t i : m.t) o.t.push_back((uint32_t)(n - 1 - i));
    return o;
}
static Mesh rotatedVertices(const Mesh& m) {       // vertex index i -> (i + n/2) mod n
    Mesh o; size_t n = m.nv(), h = n / 2;
    for (size_t i = 0; i < n; i++) { size_t s = (i + n - h) % n; o.addV(m.v[3*s], m.v[3*s+1], m.v[3*s+2]); }
    for (uint32_t i : m.t) o.t.push_back((uint32_t)((i + h) % n));
    return o;
}

static const char* g_mutant = "";
// contact points: coincident vertices and corners of a zero-extent bbox overlap
static std::vector<std::array<double, 3>> contactPoints(const Mesh& A, const Mesh& B, double& extent) {
    std::vector<std::array<double, 3>> pts;
    std::map<std::array<double, 3>, int> inB;
    for (size_t i = 0; i < B.nv(); i++) inB[{B.v[3*i], B.v[3*i+1], B.v[3*i+2]}] = 1;
    size_t start = !strcmp(g_mutant, "offbyone") ? 1 : 0;            // mutant: first vertex of A never tested
    for (size_t i = start; i < A.nv(); i++) { std::array<double, 3> k = {A.v[3*i], A.v[3*i+1], A.v[3*i+2]}; if (inB.count(k)) { bool dup = false; for (auto& q : pts) if (q == k) dup = true; if (!dup) pts.push_back(k); } }
    double alo[3], ahi[3], blo[3], bhi[3]; for (int k = 0; k < 3; k++) { alo[k] = blo[k] = 1e300; ahi[k] = bhi[k] = -1e300; }
    for (size_t i = 0; i < A.nv(); i++) for (int k = 0; k < 3; k++) { alo[k] = std::min(alo[k], A.v[3*i+k]); ahi[k] = std::max(ahi[k], A.v[3*i+k]); }
    for (size_t i = 0; i < B.nv(); i++) for (int k = 0; k < 3; k++) { blo[k] = std::min(blo[k], B.v[3*i+k]); bhi[k] = std::max(bhi[k], B.v[3*i+k]); }
    extent = 0; for (int k = 0; k < 3; k++) extent = std::max(extent, std::max(ahi[k], bhi[k]) - std::min(alo[k], blo[k]));
    double lo[3], hi[3]; bool overlap = true, zero = false;
    for (int k = 0; k < 3; k++) { lo[k] = std::max(alo[k], blo[k]); hi[k] = std::min(ahi[k], bhi[k]);
        if (hi[k] < lo[k]) overlap = false;
        bool z = !strcmp(g_mutant, "offbyone") ? false : (hi[k] == lo[k]);      // mutant: zero-extent overlap not recognised (off-by-one: > instead of >=)
        if (z) zero = true; }
    if (overlap && zero) for (int m = 0; m < 8; m++) { std::array<double, 3> k = {(m & 1) ? hi[0] : lo[0], (m & 2) ? hi[1] : lo[1], (m & 4) ? hi[2] : lo[2]}; bool dup = false; for (auto& q : pts) if (q == k) dup = true; if (!dup) pts.push_back(k); }
    if (pts.size() > 16) pts.resize(16);
    return pts;
}
static bool inOp(int op, bool a, bool b) { return op == 0 ? (a || b) : op == 1 ? (a && b) : (a && !b); }

struct Canon { std::string vol, area2; long euler; int comps; std::vector<std::array<double, 9>> tris; bool empty;
    bool operator==(const Canon& o) const { return vol == o.vol && area2 == o.area2 && euler == o.euler && comps == o.comps && tris == o.tris; } };
static Canon canon(const Mesh& m) {
    Canon c; c.empty = m.nt() == 0;
    c.vol = mesh::toString(mesh::exactVolumeTimes6(m));
    mesh::Topology t = mesh::analyzeTopology(m); c.euler = t.euler(); c.comps = t.components;
    mesh::BigInt sum = 0; int emin = INT32_MAX;
    std::vector<mesh::BigInt> iv(m.v.size()); std::vector<int> ex(m.v.size()); std::vector<int64_t> mant(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) { int e; double f = std::frexp(m.v[i], &e); mant[i] = (int64_t)std::ldexp(f, 53); ex[i] = mant[i] == 0 ? 0 : e - 53; if (mant[i] && ex[i] < emin) emin = ex[i]; }
    if (emin == INT32_MAX) emin = 0;
    for (size_t i = 0; i < m.v.size(); i++) { iv[i] = mesh::BigInt(mant[i]); if (mant[i]) iv[i] <<= (ex[i] - emin); }
    for (size_t k = 0; k < m.nt(); k++) {
        const mesh::BigInt *a = &iv[3*(size_t)m.t[3*k]], *b = &iv[3*(size_t)m.t[3*k+1]], *cc = &iv[3*(size_t)m.t[3*k+2]];
        mesh::BigInt ux = b[0]-a[0], uy = b[1]-a[1], uz = b[2]-a[2], wx = cc[0]-a[0], wy = cc[1]-a[1], wz = cc[2]-a[2];
        mesh::BigInt nx = uy*wz-uz*wy, ny = uz*wx-ux*wz, nz = ux*wy-uy*wx;
        sum += nx*nx + ny*ny + nz*nz;
        std::array<double, 9> best{}; bool have = false;
        for (int r = 0; r < 3; r++) { std::array<double, 9> q; for (int j = 0; j < 3; j++) for (int d = 0; d < 3; d++) q[3*j+d] = m.v[3*(size_t)m.t[3*k+(r+j)%3]+d];
            if (!have || q < best) { best = q; have = true; } }
        c.tris.push_back(best);
    }
    c.area2 = mesh::toString(mesh::Dyadic{sum, 6 * emin});
    std::sort(c.tris.begin(), c.tris.end());
    return c;
}

int main(int argc, char** argv) {
    int realOut = dup(1); dup2(2, 1); (void)realOut;
    if (argc != 6 || strcmp(argv[1], "boolean")) { fprintf(stderr, "usage: geogram_guard2 boolean <union|intersection|difference> A B out\n"); return 4; }
    if (const char* m = getenv("MP18_MUTANT")) g_mutant = m;
    try {
        GEO::initialize(GEO::GEOGRAM_INSTALL_ALL); GEO::set_assert_mode(GEO::ASSERT_THROW);
        if (const char* th = getenv("GEOGRAM_RUNNER_THREADS")) GEO::Process::set_max_threads((GEO::index_t)atoi(th));
        if (!getenv("GEOGRAM_RUNNER_LOG")) GEO::Logger::instance()->set_quiet(true);
        std::string opS = argv[2]; int op = opS == "union" ? 0 : opS == "intersection" ? 1 : opS == "difference" ? 2 : -1;
        if (op < 0) return 4;
        const char* opName = op == 0 ? "A+B" : op == 1 ? "A*B" : "A-B";
        Mesh a, b; if (!mesh::readMesh(argv[3], a) || !mesh::readMesh(argv[4], b)) { fprintf(stderr, "cannot read\n"); return 4; }
        Mesh r;
        if (!runGeogram(a, b, opName, r)) return 3;
        auto t0 = Clock::now();
        double extent = 0; auto pts = contactPoints(a, b, extent);
        bool triggered = !pts.empty(), guarded = false; int agree = -1;
        if (triggered) {
            mesh::Topology ta = mesh::analyzeTopology(a), tb = mesh::analyzeTopology(b);
            guarded = ta.closedManifold() && tb.closedManifold() && !ta.inverted && !tb.inverted;
            if (guarded) {
                Mesh r1, r2; bool ok1 = runGeogram(reversedVertices(a), reversedVertices(b), opName, r1);
                bool skip = !strcmp(g_mutant, "skipperm"); bool ok2 = skip ? true : runGeogram(rotatedVertices(a), rotatedVertices(b), opName, r2);
                if (skip) { r2 = r1; }
                if (!ok1 || !ok2) { fprintf(stderr, "REFUSED: guard2: a vertex-order run threw\n"); return 3; }
                Canon c0 = canon(r), c1 = canon(r1), c2 = canon(r2);
                bool e01 = c0 == c1, e02 = c0 == c2, e12 = c1 == c2;
                if (e01 && e02) agree = 3;
                else if (!strcmp(g_mutant, "majority") && (e01 || e02 || e12)) { agree = 2; if (!e01 && !e02) r = r1; }
                else { fprintf(stderr, "REFUSED: guard2: vertex-order runs disagree (orig=rev %d, orig=rot %d, rev=rot %d)\n", (int)e01, (int)e02, (int)e12); return 3; }
            }
        }
        fprintf(stderr, "guard2 triggered=%d guarded=%d contact_points=%zu agree=%d guard_ms=%.3f\n", (int)triggered, (int)guarded, pts.size(), agree, msSince(t0));
        if (!mesh::writeMesh(argv[5], r)) { fprintf(stderr, "cannot write\n"); return 4; }
        return 0;
    } catch (const std::exception& e) { fprintf(stderr, "INTERNAL: exception: %s\n", e.what()); return 4; }
}
