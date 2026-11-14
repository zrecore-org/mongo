// MP26 pair builder (seeded, one rnd() per statement, build with -ffp-contract=off). A is the model unchanged; B is
//   perm: P (p - c) + c + off, P = one of the 24 signed axis permutations with determinant +1 (exact on the coordinates),  c = bbox centre of A, off = per axis (rnd - 0.5) * extent, rounded to a multiple of 2^-10
//   rot : R (p - c) + c + off, R = random rotation from a uniform quaternion (Shoemake), double arithmetic,                  off as above
//   diff: s (p - cB) + cA + off, s = diag(A) / diag(B) * (0.6 + 0.8 rnd), off per axis (rnd - 0.5) * 0.5 * extent(A) rounded to 2^-10 (B is a different model)
//   pair_build <perm|rot|diff> <seed> <modelA.msh> <modelB.msh or -> <outA.msh> <outB.msh>      prints the parameters on stdout
#include "../../harness/mesh_io.h"
#include <cmath>
#include <algorithm>
using namespace mesh;
static uint64_t st;
static double rnd() { uint64_t z = (st += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; z ^= z >> 31; return (double)(z >> 11) / 9007199254740992.0; }
static void bbox(const Mesh& m, double* lo, double* hi) { for (int k = 0; k < 3; k++) { lo[k] = 1e300; hi[k] = -1e300; } for (size_t i = 0; i < m.nv(); i++) for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], m.v[3*i+k]); hi[k] = std::max(hi[k], m.v[3*i+k]); } }
int main(int argc, char** argv) {
    if (argc != 7) return 2;
    std::string kind = argv[1]; st = strtoull(argv[2], 0, 10);
    Mesh A, M2; if (!readMesh(argv[3], A)) return 2;
    double lo[3], hi[3], c[3], ext[3], off[3];
    bbox(A, lo, hi); for (int k = 0; k < 3; k++) { c[k] = lo[k] / 2 + hi[k] / 2; ext[k] = hi[k] - lo[k]; }
    Mesh B;
    char info[400];
    if (kind == "diff") {
        if (!readMesh(argv[4], M2)) return 2;
        double lo2[3], hi2[3], c2[3]; bbox(M2, lo2, hi2); for (int k = 0; k < 3; k++) c2[k] = lo2[k] / 2 + hi2[k] / 2;
        double dA = std::sqrt(ext[0]*ext[0] + ext[1]*ext[1] + ext[2]*ext[2]);
        double dB = std::sqrt((hi2[0]-lo2[0])*(hi2[0]-lo2[0]) + (hi2[1]-lo2[1])*(hi2[1]-lo2[1]) + (hi2[2]-lo2[2])*(hi2[2]-lo2[2]));
        double f = 0.6 + 0.8 * rnd();
        double s = dA / dB * f;
        for (int k = 0; k < 3; k++) { double r = rnd(); off[k] = std::nearbyint((r - 0.5) * 0.5 * ext[k] * 1024) / 1024; }
        B = M2;
        for (size_t i = 0; i < B.nv(); i++) for (int k = 0; k < 3; k++) { double d = B.v[3*i+k] - c2[k]; double t = s * d; B.v[3*i+k] = t + c[k] + off[k]; }
        snprintf(info, sizeof info, "scale=%.17g off=(%.17g,%.17g,%.17g)", s, off[0], off[1], off[2]);
    } else {
        for (int k = 0; k < 3; k++) { double r = rnd(); off[k] = std::nearbyint((r - 0.5) * ext[k] * 1024) / 1024; }
        double R[3][3];
        if (kind == "perm") {
            int perms[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}}; int parity[6] = {1,-1,-1,1,1,-1};
            std::vector<std::pair<int,int>> ok;   // (perm index, sign mask) with det +1
            for (int p = 0; p < 6; p++) for (int s = 0; s < 8; s++) { int sg = 1; for (int k = 0; k < 3; k++) if (s >> k & 1) sg = -sg; if (parity[p] * sg == 1) ok.push_back({p, s}); }
            double r = rnd(); size_t pick = std::min((size_t)(r * ok.size()), ok.size() - 1);
            for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) R[i][j] = 0;
            for (int i = 0; i < 3; i++) R[i][perms[ok[pick].first][i]] = (ok[pick].second >> i & 1) ? -1.0 : 1.0;
            snprintf(info, sizeof info, "perm=%d,%d,%d signs=%d%d%d off=(%.17g,%.17g,%.17g)", perms[ok[pick].first][0], perms[ok[pick].first][1], perms[ok[pick].first][2], ok[pick].second & 1, ok[pick].second >> 1 & 1, ok[pick].second >> 2 & 1, off[0], off[1], off[2]);
        } else {
            double u1 = rnd(); double u2 = rnd(); double u3 = rnd();
            double a = std::sqrt(1 - u1), b = std::sqrt(u1), t2 = 6.283185307179586 * u2, t3 = 6.283185307179586 * u3;
            double x = a * std::sin(t2), y = a * std::cos(t2), z = b * std::sin(t3), w = b * std::cos(t3);
            R[0][0] = 1 - 2*(y*y + z*z); R[0][1] = 2*(x*y - z*w);     R[0][2] = 2*(x*z + y*w);
            R[1][0] = 2*(x*y + z*w);     R[1][1] = 1 - 2*(x*x + z*z); R[1][2] = 2*(y*z - x*w);
            R[2][0] = 2*(x*z - y*w);     R[2][1] = 2*(y*z + x*w);     R[2][2] = 1 - 2*(x*x + y*y);
            snprintf(info, sizeof info, "quat=(%.17g,%.17g,%.17g,%.17g) off=(%.17g,%.17g,%.17g)", x, y, z, w, off[0], off[1], off[2]);
        }
        B = A;
        for (size_t i = 0; i < B.nv(); i++) {
            double d[3] = {A.v[3*i] - c[0], A.v[3*i+1] - c[1], A.v[3*i+2] - c[2]};
            for (int k = 0; k < 3; k++) { double t0 = R[k][0] * d[0]; double t1 = R[k][1] * d[1]; double t2 = R[k][2] * d[2]; double sum = t0 + t1; sum = sum + t2; B.v[3*i+k] = sum + c[k] + off[k]; }
        }
    }
    if (!writeMesh(argv[5], A) || !writeMesh(argv[6], B)) return 2;
    printf("%s\n", info);
    return 0;
}
