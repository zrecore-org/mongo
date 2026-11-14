// MP17 step 3 census: the integer width each valid corpus pair needs on a common lattice.
//   census <corpus-dir-with-manifest.tsv-and-adversarial/> [name ...]
// For each pair (A,B) every coordinate is an exact integer multiple of 2^emin (emin = smallest exponent of any nonzero
// mantissa bit pattern, mantissa taken as the 53-bit integer from frexp, as in harness/exact.h). Reported per pair:
//   latticeBitsRaw  = bit length of max |integer coordinate|            (no re-centring)
//   latticeBitsSpan = bit length of the largest per-axis extent (max-min) (after an exact integer translation to the centre,
//                     every coordinate fits in this many bits plus sign)
// Nothing is rounded or snapped: this only measures the width an exact integer representation would need.
#include "../../harness/mesh_io.h"
#include "../../harness/exact.h"
#include <cstdio>
#include <string>
#include <set>
#include <cmath>
using namespace mesh;
static bool load(const std::string& p, Mesh& m) { return readMesh(p, m); }
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string dir = argv[1];
    std::set<std::string> want; for (int i = 2; i < argc; i++) want.insert(argv[i]);
    FILE* mf = fopen((dir + "/manifest.tsv").c_str(), "r"); if (!mf) { fprintf(stderr, "no manifest\n"); return 2; }
    char line[8192]; fgets(line, sizeof line, mf);
    printf("case\tlatticeBitsRaw\tlatticeBitsSpan\temin\tmaxAbsBits\n");
    while (fgets(line, sizeof line, mf)) {
        std::string s(line); size_t t1 = s.find('\t'); std::string id = s.substr(0, t1);
        size_t t2 = s.find('\t', t1 + 1), t3 = s.find('\t', t2 + 1);
        std::string tags = s.substr(t2 + 1, t3 - t2 - 1);
        if (!want.empty() ? !want.count(id) : tags.find("valid") == std::string::npos) continue;
        Mesh a, b;
        std::string base = dir + "/adversarial/" + id;
        if (!load(base + "_A.msh", a) || !load(base + "_B.msh", b)) { fprintf(stderr, "skip %s\n", id.c_str()); continue; }
        std::vector<double> all(a.v); all.insert(all.end(), b.v.begin(), b.v.end());
        int emin = INT32_MAX; std::vector<int64_t> mant(all.size()); std::vector<int> ex(all.size());
        for (size_t i = 0; i < all.size(); i++) {
            int e; double f = std::frexp(all[i], &e); mant[i] = (int64_t)std::ldexp(f, 53);
            // strip trailing zero bits: the lattice step is set by the lowest SET bit, not by the 53-bit field width
            int64_t m = mant[i]; int tz = 0; if (m) { while (!(m & 1)) { m >>= 1; tz++; } }
            mant[i] = m; ex[i] = m ? e - 53 + tz : 0;
            if (m && ex[i] < emin) emin = ex[i];
        }
        if (emin == INT32_MAX) emin = 0;
        BigInt lo[3], hi[3]; bool first = true; BigInt maxAbs = 0;
        for (size_t i = 0; i < all.size(); i++) {
            BigInt v = mant[i]; if (mant[i]) v <<= (ex[i] - emin);
            int ax = i % 3;
            if (first || true) {}
            static bool init[3] = {false, false, false};
            if (i < 3) init[ax] = false;
            if (!init[ax]) { lo[ax] = hi[ax] = v; init[ax] = true; }
            if (v < lo[ax]) lo[ax] = v; if (v > hi[ax]) hi[ax] = v;
            BigInt av = v < 0 ? -v : v; if (av > maxAbs) maxAbs = av;
        }
        BigInt ext = 0; for (int k = 0; k < 3; k++) { BigInt e = hi[k] - lo[k]; if (e > ext) ext = e; }
        auto bits = [](const BigInt& x) { return x == 0 ? 0u : (unsigned)boost::multiprecision::msb(x) + 1; };
        printf("%s\t%u\t%u\t%d\t%u\n", id.c_str(), bits(maxAbs), bits(ext), emin, bits(maxAbs));
    }
}
