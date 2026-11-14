// scratch: Geogram boolean on one case under each documented flag combination; prints the exact-looking volume of the result via the library's own function
#include <geogram/basic/common.h>
#include <geogram/basic/command_line.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_surface_intersection.h>
#include <geogram/mesh/mesh_geometry.h>
#include <cstdio>
#include "../../../../poc/gpu/mesh/harness/mesh_io.h"
using namespace GEO;
static void toGeo(const mesh::Mesh& m, GEO::Mesh& g) {
    g.clear(); g.vertices.set_dimension(3); g.vertices.create_vertices((index_t)m.nv());
    for (size_t i = 0; i < m.nv(); ++i) g.vertices.point((index_t)i) = vec3(m.v[3*i], m.v[3*i+1], m.v[3*i+2]);
    g.facets.create_triangles((index_t)m.nt());
    for (size_t f = 0; f < m.nt(); ++f) for (index_t lv = 0; lv < 3; ++lv) g.facets.set_vertex((index_t)f, lv, m.t[3*f+lv]);
}
int main(int argc, char** argv) {
    GEO::initialize(GEO::GEOGRAM_INSTALL_ALL); GEO::Logger::instance()->set_quiet(true);
    mesh::Mesh a, b; mesh::readMesh(argv[1], a); mesh::readMesh(argv[2], b);
    const char* ops[] = {"A+B", "A*B", "A-B"}; int flags[] = {0, 4, 8, 12};
    for (const char* op : ops) for (int f : flags) {
        GEO::Mesh ga, gb, gr; toGeo(a, ga); toGeo(b, gb);
        try { mesh_boolean_operation(gr, ga, gb, op, (MeshBooleanOperationFlags)f);
              printf("op %s flags %2d: result facets %u, enclosed volume %.17g\n", op, f, gr.facets.nb(), gr.facets.nb() ? Geom::mesh_enclosed_volume(gr) : 0.0); }
        catch (const std::exception& e) { printf("op %s flags %2d: threw %s\n", op, f, e.what()); }
    }
}
