// POC 3b: end-to-end percentile, mirroring the real operators in src/mongo/db/pipeline:
//   discrete   (percentile_algo_accurate.cpp):    one std::nth_element per requested p on the same, mutated vector
//   continuous (percentile_algo_continuous.cpp):  std::partial_sort(begin, rank_ceil+1, end) per p, then interpolate
// The GPU path selects floor/ceil ranks for all p at once, and the CPU interpolates. The input is already a
// contiguous std::vector<double> in the real code, so there is no gather step. Run via tools/with_gpu_lock.sh.
#include "rsel.h"

static double med(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

int main(int argc, char** argv) {
    const size_t maxN = 16u << 20;
    std::mt19937_64 rng(7);
    std::normal_distribution<double> nd(0, 1);
    std::vector<double> data(maxN);
    for (auto& x : data) x = nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng));
    // p sets: 1 (median), 3, 7, 20 (the default select-to-sort threshold)
    std::vector<std::vector<double>> pset = {{0.5}, {0.5, 0.9, 0.99}, {0.1, 0.25, 0.5, 0.75, 0.9, 0.95, 0.99}, {}};
    for (int i = 1; i <= 20; i++) pset[3].push_back(i / 21.0);
    int bad = 0;
    printf("%-10s %2s %10s | %10s | %9s (up/gpu/read) | speedup\n", "algo", "P", "n", "cpu ms", "gpu ms");
    std::string csv = "algo,P,n,cpu_ms,gpu_ms\n";
    for (int cont = 0; cont < 2; cont++) {
        for (auto& ps : pset) {
            Sel g;
            if (!g.init(maxN, kRankChunk)) { printf("no GPU: %s\n", g.ctx.error.c_str()); return 2; }
            for (size_t n = 65536; n <= maxN; n *= 4) {
                if (cont && n > (4u << 20) && ps.size() > 3) continue;  // partial_sort is very slow here
                // rank for each p: discrete picks one element, continuous needs floor and ceil.
                std::vector<uint32_t> ranks; std::vector<double> frac;
                for (double p : ps) {
                    double rk = p * (n - 1);
                    if (cont) { ranks.push_back((uint32_t)std::floor(rk)); ranks.push_back((uint32_t)std::ceil(rk)); frac.push_back(rk); }
                    else ranks.push_back((uint32_t)std::ceil(p * n) - 1);
                }
                int reps = n <= (1u << 20) ? 7 : 3;
                std::vector<double> tc, tg, tu, tk, tr;
                for (int r = -1; r < reps; r++) {
                    std::vector<double> c(data.begin(), data.begin() + n);
                    std::vector<double> cpu;
                    double a = gpu::nowMs();
                    for (size_t i = 0; i < ps.size(); i++) {
                        if (!cont) { std::nth_element(c.begin(), c.begin() + ranks[i], c.end()); cpu.push_back(c[ranks[i]]); }
                        else {
                            uint32_t lo = ranks[2 * i], hi = ranks[2 * i + 1];
                            std::partial_sort(c.begin(), c.begin() + hi + 1, c.end());
                            cpu.push_back((hi - frac[i]) * c[lo] + (frac[i] - lo) * c[hi]);
                        }
                    }
                    double b = gpu::nowMs();
                    double t[3]; auto got = selectMany(g, data.data(), n, ranks, t);
                    std::vector<double> gp;
                    for (size_t i = 0; i < ps.size(); i++)
                        gp.push_back(!cont ? got[i] : (ranks[2 * i + 1] - frac[i]) * got[2 * i] + (frac[i] - ranks[2 * i]) * got[2 * i + 1]);
                    double d = gpu::nowMs();
                    for (size_t i = 0; i < ps.size(); i++) if (!(gp[i] == cpu[i])) { bad++; printf("MISMATCH %s P=%zu n=%zu p=%g gpu=%.17g cpu=%.17g\n", cont ? "cont" : "disc", ps.size(), n, ps[i], gp[i], cpu[i]); }
                    if (r < 0) continue;
                    tc.push_back(b - a); tg.push_back(d - b); tu.push_back(t[0]); tk.push_back(t[1]); tr.push_back(t[2]);
                }
                double c = med(tc), gg = med(tg);
                printf("%-10s %2zu %10zu | %10.3f | %9.3f (%.2f/%.2f/%.2f) | %6.1fx\n", cont ? "continuous" : "discrete", ps.size(), n, c, gg, med(tu), med(tk), med(tr), c / gg);
                char line[200]; snprintf(line, sizeof line, "%s,%zu,%zu,%.4f,%.4f\n", cont ? "continuous" : "discrete", ps.size(), n, c, gg); csv += line;
            }
            g.shutdown();
        }
    }
    printf("mismatches vs CPU: %d\n", bad);
    FILE* f = fopen("out/poc3b_bench.csv", "w"); fputs(csv.c_str(), f); fclose(f);
    return bad ? 1 : 0;
}
