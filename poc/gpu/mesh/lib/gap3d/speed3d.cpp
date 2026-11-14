// MP07 speed: one thread, median of 5, operation only (input conversion and mesh construction excluded; Manifold properties are evaluated lazily, so each repetition builds a fresh Manifold outside the timer and
// times the first query). The load average is read with getloadavg() before every operation and printed on the same line.  usage: speed3d <speed-dir> <out.tsv>
#include "own3d.h"
#include <CDT.h>
#include "manifold/manifold.h"
#include <chrono>
#include <cstdlib>
using namespace manifold;
typedef std::chrono::steady_clock Clk;
static FILE* out;
template <class Prep, class Op> static void bench(const char* what, const char* route, int reps, Prep prep, Op op, size_t outTris = 0) {
    double la[3]; getloadavg(la, 3); std::vector<double> ms;
    for (int i = 0; i < reps; i++) { auto ctx = prep(); auto t0 = Clk::now(); op(ctx); ms.push_back(std::chrono::duration<double, std::milli>(Clk::now() - t0).count()); }
    std::sort(ms.begin(), ms.end()); fprintf(out, "%s\t%s\t%.3f\t%.3f\t%.3f\t%.2f\t%zu\n", what, route, ms[ms.size() / 2], ms.front(), ms.back(), la[0], outTris); fflush(out); fprintf(stderr, "%s %s %.3f ms\n", what, route, ms[ms.size() / 2]); }
static std::vector<std::array<long long, 2>> zigzag(int W) { std::vector<std::array<long long, 2>> p; p.push_back({0, 0}); p.push_back({W, 0}); for (int x = W; x >= 0; x--) p.push_back({x, (x & 1) ? 6 : 12}); return p; }
static Manifold fromMesh(const mesh::Mesh& m) { MeshGL64 g; g.numProp = 3; g.vertProperties = m.v; for (auto t : m.t) g.triVerts.push_back(t); return Manifold(g); }
static Manifold prismParab(int k, double dz) { Polygons P; SimplePolygon s; for (int i = -k; i <= k; i++) s.push_back(vec2(i, (double)i * i)); P.push_back(s); (void)dz;
    // convex polygon (i, i^2) closed by its chord is counter-clockwise already
    return Manifold::Extrude(P, 1.0); }
