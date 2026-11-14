// MP08b 3D Delaunay via Geogram (permissive libgeogram.a from storage/mesh/geogram: TetGen/Triangle/HLBFGS compiled out), "BDEL" = sequential exact-predicate Delaunay.
// Same input as the oracle (H3D1). Output: 'tet a b c d'. -DMUT=1 drops one tet, -DMUT=2 replaces a vertex index of the first tet. usage: run_geogram_del3d <case.h3d> <out>
#ifndef MUT
#define MUT 0
#endif
#include <geogram/basic/common.h>
#include <geogram/delaunay/delaunay.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
int main(int argc, char** argv) { if (argc < 3) return 2; std::ifstream in(argv[1]); std::string tok; in >> tok; std::vector<double> pts; size_t n = 0;
    while (in >> tok) { if (tok == "points") { in >> n; pts.resize(3 * n); for (size_t i = 0; i < 3 * n; i++) { long long v; in >> v; pts[i] = (double)v; } } else if (tok == "name" || tok == "family") in >> tok; }
    GEO::initialize(GEO::GEOGRAM_INSTALL_NONE); GEO::Delaunay_var d = GEO::Delaunay::create(3, "BDEL"); d->set_vertices((GEO::index_t)n, pts.data());
    FILE* f = fopen(argv[2], "w"); fprintf(f, "RES1 OK\n"); GEO::index_t nc = d->nb_cells();
    for (GEO::index_t c = 0; c < nc; c++) { if (MUT == 1 && c == 0) continue; GEO::index_t v[4]; for (int k = 0; k < 4; k++) v[k] = (GEO::index_t)d->cell_vertex(c, (GEO::index_t)k); if (MUT == 2 && c == 0) v[0] = (v[0] + 1) % (GEO::index_t)n; fprintf(f, "tet %u %u %u %u\n", v[0], v[1], v[2], v[3]); }
    fclose(f); return 0; }
