// MP14d step 1 test: meshcontains::classify versus the exactmesh oracle (harness/pip.h, Shewchuk predicates; read-only, paths given by build.sh) on a uv
// sphere, integer blocks, scaled and offset copies. Queries: random in the bounding box, on vertices, edge midpoints, face centroids (exact boundary in the
// integer-block cases), ulp neighbours of vertices, and ray-degenerate points (same y and z as a vertex, x shifted). Exit 0 only if 0 disagreements.
// Mutants: -DMESHCONTAINS_MUT_NO_TIEBREAK (no symbolic tie-break) and -DMUT_BOUNDARY_IN (boundary reported as inside) must each produce disagreements.
#include "meshcontains_core.h"
#include "harness/pip.h"
#include "gpu/pip_cases.h"
#include <cstdio>

extern "C" void exactinit(void);
int main() {
    exactinit();
    std::vector<std::pair<std::string, Mesh>> meshes;
    meshes.push_back({"sphere 12x16", uvsphere(12, 16, 1.0, 0.1, -0.2, 0.3)});
    meshes.push_back({"block 3", block(3)});
    meshes.push_back({"block 4 scaled 2^-20 offset", mapped(block(4), -20, 0.25, -0.5, 3.0)});
    meshes.push_back({"sphere 8x8 scaled 2^30", mapped(uvsphere(8, 8, 1.0, 0, 0, 0), 30, 0, 0, 0)});
    long total = 0, bad = 0, on = 0, in = 0, out = 0;
    for (auto& nm : meshes) {
        const Mesh& m = nm.second;
        std::vector<std::vector<double>> qs;
        Rng rng(12345);
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        for (size_t i = 0; i < m.nv(); i++) for (int d = 0; d < 3; d++) { lo[d] = std::min(lo[d], m.v[3*i+d]); hi[d] = std::max(hi[d], m.v[3*i+d]); }
        for (int i = 0; i < 400; i++) qs.push_back({lo[0] + (hi[0]-lo[0]) * (1.2 * rng.u01() - 0.1), lo[1] + (hi[1]-lo[1]) * (1.2 * rng.u01() - 0.1), lo[2] + (hi[2]-lo[2]) * (1.2 * rng.u01() - 0.1)});
        for (size_t i = 0; i < m.nv(); i++) {
            const double* a = &m.v[3*i];
            qs.push_back({a[0], a[1], a[2]});
            for (int d = 0; d < 3; d++) for (int k : {-1, 1}) { std::vector<double> q = {a[0], a[1], a[2]}; q[d] = ulps(q[d], k); qs.push_back(q); }
            const double sx = (hi[0]-lo[0]) * 0.5; qs.push_back({a[0] - sx, a[1], a[2]}); qs.push_back({a[0] + sx, a[1], a[2]});   // ray-degenerate
        }
        for (size_t k = 0; k < m.nt(); k++) {
            const double* a = &m.v[3*(size_t)m.t[3*k]]; const double* b = &m.v[3*(size_t)m.t[3*k+1]]; const double* c = &m.v[3*(size_t)m.t[3*k+2]];
            qs.push_back({(a[0]+b[0])/2, (a[1]+b[1])/2, (a[2]+b[2])/2});
            qs.push_back({(a[0]+b[0]+c[0])/3, (a[1]+b[1]+c[1])/3, (a[2]+b[2]+c[2])/3});
        }
        long mb = 0;
        for (auto& q : qs) {
            mesh::Pip o = mesh::classifyPoint(m, q.data());
            const uint8_t want = o.boundary ? meshcontains::kOn : o.inside ? meshcontains::kIn : meshcontains::kOut;
            uint8_t got = meshcontains::classify(m.v, m.t, q.data());
#ifdef MUT_BOUNDARY_IN
            if (got == meshcontains::kOn) got = meshcontains::kIn;
#endif
            total++; (want == 2 ? on : want == 1 ? in : out)++;
            if (got != want) { bad++; mb++; }
        }
        std::printf("%-32s queries=%zu disagreements=%ld\n", nm.first.c_str(), qs.size(), mb);
    }
    std::printf("TOTAL queries=%ld (OUT %ld, IN %ld, ON %ld) disagreements=%ld\n", total, out, in, on, bad);
    return bad == 0 ? 0 : 1;
}
