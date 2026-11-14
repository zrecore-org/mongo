// Runtime availability probe for the permissive build (not part of the runner contract).
#include <geogram/basic/common.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/delaunay/delaunay.h>
#include <geogram/numerics/optimizer.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_AABB.h>
#include <geogram/mesh/mesh_convex_hull.h>
#include <geogram/mesh/mesh_tetrahedralize.h>
#include <geogram/mesh/mesh_remesh.h>
#include <geogram/mesh/mesh_decimate.h>
#include <geogram/mesh/mesh_geometry.h>
#include <iostream>
#include <typeinfo>
#include <string>
using namespace GEO;
static void tryD(index_t dim, const char* name) {
    try { Delaunay_var d = Delaunay::create(dim, name); std::cout << "Delaunay::create(" << (int)dim << ",\"" << name << "\") -> " << (d.is_null() ? std::string("null") : std::string("ok, dynamic type ") + typeid(*d.get()).name()) << "\n"; }
    catch (const std::exception& e) { std::cout << "Delaunay::create(" << (int)dim << ",\"" << name << "\") threw: " << e.what() << "\n"; }
}
int main() {
    GEO::initialize(GEO::GEOGRAM_INSTALL_ALL); set_assert_mode(ASSERT_THROW);
    CmdLine::import_arg_group("standard"); CmdLine::import_arg_group("algo"); CmdLine::import_arg_group("remesh");
    Logger::instance()->set_quiet(true);
    tryD(3, "tetgen"); tryD(2, "triangle"); tryD(3, "BDEL"); tryD(3, "PDEL"); tryD(2, "BDEL2d");
    std::cout << "Optimizer::create(\"HLBFGS\") -> " << (Optimizer_var(Optimizer::create("HLBFGS")).is_null() ? "null (unavailable)" : "ok") << "\n";
    // unit cube surface
    Mesh M; M.vertices.set_dimension(3); M.vertices.create_vertices(8);
    for (index_t i = 0; i < 8; ++i) M.vertices.point(i) = vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
    M.facets.create_triangles(12);
    static const index_t T[12][3] = {{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}};
    for (index_t f = 0; f < 12; ++f) for (index_t k = 0; k < 3; ++k) M.facets.set_vertex(f, k, T[f][k]);
    M.facets.connect();
    try { MeshFacetsAABB aabb(M); std::cout << "AABB contains(0.5,0.5,0.5)=" << aabb.contains(vec3(.5,.5,.5)) << " contains(2,2,2)=" << aabb.contains(vec3(2,2,2)) << " squared_distance(2,0.5,0.5)=" << aabb.squared_distance(vec3(2,.5,.5)) << "\n"; }
    catch (const std::exception& e) { std::cout << "AABB threw: " << e.what() << "\n"; }
    try { Mesh P; P.vertices.set_dimension(3); P.vertices.create_vertices(5); double c[5][3]={{0,0,0},{1,0,0},{0,1,0},{0,0,1},{.1,.1,.1}}; for(index_t i=0;i<5;++i) P.vertices.point(i)=vec3(c[i][0],c[i][1],c[i][2]); compute_convex_hull_3d(P); std::cout << "compute_convex_hull_3d ok: facets=" << P.facets.nb() << " enclosed_volume=" << Geom::mesh_enclosed_volume(P) << "\n"; }
    catch (const std::exception& e) { std::cout << "convex_hull threw: " << e.what() << "\n"; }
    try { MeshTetrahedralizeParameters p; p.verbose = false; Mesh Q; Q.copy(M); bool ok = mesh_tetrahedralize(Q, p); std::cout << "mesh_tetrahedralize returned " << ok << " cells=" << Q.cells.nb() << "\n"; }
    catch (const std::exception& e) { std::cout << "mesh_tetrahedralize threw: " << e.what() << "\n"; }
    try { Mesh R; remesh_smooth(M, R, 50); std::cout << "remesh_smooth ok: out facets=" << R.facets.nb() << "\n"; }
    catch (const std::exception& e) { std::cout << "remesh_smooth threw: " << e.what() << "\n"; }
    try { Mesh D; D.copy(M); mesh_decimate_vertex_clustering(D, 4); std::cout << "mesh_decimate_vertex_clustering ok: facets=" << D.facets.nb() << "\n"; }
    catch (const std::exception& e) { std::cout << "decimate threw: " << e.what() << "\n"; }
}
