// POC 9: `$stdDevPop`/`$stdDevSamp` on the GPU vs the real CPU accumulator (src/mongo/db/pipeline/accumulator_std_dev.cpp:
// plain-double Welford with a division per element). GPU method: pass 1 = the POC 2 `$sum` kernel (df64 sum per group),
// host computes the means, pass 2 = sum of (x - mean)^2 in df64 (shaders/std_m2.comp). The data is uploaded once.
//   ./run.sh poc9_stddev            (run through tools/with_gpu_lock.sh when the GPU is shared)
#include "vkctx.h"

#include <algorithm>
#include <cmath>
#include <random>

struct DD {  // 2Sum accumulator (the same idea as DoubleDoubleSummation::addDouble)
    double s = 0, c = 0;
    void add(double x) { double t = s + x, bp = t - s; c += (s - (t - bp)) + (x - bp); s = t; }
    double get() const { return s + c; }
};
static double med(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

struct Gpu {
    gpu::Ctx ctx;
    gpu::Kernel k1, k2;
    gpu::Buf in, gid, part, flag, means;
    uint32_t G = 1, wg = 256;
    static constexpr uint32_t kMaxWgs = 2048;
    bool init(size_t maxN, uint32_t groups) {
        if (!ctx.init(gpu::defaultLoaderPaths())) return false;
        G = groups;
        k1 = ctx.makeKernel("build/shaders/sum_gpusplit.spv", 4, 8, {wg, G});
        k2 = ctx.makeKernel("build/shaders/std_m2.spv", 5, 8, {wg, G});
        in = ctx.alloc(maxN * 8); gid = ctx.alloc(maxN * 4);
        part = ctx.alloc((size_t)kMaxWgs * G * 8); flag = ctx.alloc(kMaxWgs * 4); means = ctx.alloc(G * 8);
        return true;
    }
    void shutdown() { for (auto* b : {&in, &gid, &part, &flag, &means}) ctx.release(*b); ctx.release(k1); ctx.release(k2); ctx.destroy(); }

    // Returns the per-group M2 (sum of squared deviations) and counts; fallback is set when any input is out of range.
    void run(const double* v, const uint32_t* g, size_t n, std::vector<double>& m2, std::vector<uint64_t>& cnt, bool& fallback) {
        memcpy(in.ptr, v, n * 8);
        cnt.assign(G, 0);
        if (G > 1) { memcpy(gid.ptr, g, n * 4); for (size_t i = 0; i < n; i++) cnt[g[i]]++; } else cnt[0] = n;
        uint32_t wgs = std::clamp<uint32_t>((n + wg * 16 - 1) / (wg * 16), 1, kMaxWgs);
        uint32_t pc[2] = {(uint32_t)n, wgs * wg};
        ctx.begin();
        ctx.upload(in, n * 8); if (G > 1) ctx.upload(gid, n * 4);
        ctx.dispatch(k1, {&in, &gid, &part, &flag}, pc, wgs);
        ctx.download(part, (size_t)wgs * G * 8); ctx.download(flag, (size_t)wgs * 4);
        ctx.submitWait();
        bool bad = false;
        std::vector<DD> sum(G);
        for (uint32_t w = 0; w < wgs; w++) {
            bad |= ((uint32_t*)flag.ptr)[w] != 0;
            for (uint32_t k = 0; k < G; k++) { const float* p = (const float*)part.ptr + 2 * (size_t)(w * G + k); sum[k].add(p[0]); sum[k].add(p[1]); }
        }
        float* mp = (float*)means.ptr;
        for (uint32_t k = 0; k < G; k++) {
            double mean = cnt[k] ? sum[k].get() / (double)cnt[k] : 0.0;
            float hi = (float)mean; mp[2 * k] = hi; mp[2 * k + 1] = (float)(mean - (double)hi);
        }
        ctx.begin();
        ctx.upload(means);
        ctx.dispatch(k2, {&in, &gid, &part, &flag, &means}, pc, wgs);
        ctx.download(part, (size_t)wgs * G * 8); ctx.download(flag, (size_t)wgs * 4);
        ctx.submitWait();
        std::vector<DD> acc(G);
        for (uint32_t w = 0; w < wgs; w++) {
            bad |= ((uint32_t*)flag.ptr)[w] != 0;
            for (uint32_t k = 0; k < G; k++) { const float* p = (const float*)part.ptr + 2 * (size_t)(w * G + k); acc[k].add(p[0]); acc[k].add(p[1]); }
        }
        m2.resize(G);
        for (uint32_t k = 0; k < G; k++) m2[k] = acc[k].get();
        fallback = bad;
    }
};

// The real accumulator's update, per group (plain double Welford).
static std::vector<double> cpuWelford(const double* v, const uint32_t* g, size_t n, uint32_t G) {
    std::vector<double> mean(G, 0), m2(G, 0), cnt(G, 0);
    for (size_t i = 0; i < n; i++) {
        uint32_t k = G > 1 ? g[i] : 0;
        cnt[k] += 1;
        double delta = v[i] - mean[k];
        if (delta != 0.0) { mean[k] += delta / cnt[k]; m2[k] += delta * (v[i] - mean[k]); }
    }
    return m2;
}
// Two-pass in plain double (what a naive port would do), and an oracle using double-double products.
static std::vector<double> cpuTwoPass(const double* v, const uint32_t* g, size_t n, uint32_t G, bool oracle) {
    std::vector<DD> s(G); std::vector<double> c(G, 0);
    for (size_t i = 0; i < n; i++) { uint32_t k = G > 1 ? g[i] : 0; s[k].add(v[i]); c[k]++; }
    std::vector<double> mean(G); for (uint32_t k = 0; k < G; k++) mean[k] = c[k] ? s[k].get() / c[k] : 0;
    std::vector<DD> m(G); std::vector<double> plain(G, 0);
    for (size_t i = 0; i < n; i++) {
        uint32_t k = G > 1 ? g[i] : 0;
        double hi = v[i] - mean[k], bb = v[i] - hi, lo = (v[i] - (hi + bb)) + (-mean[k] + bb);  // exact twoDiff
        if (oracle) { double p = hi * hi; m[k].add(p); m[k].add(std::fma(hi, hi, -p)); m[k].add(2 * hi * lo); }
        else plain[k] += hi * hi;
    }
    if (!oracle) return plain;
    std::vector<double> out(G); for (uint32_t k = 0; k < G; k++) out[k] = m[k].get(); return out;
}

int main() {
    const size_t maxN = 16u << 20;
    Gpu g1;
    if (!g1.init(maxN, 1)) { printf("no GPU: %s\n", g1.ctx.error.c_str()); return 2; }
    printf("device=%s memory=%s\n", g1.ctx.props.deviceName, g1.ctx.memMode());
    std::mt19937_64 rng(9);
    std::normal_distribution<double> nd(0, 1);
    // Accuracy: relative error of the standard deviation against the oracle, three data shapes, 1M values, 1 group.
    printf("accuracy (relative error of stdDevPop vs oracle, n=1M):\n%-34s %12s %12s %12s\n", "data", "CPU Welford", "naive 2-pass", "GPU df64");
    struct Shape { const char* name; double mean, sd; bool mixed; };
    for (Shape sh : {Shape{"mean 0, sd 1", 0, 1, false}, Shape{"mean 1e6+0.3, sd 1", 1e6 + 0.3, 1, false}, Shape{"mean 123456789.123, sd 1e-2", 123456789.123, 1e-2, false}, Shape{"mean 1e9+12345.678, sd 1e-2", 1e9 + 12345.678, 1e-2, false}, Shape{"mixed magnitude 1e-3..1e3", 0, 1, true}}) {
        size_t n = 1u << 20; std::vector<double> v(n);
        for (auto& x : v) x = sh.mean + (sh.mixed ? nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng)) : sh.sd * nd(rng));
        auto ref = cpuTwoPass(v.data(), nullptr, n, 1, true)[0];
        auto w = cpuWelford(v.data(), nullptr, n, 1)[0], nv = cpuTwoPass(v.data(), nullptr, n, 1, false)[0];
        std::vector<double> m2; std::vector<uint64_t> c; bool fb;
        g1.run(v.data(), nullptr, n, m2, c, fb);
        auto rel = [&](double m) { return std::fabs(std::sqrt(m / n) - std::sqrt(ref / n)) / std::sqrt(ref / n); };
        printf("%-34s %12.2e %12.2e %12.2e%s\n", sh.name, rel(w), rel(nv), rel(m2[0]), fb ? "  (fallback flag set)" : "");
    }
    g1.shutdown();

    printf("timing (ms, median; GPU includes uploads, both passes and the host mean/combine):\n%3s %10s | %10s | %10s | speedup\n", "G", "n", "cpu Welford", "gpu df64");
    std::string csv = "G,n,cpu_ms,gpu_ms\n";
    for (uint32_t G : {1u, 16u}) {
        Gpu g;
        if (!g.init(maxN, G)) return 2;
        std::vector<double> data(maxN); std::vector<uint32_t> gids(maxN);
        for (size_t i = 0; i < maxN; i++) { data[i] = nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng)); gids[i] = (uint32_t)(rng() % G); }
        for (size_t n = 1024; n <= maxN; n *= 4) {
            int reps = n <= (1u << 20) ? 9 : 5;
            std::vector<double> tc, tg;
            std::vector<double> wref;
            for (int r = -1; r < reps; r++) {   // phase 1: CPU baseline only
                double a = gpu::nowMs(); wref = cpuWelford(data.data(), gids.data(), n, G); double b = gpu::nowMs();
                if (r >= 0) tc.push_back(b - a);
            }
            std::vector<double> m2; std::vector<uint64_t> c; bool fb;
            gpu::warmup([&] { g.run(data.data(), gids.data(), n, m2, c, fb); });   // phase 2: GPU only, after a warm-up
            for (int r = 0; r < reps; r++) { double d0 = gpu::nowMs(); g.run(data.data(), gids.data(), n, m2, c, fb); tg.push_back(gpu::nowMs() - d0); }
            if (wref[0] > 0 && std::fabs(std::sqrt(m2[0]) - std::sqrt(wref[0])) / std::sqrt(wref[0]) > 1e-6) printf("  (G=%u n=%zu: GPU and CPU stdDev differ by %.2e relative)\n", G, n, std::fabs(std::sqrt(m2[0]) - std::sqrt(wref[0])) / std::sqrt(wref[0]));
            printf("%3u %10zu | %10.3f | %10.3f | %6.2fx\n", G, n, med(tc), med(tg), med(tc) / med(tg));
            char line[100]; snprintf(line, sizeof line, "%u,%zu,%.4f,%.4f\n", G, n, med(tc), med(tg)); csv += line;
        }
        g.shutdown();
    }
    FILE* f = fopen("out/poc9_bench.csv", "w"); fputs(csv.c_str(), f); fclose(f);
    return 0;
}
