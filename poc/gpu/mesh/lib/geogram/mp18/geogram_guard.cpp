// MP18 step 3: geogram_guard, a wrapper around Geogram's boolean with an exact contact guard. CLI identical to geogram_runner:
//   geogram_guard boolean <union|intersection|difference> A.msh B.msh out.msh      exit 0 ok, 3 refused, 4 error
// Algorithm (all decisions by exact predicates from harness/exact.h, pip.h, topology.h; no tolerance anywhere):
//   1. Run Geogram exactly as geogram_runner does (same conversion, same flags, same single call).
//   2. Trigger: some vertex of A has exactly the coordinates of a vertex of B (contact vertex), or the bounding boxes overlap in a
//      zero-extent set on some axis (bbox contact; the overlap box's corners are the contact points).
//      Not triggered, or an operand is not a closed manifold with positive volume => Geogram's result is written unchanged.
//   3. Post-check (guard b): around each contact point v (at most 16), classify the 26 points v + delta*d (d in {-1,0,1}^3 \ 0, delta a
//      power of two = 2^-10 of the largest bounding-box extent) and one point inside each operand next to every incident triangle, exactly in A, B and the result R (ray parity, symbolic tie-break);
//      points on a surface of A, B or R are skipped. The result is accepted iff R agrees with the boolean of A and B at every such point.
//   4. A result that fails the post-check is recomputed from the same geometry with a different vertex numbering (root cause, see
//      REPORT.md: Geogram's classification starts a ray at a vertex index that can be the contact vertex): reverse both, reverse A,
//      reverse B, rotate both. The first recomputation that passes the post-check is written. If none passes: exit 3 (refused).
// MP18_MUTANT=<name> plants a bug in the guard for the mutation study: skipaxis, offbyone, nocheck.
#include "../../../harness/exact.h"
#include "../../../harness/mesh_io.h"
#include "../../../harness/pip.h"
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
// Test points around the contact point v. (1) the 26 axis/diagonal offsets; (2) for every triangle of A or B incident to v (cyclic (v,u1,u2),
// outward orientation): v + s*(e1+e2) displaced a little along the inward normal, i.e. a point inside that operand next to v. Sheared corner
// cones can miss all 26 integer directions (measured in MP18: the sheared VV pairs), the facet-derived points cannot miss the operand.
static void testPointsAround(const Mesh& A, const Mesh& B, const std::array<double, 3>& v, double delta, std::vector<std::array<double, 3>>& out) {
    bool skipZ = !strcmp(g_mutant, "skipaxis");
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) {
        if (!dx && !dy && !dz) continue;
        if (skipZ && dz != 0) continue;
        out.push_back({v[0] + dx * delta, v[1] + dy * delta, v[2] + dz * delta});
    }
    int nfacet = 0;
    for (const Mesh* M : {&A, &B}) for (size_t k = 0; k < M->nt() && nfacet < 24; k++) for (int c = 0; c < 3; c++) {
        const double* p = &M->v[3 * (size_t)M->t[3*k+c]];
        if (p[0] != v[0] || p[1] != v[1] || p[2] != v[2]) continue;
        const double* u1 = &M->v[3 * (size_t)M->t[3*k+(c+1)%3]]; const double* u2 = &M->v[3 * (size_t)M->t[3*k+(c+2)%3]];
        double e1[3], e2[3], n[3], mx = 0, mn = 0;
        for (int i = 0; i < 3; i++) { e1[i] = u1[i] - v[i]; e2[i] = u2[i] - v[i]; mx = std::max(mx, std::max(std::fabs(e1[i]), std::fabs(e2[i]))); }
        n[0] = e1[1]*e2[2]-e1[2]*e2[1]; n[1] = e1[2]*e2[0]-e1[0]*e2[2]; n[2] = e1[0]*e2[1]-e1[1]*e2[0];
        for (int i = 0; i < 3; i++) mn = std::max(mn, std::fabs(n[i]));
        if (mx == 0 || mn == 0) continue;
        double kk = std::ldexp(1.0, -std::ilogb(mn / mx) - 3);                // inward displacement about 1/8 of the in-plane step
        std::array<double, 3> q;
        for (int i = 0; i < 3; i++) q[i] = v[i] + delta * (e1[i] + e2[i]) / mx - delta * n[i] * kk / mx;   // outward n for CCW (v,u1,u2): subtract = inward
        if (skipZ) q[2] = v[2];
        out.push_back(q); nfacet++;
    }
}
static int postCheck(const Mesh& A, const Mesh& B, const Mesh& R, int op, const std::vector<std::array<double, 3>>& pts, double extent, int* tested) {
    int bad = 0; *tested = 0;
    double delta = std::ldexp(1.0, std::ilogb(extent > 0 ? extent : 1.0) - 10);
    std::vector<std::array<double, 3>> q;
    for (auto& v : pts) testPointsAround(A, B, v, delta, q);
    for (auto& pt : q) {
        mesh::Pip a = mesh::classifyPoint(A, pt.data()), b = mesh::classifyPoint(B, pt.data()), r = mesh::classifyPoint(R, pt.data());
        if (a.boundary || b.boundary || r.boundary) continue;
        (*tested)++; if (r.inside != inOp(op, a.inside, b.inside)) bad++;
    }
    return bad;
}

