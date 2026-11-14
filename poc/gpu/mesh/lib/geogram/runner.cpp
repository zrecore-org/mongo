// geogram_runner: MP04 CLI around Geogram's exact boolean (permissive build).
//   geogram_runner boolean <union|intersection|difference> <A.msh> <B.msh> <out.msh>
//   geogram_runner measure <A.msh>
// Exit codes: 0 ok, 3 REFUSED (Geogram threw on the boolean call), 4 internal error / I/O / usage.
// stdout carries only the `measure` line; Geogram's own console logging is redirected to stderr.
#include "../../harness/mesh_io.h"

#include <geogram/basic/common.h>
#include <geogram/basic/assert.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/process.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_geometry.h>
#include <geogram/mesh/mesh_topology.h>
#include <geogram/mesh/mesh_surface_intersection.h>

#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static void toGeo(const mesh::Mesh& m, GEO::Mesh& g) {
    g.clear();
    g.vertices.set_dimension(3);
    g.vertices.create_vertices((GEO::index_t)m.nv());
    for (size_t i = 0; i < m.nv(); ++i)
        g.vertices.point((GEO::index_t)i) = GEO::vec3(m.v[3 * i], m.v[3 * i + 1], m.v[3 * i + 2]);
    g.facets.create_triangles((GEO::index_t)m.nt());
    for (size_t f = 0; f < m.nt(); ++f)
        for (GEO::index_t lv = 0; lv < 3; ++lv)
            g.facets.set_vertex((GEO::index_t)f, lv, m.t[3 * f + lv]);
}

// Coordinates are copied exactly as Geogram holds them. A non-triangular facet (not expected from the
// boolean) is fan-triangulated from its first vertex; the number of such facets is reported on stderr.
static void fromGeo(const GEO::Mesh& g, mesh::Mesh& m) {
    m.v.clear(); m.t.clear();
    m.v.reserve(g.vertices.nb() * 3);
    for (GEO::index_t v = 0; v < g.vertices.nb(); ++v) {
        const double* p = g.vertices.point_ptr(v);
        m.v.push_back(p[0]); m.v.push_back(p[1]); m.v.push_back(p[2]);
    }
    size_t nonTri = 0;
    for (GEO::index_t f = 0; f < g.facets.nb(); ++f) {
        GEO::index_t n = g.facets.nb_vertices(f);
        if (n != 3) ++nonTri;
        for (GEO::index_t k = 1; k + 1 < n; ++k) {
            m.addT(g.facets.vertex(f, 0), g.facets.vertex(f, k), g.facets.vertex(f, k + 1));
        }
    }
    if (nonTri) fprintf(stderr, "note: %zu non-triangular facets fan-triangulated\n", nonTri);
}

int main(int argc, char** argv) {
    // Keep stdout clean: Geogram's logger writes to stdout. Save the real stdout, point fd 1 at stderr.
    int realOut = dup(1);
    dup2(2, 1);
    FILE* out = fdopen(realOut, "w");

    if (argc < 2) { fprintf(stderr, "usage error\n"); return 4; }
    std::string cmd = argv[1];
    try {
        GEO::initialize(GEO::GEOGRAM_INSTALL_ALL);
        GEO::set_assert_mode(GEO::ASSERT_THROW);   // geo_assert failures become exceptions instead of abort()
        // Optional, outside the CLI contract: GEOGRAM_RUNNER_THREADS=<n> caps Geogram's worker threads (determinism study).
        if (const char* th = getenv("GEOGRAM_RUNNER_THREADS")) GEO::Process::set_max_threads((GEO::index_t)atoi(th));
        if (!getenv("GEOGRAM_RUNNER_LOG")) GEO::Logger::instance()->set_quiet(true);

        if (cmd == "boolean" && argc == 6) {
            std::string op = argv[2];
            const char* opName = op == "union" ? "A+B" : op == "intersection" ? "A*B" : op == "difference" ? "A-B" : nullptr;
            if (!opName) { fprintf(stderr, "bad operation '%s'\n", op.c_str()); return 4; }
            mesh::Mesh a, b;
            if (!mesh::readMesh(argv[3], a)) { fprintf(stderr, "cannot read %s\n", argv[3]); return 4; }
            if (!mesh::readMesh(argv[4], b)) { fprintf(stderr, "cannot read %s\n", argv[4]); return 4; }

            auto t0 = Clock::now();
            GEO::Mesh ga, gb, gr;
            toGeo(a, ga); toGeo(b, gb);
            double convertIn = msSince(t0);

            auto t1 = Clock::now();
            try {
                GEO::mesh_boolean_operation(gr, ga, gb, opName, GEO::MESH_BOOL_OPS_DEFAULT);
            } catch (const std::exception& e) {
                fprintf(stderr, "REFUSED: geogram threw: %s\n", e.what());
                return 3;
            }
            double boolMs = msSince(t1);

            auto t2 = Clock::now();
            mesh::Mesh r;
            fromGeo(gr, r);
            double convertOut = msSince(t2);
            fprintf(stderr, "convert_ms=%.3f\n", convertIn + convertOut);
            fprintf(stderr, "convert_in_ms=%.3f convert_out_ms=%.3f boolean_ms=%.3f out_nv=%zu out_nt=%zu\n",
                    convertIn, convertOut, boolMs, r.nv(), r.nt());
            if (!mesh::writeMesh(argv[5], r)) { fprintf(stderr, "cannot write %s\n", argv[5]); return 4; }
            return 0;
        }
        if (cmd == "measure" && argc == 3) {
            mesh::Mesh a;
            if (!mesh::readMesh(argv[2], a)) { fprintf(stderr, "cannot read %s\n", argv[2]); return 4; }
            GEO::Mesh g; toGeo(a, g);
            g.facets.connect();
            // Geom::mesh_enclosed_volume returns fabs(sum of dot(p1,cross(p2,p3))/6) in plain double arithmetic.
            double vol = GEO::Geom::mesh_enclosed_volume(g);
            double area = GEO::Geom::mesh_area(g, 3);
            GEO::signed_index_t nb = GEO::mesh_nb_borders(g);   // -1 on non-manifold border configuration
            int closed = (nb == 0) ? 1 : 0;
            fprintf(out, "volume=%.17g area=%.17g closed=%d\n", vol, area, closed);
            fflush(out);
            return 0;
        }
        fprintf(stderr, "usage: geogram_runner boolean <union|intersection|difference> A B out | measure A\n");
        return 4;
    } catch (const std::exception& e) {
        fprintf(stderr, "INTERNAL: exception: %s\n", e.what());
        return 4;
    } catch (...) {
        fprintf(stderr, "INTERNAL: unknown exception\n");
        return 4;
    }
}
