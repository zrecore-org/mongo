// MP18 step 2 probe: geogram_runner-like boolean with selectable flags, swap of operands and Geogram's own verbose log on stderr.
//   probe_mp18 <union|intersection|difference> A.msh B.msh out.msh [verbose] [nocheck] [swap]
// (swap runs the operation on (B,A) and, for difference, B-A is NOT what is wanted: use it with union/intersection only.)
#include "../../../harness/mesh_io.h"
#include <geogram/basic/common.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/process.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_surface_intersection.h>
#include <cstdio>
#include <cstdlib>
#include <string>
static void toGeo(const mesh::Mesh& m, GEO::Mesh& g) {
    g.clear(); g.vertices.set_dimension(3); g.vertices.create_vertices((GEO::index_t)m.nv());
    for (size_t i = 0; i < m.nv(); ++i) g.vertices.point((GEO::index_t)i) = GEO::vec3(m.v[3*i], m.v[3*i+1], m.v[3*i+2]);
    g.facets.create_triangles((GEO::index_t)m.nt());
    for (size_t f = 0; f < m.nt(); ++f) for (GEO::index_t lv = 0; lv < 3; ++lv) g.facets.set_vertex((GEO::index_t)f, lv, m.t[3*f+lv]);
}
int main(int argc, char** argv) {
    if (argc < 5) return 4;
    GEO::initialize(GEO::GEOGRAM_INSTALL_ALL); GEO::set_assert_mode(GEO::ASSERT_THROW); GEO::Process::set_max_threads(1); GEO::Logger::instance()->set_quiet(false);
    std::string op = argv[1]; bool verbose = false, nocheck = false, swap = false;
    for (int i = 5; i < argc; i++) { std::string s = argv[i]; verbose |= s == "verbose"; nocheck |= s == "nocheck"; swap |= s == "swap"; }
    mesh::Mesh a, b; if (!mesh::readMesh(argv[2], a) || !mesh::readMesh(argv[3], b)) return 4;
    GEO::Mesh ga, gb, gr; toGeo(a, ga); toGeo(b, gb);
    int flags = GEO::MESH_BOOL_OPS_DEFAULT | (verbose ? GEO::MESH_BOOL_OPS_VERBOSE : 0) | (nocheck ? GEO::MESH_BOOL_OPS_NO_CHECK_NEIGHBORS : 0);
    const char* e = op == "union" ? "A+B" : op == "intersection" ? "A*B" : "A-B";
    try { GEO::mesh_boolean_operation(gr, swap ? gb : ga, swap ? ga : gb, e, (GEO::MeshBooleanOperationFlags)flags); }
    catch (const std::exception& x) { fprintf(stderr, "REFUSED %s\n", x.what()); return 3; }
    mesh::Mesh r;
    for (GEO::index_t v = 0; v < gr.vertices.nb(); ++v) { const double* p = gr.vertices.point_ptr(v); r.addV(p[0], p[1], p[2]); }
    for (GEO::index_t f = 0; f < gr.facets.nb(); ++f) for (GEO::index_t k = 1; k + 1 < gr.facets.nb_vertices(f); ++k) r.addT(gr.facets.vertex(f,0), gr.facets.vertex(f,k), gr.facets.vertex(f,k+1));
    fprintf(stderr, "out_nv=%zu out_nt=%zu\n", r.nv(), r.nt());
    return mesh::writeMesh(argv[4], r) ? 0 : 4;
}
