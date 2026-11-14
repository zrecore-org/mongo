// MP08b 3D corpus generator (60 cases, integer coordinates, splitmix64, every draw its own statement, -ffp-contract=off). Format:
//   H3D1 / name N / family F / points M / M lines 'x y z'. usage: gen_hard3d <out-dir>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <set>
#include <array>
#include <sys/stat.h>
typedef long long i64;
struct Rng { uint64_t s; uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ULL); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); } i64 rr(i64 lo, i64 hi) { uint64_t r = next(); return lo + (i64)(r % (uint64_t)(hi - lo + 1)); } };
typedef std::array<i64, 3> P3;
int main(int argc, char** argv) { if (argc < 2) return 2; std::string dir = argv[1]; mkdir(dir.c_str(), 0755); Rng g{0x4D5030384233ULL};
    for (int i = 0; i < 60; i++) { std::vector<P3> pts; std::set<P3> seen; std::string fam;
        auto add = [&](i64 x, i64 y, i64 z) { P3 p{x, y, z}; if (seen.insert(p).second) pts.push_back(p); };
        if (i < 25) { fam = "random"; int n = (int)g.rr(8, 40); i64 R = g.rr(6, 40); while ((int)pts.size() < n) { i64 x = g.rr(0, R); i64 y = g.rr(0, R); i64 z = g.rr(0, R); add(x, y, z); } }
        else if (i < 45) { fam = "lattice"; int w = (int)g.rr(2, 4), h = (int)g.rr(2, 4), d = (int)g.rr(2, 4); for (int a = 0; a < w; a++) for (int b = 0; b < h; b++) for (int c = 0; c < d; c++) { i64 keep = g.rr(0, 99); if (keep < 85) add(a * 3, b * 3, c * 3); } add(1, 1, 7); add(7, 2, 1); }
        else if (i < 55) { fam = "flat"; int n = (int)g.rr(10, 30); while ((int)pts.size() < n) { i64 x = g.rr(0, 20); i64 y = g.rr(0, 20); i64 z = g.rr(0, 1); add(x, y, z); } }
        else { fam = "cluster"; int cl = (int)g.rr(2, 4); for (int k = 0; k < cl; k++) { i64 cx = g.rr(0, 60); i64 cy = g.rr(0, 60); i64 cz = g.rr(0, 60); int m = (int)g.rr(4, 9); for (int a = 0; a < m; a++) { i64 dx = g.rr(-5, 5); i64 dy = g.rr(-5, 5); i64 dz = g.rr(-5, 5); add(cx + dx, cy + dy, cz + dz); } } }
        char nm[96]; snprintf(nm, sizeof nm, "%03d_d3_%s", i, fam.c_str()); FILE* f = fopen((dir + "/" + nm + ".h3d").c_str(), "w"); fprintf(f, "H3D1\nname %s\nfamily %s\npoints %zu\n", nm, fam.c_str(), pts.size()); for (auto& p : pts) fprintf(f, "%lld %lld %lld\n", p[0], p[1], p[2]); fclose(f); }
    return 0; }
