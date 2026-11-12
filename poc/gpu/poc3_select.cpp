// POC 3: GPU radix select (percentile / median) vs CPU std::nth_element and std::sort.
//   ./run.sh poc3_select          (needs the GPU: run it through tools/with_gpu_lock.sh)
#include "vkctx.h"

#include <algorithm>
#include <cmath>
#include <random>

static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
static uint64_t bits(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }

#include "rsel.h"

// T4 investigation: GPU time grows ~13x from 1M to 4M elements (4x the data) on the NVIDIA T4 but not on the M1.
// Sweeps the workgroup cap and the number of passes at fixed n to see which one the time follows.
static int sweep(Sel& g, const std::vector<double>& data) {
    printf("sweep: median GPU ms (incl. PCIe copies) for 3 ranks\n%10s %8s %8s | %s\n", "n", "wg cap", "passes", "gpu ms (upload/gpu/read)");
    for (size_t n : {1u << 20, 1u << 22, 1u << 24})
        for (uint32_t cap : {256u, 1024u, 4096u, 16384u})
            for (uint32_t passes : {1u, 8u}) {
                g.maxWgs = cap; g.passes = passes;
                std::vector<uint32_t> ranks = {(uint32_t)(n / 2), (uint32_t)(n * 9 / 10), (uint32_t)(n * 99 / 100)};
                std::vector<double> tg, tu, tk;
                for (int r = -2; r < 5; r++) { double t[3]; g.select(data.data(), n, ranks, t); if (r >= 0) { tg.push_back(t[0] + t[1] + t[2]); tu.push_back(t[0]); tk.push_back(t[1]); } }
                printf("%10zu %8u %8u | %8.3f (%.2f/%.2f)\n", n, cap, passes, median(tg), median(tu), median(tk));
            }
    return 0;
}

