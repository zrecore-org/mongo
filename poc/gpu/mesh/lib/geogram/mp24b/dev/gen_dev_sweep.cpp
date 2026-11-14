// MP24b DEVELOPMENT sweep corpus (not an acceptance set; chooses T). gen_dev_sweep <outdir>
// Full-mantissa curved meshes (icosphere levels 1-3, spiky, radius 1.1) at bounding-box centre / largest-side ratios 2^0..2^30 (j = 0..30), each as
// (a) the mesh against itself (identical; exact volumes) and (b) against the same mesh shifted by a dyadic (rounded where the sum is inexact; point tests, topology, orientation).
// Build: c++ -std=c++17 -O2 -ffp-contract=off -I<boost...> gen_dev_sweep.cpp
#define main gen_fidelity_main
#include "../../../../corpus/fidelity/gen_fidelity.cpp"
#undef main
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    openCorpus(std::string(argv[1]));
    struct Shape { const char* n; int lvl; double r; bool spiky; };
    const Shape S[5] = {{"L1", 1, 1.0, false}, {"L2", 2, 1.0, false}, {"L3", 3, 1.0, false}, {"L2sp", 2, 1.0, true}, {"L3r", 3, 1.1, false}};
    for (const Shape& sh : S) for (int j = 0; j <= 30; j++) {
        Mesh base = makeIcosphere(sh.lvl, sh.r, sh.spiky);
        double s = std::ldexp(1.0, j + 1); double o[3] = {s, -0.625 * s, 0.5 * s};
        Mesh A = translated(base, o);
        double U = 0; for (int k = 0; k < 3; k++) U = std::max(U, ulpOf(std::fabs(o[k]) + 4.0));
        double d[3]; for (int k = 0; k < 3; k++) d[k] = std::nearbyint(std::ldexp(0.125 * sh.r, 0) / U) * U * (k == 1 ? -1 : 1);
        bool exact = true; for (size_t i = 0; i < A.nv(); i++) for (int k = 0; k < 3; k++) if (twoSumErr(A.v[3*i+k], d[k]) != 0) exact = false;
        std::string id = std::string("dev_") + sh.n + "_j" + (j < 10 ? "0" : "") + std::to_string(j);
        meshPair(id + "_ident", "dev", "valid,dev,curved,identical", A, A, true);
        meshPair(id + "_shift", "dev", "valid,dev,curved,overlap", A, translated(A, d), false); (void)exact;
    }
    closeCorpus("dev"); return 0;
}
