// refpip <mesh.msh> <points.txt>: simple O(n) double-precision ray parity (same direction as embree_runner, rounded through float32).
// A plain reference, NOT claimed exact (Moller-Trumbore in binary64 with rounding). Prints 0/1 per point; with --verbose prints nothing else.
#include "mesh_io.h"
#include <cmath>
#include <cstdio>
int main(int, char** argv) {
    mesh::Mesh m; if (!mesh::readMesh(argv[1], m)) return 1;
    FILE* f = fopen(argv[2], "r"); double dl = std::sqrt(1 + 0.001 * 0.001 + 0.0003 * 0.0003);
    double d[3] = {(double)(float)(1 / dl), (double)(float)(0.001 / dl), (double)(float)(0.0003 / dl)};
    double o[3]; long inside = 0, n = 0;
    while (fscanf(f, "%lf %lf %lf", o, o + 1, o + 2) == 3) {
        int hits = 0;
        for (size_t t = 0; t < m.nt(); t++) {
            const double *a = &m.v[3*(size_t)m.t[3*t]], *b = &m.v[3*(size_t)m.t[3*t+1]], *c = &m.v[3*(size_t)m.t[3*t+2]];
            double e1[3], e2[3], p[3], s[3], q[3];
            for (int k = 0; k < 3; k++) { e1[k] = b[k]-a[k]; e2[k] = c[k]-a[k]; }
            p[0] = d[1]*e2[2]-d[2]*e2[1]; p[1] = d[2]*e2[0]-d[0]*e2[2]; p[2] = d[0]*e2[1]-d[1]*e2[0];
            double det = e1[0]*p[0]+e1[1]*p[1]+e1[2]*p[2]; if (det == 0) continue;
            double inv = 1 / det; for (int k = 0; k < 3; k++) s[k] = o[k]-a[k];
            double u = (s[0]*p[0]+s[1]*p[1]+s[2]*p[2]) * inv; if (u < 0 || u > 1) continue;
            q[0] = s[1]*e1[2]-s[2]*e1[1]; q[1] = s[2]*e1[0]-s[0]*e1[2]; q[2] = s[0]*e1[1]-s[1]*e1[0];
            double v = (d[0]*q[0]+d[1]*q[1]+d[2]*q[2]) * inv; if (v < 0 || u + v > 1) continue;
            double tt = (e2[0]*q[0]+e2[1]*q[1]+e2[2]*q[2]) * inv; if (tt > 0) hits++;
        }
        printf("%d\n", hits & 1); inside += hits & 1; n++;
    }
    fprintf(stderr, "points %ld inside %ld\n", n, inside);
}
