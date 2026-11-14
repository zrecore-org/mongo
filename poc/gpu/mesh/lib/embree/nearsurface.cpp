// nearsurface <mesh.msh> <out.txt> <ntri_sample>: points at centroid +- delta*unit normal for sampled triangles, delta in {1e-3..1e-8}.
// Output lines "x y z" (%.17g) plus a sidecar comment is not written; order is triangle-major, then delta, then sign (+,-).
#include "mesh_io.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
int main(int, char** v) {
    mesh::Mesh m; if (!mesh::readMesh(v[1], m)) return 1; size_t ns = atol(v[3]); FILE* f = fopen(v[2], "w");
    double deltas[] = {1e-3, 1e-5, 1e-6, 1e-7, 1e-8};
    for (size_t s = 0; s < ns; s++) {
        size_t t = s * (m.nt() / ns);
        const double *a = &m.v[3*(size_t)m.t[3*t]], *b = &m.v[3*(size_t)m.t[3*t+1]], *c = &m.v[3*(size_t)m.t[3*t+2]];
        double e1[3], e2[3], n[3], g[3];
        for (int k = 0; k < 3; k++) { e1[k] = b[k]-a[k]; e2[k] = c[k]-a[k]; g[k] = (a[k]+b[k]+c[k]) / 3; }
        n[0] = e1[1]*e2[2]-e1[2]*e2[1]; n[1] = e1[2]*e2[0]-e1[0]*e2[2]; n[2] = e1[0]*e2[1]-e1[1]*e2[0];
        double l = std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); for (int k = 0; k < 3; k++) n[k] /= l;
        for (double d : deltas) for (int sg = 1; sg >= -1; sg -= 2) fprintf(f, "%.17g %.17g %.17g\n", g[0]+sg*d*n[0], g[1]+sg*d*n[1], g[2]+sg*d*n[2]);
    }
    fclose(f);
}
