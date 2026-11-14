#include "mesh_io.h"
#include "exact.h"
#include "pip.h"
#include <cstdio>
using namespace mesh;
extern "C" void exactinit(void);
int main(int, char** argv) {
    exactinit();
    std::string d = argv[1], id = argv[2];
    Mesh A, B, U;
    readMesh(d + "/adversarial/" + id + "_A.msh", A); readMesh(d + "/adversarial/" + id + "_B.msh", B); readMesh(argv[3], U);
    auto v6 = [](const Mesh& m) { Dyadic x = exactVolumeTimes6(m); return x; };
    Dyadic a = v6(A), b = v6(B), u = v6(U), s = addExact(a, b);
    printf("6vol A=%s\n6vol B=%s\n6vol A+B=%s\n6vol Geogram union=%s\nunion == A+B exactly: %d\n", toString(a).c_str(), toString(b).c_str(), toString(s).c_str(), toString(u).c_str(), (int)equalExact(u, s));
    // B has 4 vertices (tetra): several interior points near its vertices, classified exactly against A.
    printf("B vertices %zu, triangles %zu\n", B.nv(), B.nt());
    for (double w : {0.5, 0.25, 0.1}) {
        double c[3] = {0, 0, 0};
        for (size_t i = 0; i < B.nv(); i++) for (int k = 0; k < 3; k++) c[k] += B.v[3*i+k];
        for (int k = 0; k < 3; k++) c[k] /= B.nv();
        double p[3]; for (int k = 0; k < 3; k++) p[k] = c[k] + w * (B.v[k] - c[k]);   // point between centroid and vertex 0
        Pip pa = classifyPoint(A, p), pb = classifyPoint(B, p);
        printf("w=%.2f p=(%g,%g,%g): in B=%d in A=%d\n", w, p[0], p[1], p[2], (int)pb.inside, (int)pa.inside);
    }
}
