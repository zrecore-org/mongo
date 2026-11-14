// MP03 speed probe: union of a mesh with itself translated by (dx,dy,dz); prints seconds per repetition.
//   manifold_bench <A.msh> <dx> <dy> <dz> <reps>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "../../harness/mesh_io.h"
#include "manifold/manifold.h"
using namespace manifold;
int main(int argc, char** argv) {
    if (argc != 6) { fprintf(stderr, "usage\n"); return 4; }
    mesh::Mesh m;
    if (!mesh::readMesh(argv[1], m)) return 4;
    double d[3] = {atof(argv[2]), atof(argv[3]), atof(argv[4])};
    int reps = atoi(argv[5]);
    MeshGL64 a, b;
    a.numProp = b.numProp = 3;
    a.vertProperties = m.v; b.vertProperties = m.v;
    for (size_t i = 0; i < m.nv(); i++) for (int k = 0; k < 3; k++) b.vertProperties[i * 3 + k] += d[k];
    a.triVerts.assign(m.t.begin(), m.t.end()); b.triVerts = a.triVerts;
    for (int r = 0; r < reps; r++) {
        auto t0 = std::chrono::steady_clock::now();
        Manifold A(a), B(b);
        Manifold R = A + B;
        MeshGL64 g = R.GetMeshGL64();
        auto t1 = std::chrono::steady_clock::now();
        printf("rep %d: %.6f s  (construct+union+extract) status=%d tris=%zu verts=%zu\n", r,
               std::chrono::duration<double>(t1 - t0).count(), (int)R.Status(), g.triVerts.size() / 3, g.NumVert());
    }
}