int main(int argc, char** argv) {
    int realOut = dup(1); dup2(2, 1); (void)realOut;
    if (argc != 6 || strcmp(argv[1], "boolean")) { fprintf(stderr, "usage: geogram_guard boolean <union|intersection|difference> A B out\n"); return 4; }
    if (const char* m = getenv("MP18_MUTANT")) g_mutant = m;
    try {
        GEO::initialize(GEO::GEOGRAM_INSTALL_ALL); GEO::set_assert_mode(GEO::ASSERT_THROW);
        if (const char* th = getenv("GEOGRAM_RUNNER_THREADS")) GEO::Process::set_max_threads((GEO::index_t)atoi(th));
        if (!getenv("GEOGRAM_RUNNER_LOG")) GEO::Logger::instance()->set_quiet(true);
        exactinit();
        std::string opS = argv[2]; int op = opS == "union" ? 0 : opS == "intersection" ? 1 : opS == "difference" ? 2 : -1;
        if (op < 0) return 4;
        const char* opName = op == 0 ? "A+B" : op == 1 ? "A*B" : "A-B";
        Mesh a, b; if (!mesh::readMesh(argv[3], a) || !mesh::readMesh(argv[4], b)) { fprintf(stderr, "cannot read\n"); return 4; }
        Mesh r;
        if (!runGeogram(a, b, opName, r)) return 3;
        auto t0 = Clock::now();
        double extent = 0; auto pts = contactPoints(a, b, extent);
        int attempt = 0, bad = 0, tested = 0; bool triggered = !pts.empty(), guarded = false;
        if (triggered) {
            mesh::Topology ta = mesh::analyzeTopology(a), tb = mesh::analyzeTopology(b);
            guarded = ta.closedManifold() && tb.closedManifold() && !ta.inverted && !tb.inverted;   // the scoring's validInputs rule
            if (guarded && strcmp(g_mutant, "nocheck")) {
                bad = postCheck(a, b, r, op, pts, extent, &tested);
                int first = bad;
                while (bad > 0 && attempt < 4) {
                    Mesh a2 = a, b2 = b;
                    if (attempt == 0) { a2 = reversedVertices(a); b2 = reversedVertices(b); }
                    else if (attempt == 1) a2 = reversedVertices(a);
                    else if (attempt == 2) b2 = reversedVertices(b);
                    else { a2 = rotatedVertices(a); b2 = rotatedVertices(b); }
                    attempt++;
                    Mesh r2; if (!runGeogram(a2, b2, opName, r2)) continue;
                    int t2; int bad2 = postCheck(a, b, r2, op, pts, extent, &t2);
                    if (bad2 == 0) { r = r2; bad = 0; break; }
                }
                if (bad > 0) { fprintf(stderr, "REFUSED: guard: contact post-check failed (%d of %d points, first run %d)\n", bad, tested, first); return 3; }
            }
        }
        fprintf(stderr, "guard triggered=%d guarded=%d contact_points=%zu tested=%d retries=%d guard_ms=%.3f\n", (int)triggered, (int)guarded, pts.size(), tested, attempt, msSince(t0));
        if (!mesh::writeMesh(argv[5], r)) { fprintf(stderr, "cannot write\n"); return 4; }
        return 0;
    } catch (const std::exception& e) { fprintf(stderr, "INTERNAL: exception: %s\n", e.what()); return 4; }
}
