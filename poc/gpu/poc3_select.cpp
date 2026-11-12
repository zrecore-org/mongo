// POC 3: GPU radix select (percentile / median) vs CPU std::nth_element and std::sort.
//   ./run.sh poc3_select          (needs the GPU: run it through tools/with_gpu_lock.sh)
#include "vkctx.h"

#include <algorithm>
#include <cmath>
#include <random>

static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
static uint64_t bits(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }

struct Sel {
    gpu::Ctx ctx;
    gpu::Kernel kh, kp;
    gpu::Buf in, hist, st;
    uint32_t R = 0;
    bool init(size_t maxN, uint32_t ranks) {
        if (!ctx.init(gpu::defaultLoaderPaths())) return false;
        R = ranks;
        kh = ctx.makeKernel("build/shaders/rsel_hist.spv", 3, 12, {ranks});
        kp = ctx.makeKernel("build/shaders/rsel_pick.spv", 3, 12, {ranks});
        in = ctx.alloc(maxN * 8); hist = ctx.alloc(ranks * 256 * 4); st = ctx.alloc(ranks * 12);
        return true;
    }
    // Returns the values at the given 0-based ranks; t = {upload, gpu, readback} in ms.
    std::vector<double> select(const double* v, size_t n, const std::vector<uint32_t>& ranks, double t[3]) {
        double t0 = gpu::nowMs();
        memcpy(in.ptr, v, n * 8);
        memset(hist.ptr, 0, R * 256 * 4);
        uint32_t* s = (uint32_t*)st.ptr;
        for (uint32_t r = 0; r < R; r++) { s[3 * r] = 0; s[3 * r + 1] = 0; s[3 * r + 2] = ranks[r]; }
        double t1 = gpu::nowMs();
        uint32_t wgs = std::clamp<size_t>((n + 4095) / 4096, 1, 1024);
        ctx.begin();
        for (uint32_t p = 0; p < 8; p++) {
            uint32_t pc[3] = {(uint32_t)n, wgs * 256, p};
            ctx.dispatch(kh, {&in, &hist, &st}, pc, wgs);
            ctx.dispatch(kp, {&in, &hist, &st}, pc, 1);
        }
        ctx.submitWait();
        double t2 = gpu::nowMs();
        std::vector<double> out(R);
        for (uint32_t r = 0; r < R; r++) {
            uint64_t key = ((uint64_t)s[3 * r] << 32) | s[3 * r + 1];
            uint64_t b = (key >> 63) ? (key & ~(1ull << 63)) : ~key;  // undo the order-preserving map
            memcpy(&out[r], &b, 8);
        }
        t[0] = t1 - t0; t[1] = t2 - t1; t[2] = gpu::nowMs() - t2;
        return out;
    }
    void shutdown() { ctx.release(in); ctx.release(hist); ctx.release(st); ctx.release(kh); ctx.release(kp); ctx.destroy(); }
};

int main() {
    const uint32_t R = 3;
    const size_t maxN = 16u << 20;
    Sel g;
    if (!g.init(maxN, R)) { printf("no GPU: %s\n", g.ctx.error.c_str()); return 2; }
    printf("device=%s R=%u pipeline create: hist %.1f ms, pick %.1f ms\n", g.ctx.props.deviceName, R, g.kh.createMs, g.kp.createMs);
    std::mt19937_64 rng(5);
    std::normal_distribution<double> nd(0, 1);
    std::vector<double> data(maxN);
    for (auto& x : data) x = nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng));

    {   // Diagnostic: one histogram pass alone; the counts must add up to n (never run before pick is trusted).
        size_t n = 1000; memcpy(g.in.ptr, data.data(), n * 8); memset(g.hist.ptr, 0, R * 256 * 4);
        uint32_t* s = (uint32_t*)g.st.ptr; for (uint32_t r = 0; r < R; r++) { s[3*r] = 0; s[3*r+1] = 0; s[3*r+2] = 0; }
        uint32_t pc[3] = {(uint32_t)n, 256, 0};
        g.ctx.begin(); g.ctx.dispatch(g.kh, {&g.in, &g.hist, &g.st}, pc, 1); g.ctx.submitWait();
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
