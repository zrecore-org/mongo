// MP24b dev check for generator design (after the freeze; changes nothing in the pipeline): which far configurations are NOT exactly re-centrable?
// gen_dev_axes <outdir>: smooth icospheres (L1, L2, L3, L3r) at x-only far offsets o = (s, 0.3, -0.2) ("x1") and x,y-far offsets o = (s, -0.625 s, -0.2) ("x2"), s = 2^(j+1), j = 0..30.
#define main gen_fidelity_main
#include "../../../../corpus/fidelity/gen_fidelity.cpp"
#undef main
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    openCorpus(std::string(argv[1]));
    struct Shape { const char* n; int lvl; double r; };
    const Shape S[4] = {{"L1", 1, 1.0}, {"L2", 2, 1.0}, {"L3", 3, 1.0}, {"L3r", 3, 1.1}};
    for (const Shape& sh : S) for (int mode = 1; mode <= 2; mode++) for (int j = 0; j <= 30; j++) {
        Mesh base = makeIcosphere(sh.lvl, sh.r, false);
        double s = std::ldexp(1.0, j + 1); double o[3] = {s, mode == 1 ? 0.3 : -0.625 * s, -0.2};
        Mesh A = translated(base, o); double d[3] = {0.125 * sh.r, 0.0625, -0.09375};
        std::string id = std::string("dax_") + sh.n + "_x" + std::to_string(mode) + "_j" + (j < 10 ? "0" : "") + std::to_string(j);
        meshPair(id + "_ident", "dax", "valid,curved,identical", A, A, true);
        meshPair(id + "_shift", "dax", "valid,curved,overlap", A, translated(A, d), false);
    }
    closeCorpus("dax"); return 0;
}