int main(int argc, char** argv) {
    const uint32_t R = 3;
    const bool quick = getenv("GPU_POC_CORRECTNESS_ONLY") != nullptr;  // CI on a CPU Vulkan driver: no timing sweep
    const size_t maxN = quick ? 1u << 20 : 16u << 20;
    Sel g;
    if (!g.init(maxN, R)) { printf("no GPU: %s\n", g.ctx.error.c_str()); return 2; }
    printf("device=%s memory=%s R=%u pipeline create: hist %.1f ms, pick %.1f ms\n", g.ctx.props.deviceName, g.ctx.memMode(), R, g.kh.createMs, g.kp.createMs);
    std::mt19937_64 rng(5);
    std::normal_distribution<double> nd(0, 1);
    std::vector<double> data(maxN);
    for (auto& x : data) x = nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng));

    {   // Diagnostic: one histogram pass alone; the counts must add up to n (never run before pick is trusted).
        size_t n = 1000; memcpy(g.in.ptr, data.data(), n * 8); memset(g.hist.ptr, 0, R * 256 * 4);
        uint32_t* s = (uint32_t*)g.st.ptr; for (uint32_t r = 0; r < R; r++) { s[3*r] = 0; s[3*r+1] = 0; s[3*r+2] = 0; }
        uint32_t pc[3] = {(uint32_t)n, 256, 0};
        g.ctx.begin(); g.ctx.upload(g.in, n * 8); g.ctx.upload(g.hist); g.ctx.upload(g.st);
        g.ctx.dispatch(g.kh, {&g.in, &g.hist, &g.st}, pc, 1); g.ctx.download(g.hist); g.ctx.submitWait();
        uint32_t* h = (uint32_t*)g.hist.ptr; uint64_t sum[3] = {0, 0, 0};
        for (uint32_t r = 0; r < R; r++) for (int d = 0; d < 256; d++) sum[r] += h[r * 256 + d];
        printf("diag: hist pass-0 sums (expect %zu each): %llu %llu %llu\n", n, (unsigned long long)sum[0], (unsigned long long)sum[1], (unsigned long long)sum[2]);
        memset(g.hist.ptr, 0, R * 256 * 4);
    }
    // Correctness: exact match with std::nth_element, including duplicates, negatives, zeros and tiny n.
    int bad = 0, cases = 0;
    for (size_t n : {1ul, 2ul, 3ul, 17ul, 1000ul, 4097ul, 100000ul}) {
        for (int mode = 0; mode < 3; mode++) {
            std::vector<double> v(data.begin(), data.begin() + n);
            if (mode == 1) for (auto& x : v) x = std::round(x * 10) / 10;   // many duplicates
            if (mode == 2) for (size_t i = 0; i < n; i++) v[i] = (i % 3 == 0) ? 0.0 : v[i];  // zeros
            std::vector<uint32_t> ranks = {0, (uint32_t)(n / 2), (uint32_t)(n - 1)};
            double t[3]; auto got = g.select(v.data(), n, ranks, t);
            auto c = v;
            for (uint32_t r = 0; r < R; r++) {
                std::nth_element(c.begin(), c.begin() + ranks[r], c.end());
                cases++;
                if (got[r] != c[ranks[r]]) { bad++; printf("MISMATCH n=%zu mode=%d rank=%u gpu=%.17g cpu=%.17g\n", n, mode, ranks[r], got[r], c[ranks[r]]); }
            }
        }
    }
    printf("correctness: %d/%d ranks equal std::nth_element (numeric ==)\n", cases - bad, cases);

    if (argc > 1 && std::string(argv[1]) == "sweep") { sweep(g, data); g.shutdown(); return bad ? 1 : 0; }
    if (quick) { g.shutdown(); return bad ? 1 : 0; }
    printf("%10s | %10s %10s | %9s (upload/gpu/read) | speedup vs nth_element x3, vs sort\n", "n", "nth_elem x3", "std::sort", "gpu total");
    std::string csv = "n,nth3_ms,sort_ms,gpu_total_ms,upload_ms,gpu_ms,read_ms\n";
    for (size_t n = 1024; n <= maxN; n *= 4) {
        int reps = n <= (1u << 20) ? 11 : 5;
        std::vector<uint32_t> ranks = {(uint32_t)(n / 2), (uint32_t)(n * 9 / 10), (uint32_t)(n * 99 / 100)};
        std::vector<double> tn, ts, tg, tu, tk, tr;
        for (int r = -2; r < reps; r++) {
            auto c = std::vector<double>(data.begin(), data.begin() + n);
            double a = gpu::nowMs();
            for (uint32_t k : ranks) std::nth_element(c.begin(), c.begin() + k, c.end());
            double b = gpu::nowMs();
            double tt[3];
            g.select(data.data(), n, ranks, tt);
            double sortMs = 0;
            if (n <= (4u << 20)) { auto c2 = std::vector<double>(data.begin(), data.begin() + n); double s0 = gpu::nowMs(); std::sort(c2.begin(), c2.end()); sortMs = gpu::nowMs() - s0; }
            if (r < 0) continue;
            tn.push_back(b - a); ts.push_back(sortMs); tg.push_back(tt[0] + tt[1] + tt[2]); tu.push_back(tt[0]); tk.push_back(tt[1]); tr.push_back(tt[2]);
        }
        double nth = median(tn), srt = median(ts), gt = median(tg);
        printf("%10zu | %10.3f %10.3f | %9.3f (%.2f/%.2f/%.2f) | %6.1fx %s\n", n, nth, srt, gt, median(tu), median(tk), median(tr), nth / gt,
               srt > 0 ? (std::to_string(srt / gt).substr(0, 6) + "x").c_str() : "-");
        char line[256]; snprintf(line, sizeof line, "%zu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", n, nth, srt, gt, median(tu), median(tk), median(tr)); csv += line;
    }
    FILE* f = fopen("out/poc3_bench.csv", "w"); fputs(csv.c_str(), f); fclose(f);
    g.shutdown();
    return bad ? 1 : 0;
}