int main(int argc, char** argv) {
    std::string dir = argv[1]; out = fopen(argv[2], "w"); fprintf(out, "operation\troute\tmedian_ms\tmin_ms\tmax_ms\tloadavg1\tresult_triangles\n");
    // ---- extrude, ~1e5 result triangles (25003-vertex profile: 2*25001 cap + 2*25003 wall) ----
    { auto zz = zigzag(25000); Polygons P; SimplePolygon s; for (auto& q : zz) s.push_back(vec2((double)q[0], (double)q[1])); P.push_back(s);
      c3d::Case c; c3d::Ring r; r.dim = 2; for (auto& q : zz) r.p.push_back({q[0], q[1], 0}); c.rings.push_back(r); c.param["vec"] = {0, 0, 7}; c.exp = 0;
      size_t nt = 0;
      bench("extrude_25k_profile", "manifold", 5, [&] { return 0; }, [&](int) { Manifold M = Manifold::Extrude(P, 7.0); nt = M.NumTri(); }, 0); { Manifold M = Manifold::Extrude(P, 7.0); nt = M.NumTri(); }
      bench("extrude_25k_profile", "own_earcut", 5, [&] { return 0; }, [&](int) { mesh::Mesh m; own::extrude(c, m); nt = m.nt(); }, nt); fprintf(stderr, "extrude tris manifold %zu own %zu\n", nt, nt); }
    // ---- tessellation of a 3D planar polygon with 1e5 vertices ----
    { auto zz = zigzag(100000); c3d::Case c; c3d::Ring r; r.dim = 3; for (auto& q : zz) r.p.push_back({q[0] + 2 * q[1], q[1], 3 * q[0] - q[1]}); c.rings.push_back(r);   // plane through the integer lattice, normal not axis aligned
      size_t nt = 0; bench("tessellate_1e5_vertices", "earcut", 5, [&] { return 0; }, [&](int) { std::vector<std::array<std::array<double, 3>, 3>> o; own::tessEarcut(c, o); nt = o.size(); }, 0);
      { std::vector<std::array<std::array<double, 3>, 3>> o; own::tessEarcut(c, o); nt = o.size(); }
      own::Proj p = own::makeProj(c); std::vector<CDT::V2d<double>> pts; std::vector<CDT::Edge> edges; for (size_t i = 0; i < r.p.size(); i++) { auto a = own::proj2(p, r.p[i]); pts.push_back(CDT::V2d<double>(a[0], a[1])); edges.push_back(CDT::Edge((CDT::VertInd)i, (CDT::VertInd)((i + 1) % r.p.size()))); }
      size_t nc = 0; bench("tessellate_1e5_vertices", "cdt", 5, [&] { return 0; }, [&](int) { auto pp = pts; auto ee = edges; CDT::RemoveDuplicatesAndRemapEdges(pp, ee); CDT::Triangulation<double> t; t.insertVertices(pp); t.insertEdges(ee); t.eraseOuterTrianglesAndHoles(); nc = t.triangles.size(); }, 0);
      fprintf(out, "tessellate_1e5_vertices\tresult_triangles\t%zu(earcut)/%zu(cdt)\t\t\t\t\n", nt, nc); }
    // ---- revolve: 251-vertex convex profile x 200 segments ----
    { int k = 125; Polygons P; SimplePolygon s; for (int i = -k; i <= k; i++) s.push_back(vec2(i + k + 1, (double)i * i)); P.push_back(s); size_t nt = 0;
      bench("revolve_251x200", "manifold", 5, [&] { return 0; }, [&](int) { Manifold M = Manifold::Revolve(P, 200, 360); nt = M.NumTri(); }, 0); fprintf(out, "revolve_251x200\tresult_triangles\t%zu\t\t\t\t\n", nt); }
    // ---- Minkowski / hull of 98596 pairwise sums (314-vertex prisms) ----
    { Manifold A = prismParab(78, 0), B = prismParab(78, 0); fprintf(stderr, "prism verts %zu tris %zu\n", A.NumVert(), A.NumTri()); size_t nt = 0;
      Manifold Bt = B.Translate(vec3(-78, 0, 0)); // contains the origin: vertex (0,0,0) is a vertex of B after the shift (i=0 -> x=0,y=0)
      bench("minkowski_convex_98k_sums", "manifold_MinkowskiSum", 5, [&] { return 0; }, [&](int) { Manifold M = A.MinkowskiSum(Bt); nt = M.NumTri(); }, 0);
      std::vector<vec3> pts; mesh::Mesh ma, mb; { MeshGL64 ga = A.GetMeshGL64(), gb = B.GetMeshGL64(); for (size_t i = 0; i < ga.vertProperties.size(); i += 3) for (size_t j = 0; j < gb.vertProperties.size(); j += 3) pts.push_back(vec3(ga.vertProperties[i] + gb.vertProperties[j], ga.vertProperties[i + 1] + gb.vertProperties[j + 1], ga.vertProperties[i + 2] + gb.vertProperties[j + 2])); }
      bench("minkowski_convex_98k_sums", "manifold_Hull(sums)", 5, [&] { return 0; }, [&](int) { Manifold M = Manifold::Hull(pts); nt = M.NumTri(); }, 0); fprintf(out, "minkowski_convex_98k_sums\tresult_triangles(hull)\t%zu\t\t\t\t\n", nt); fprintf(stderr, "sums %zu\n", pts.size()); }
    // NOTE: with searchLength 5000 (larger than the whole shape) one MinGap call on 1e5 x 1e5 triangles did not finish in 8 minutes (killed; first speed run), so the timed call uses 120.
    // ---- distance and measures on the 1e5-triangle meshes ----
    mesh::Mesh ma, mb; mesh::readMesh(dir + "/speed_cube_1e5.msh", ma); mesh::readMesh(dir + "/speed_cube_1e5_B.msh", mb);
    own::DMesh da = own::fromMesh(ma), db = own::fromMesh(mb);
    bench("bvh_build_1e5_tris", "own", 5, [&] { return 0; }, [&](int) { own::Bvh b; b.build(da.v, da.t); }, 0); da.bvh.build(da.v, da.t); db.bvh.build(db.v, db.t);
    { size_t i = 0; bench("point_mesh_distance_1000_queries", "own_bvh", 5, [&] { return 0; }, [&](int) { double s = 0; for (int q = 0; q < 1000; q++) { own::D3 p = {37.0 * q - 20000 + (double)(i++ % 7), 1100.5 + (double)(q % 50), 500.25 + (double)(q % 13)}; s += own::pointMeshD(da, p); } if (s < 0) puts("x"); }, 0); }
    bench("mesh_mesh_distance_1e5_vs_1e5", "own_bvh", 5, [&] { return 0; }, [&](int) { double d = own::meshMeshD(da, db, false); fprintf(stderr, "own mm %.17g\n", d); }, 0);
    { MeshGL64 g1, g2; g1.numProp = g2.numProp = 3; g1.vertProperties = ma.v; g2.vertProperties = mb.v; for (auto t : ma.t) g1.triVerts.push_back(t); for (auto t : mb.t) g2.triVerts.push_back(t);
      bench("mesh_mesh_distance_1e5_vs_1e5", "manifold_MinGap(searchLength=120, true gap 94; incl. boolean)", 5, [&] { return std::make_pair(Manifold(g1), Manifold(g2)); }, [&](std::pair<Manifold, Manifold>& p) { double d = p.first.MinGap(p.second, 120); fprintf(stderr, "manifold mingap %.17g\n", d); }, 0);
      bench("measures_1e5", "manifold_Volume", 5, [&] { return Manifold(g1); }, [&](Manifold& M) { volatile double v = M.Volume(); (void)v; }, 0);
      bench("measures_1e5", "manifold_SurfaceArea", 5, [&] { return Manifold(g1); }, [&](Manifold& M) { volatile double v = M.SurfaceArea(); (void)v; }, 0);
      bench("measures_1e5", "manifold_BoundingBox", 5, [&] { return Manifold(g1); }, [&](Manifold& M) { Box b = M.BoundingBox(); volatile double v = b.max[0]; (void)v; }, 0); }
    bench("measures_1e5", "own_volume", 5, [&] { return 0; }, [&](int) { volatile double v = own::volumeD(da); (void)v; }, 0);
    bench("measures_1e5", "own_area", 5, [&] { return 0; }, [&](int) { volatile double v = own::areaD(da); (void)v; }, 0);
    bench("measures_1e5", "own_centroid", 5, [&] { return 0; }, [&](int) { volatile double v = own::centroidD(da)[0]; (void)v; }, 0);
    fclose(out); return 0; }
