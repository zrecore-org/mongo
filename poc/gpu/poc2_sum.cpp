// POC 2: segmented df64 $sum on the GPU vs MongoDB's DoubleDoubleSummation on the CPU.
//   ./run.sh poc2_sum bench     -> end-to-end timing sweep (CSV in out/poc2_bench.csv)
//   ./run.sh poc2_sum accuracy  -> bit-identity, ulp differences, ill-conditioned data, determinism
#include "vkctx.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>

// ---- CPU reference: faithful port of mongo::DoubleDoubleSummation (src/mongo/util/summation.h) ----
struct DD {
    double sum = 0, addend = 0, special = 0;
    void add(double x) {  // addDouble(), summation.h:36-41
        special += x;
        {  // _fast2Sum(x, _addend)  (summation.h:96-100)
            double s = x + addend, z = s - x, t = addend - z;
            x = s; addend = t;
        }
        {  // _2Sum(_sum, x)  (summation.h:111-118)
            double s = sum + x, ap = s - x, bp = s - ap, da = sum - ap, db = x - bp;
            sum = s; x = da + db;
        }
        addend += x;
    }
    double get() const { return std::isnan(sum) ? special : sum; }  // getDouble(), summation.h:56
};

static void cpuDd(const double* v, const uint32_t* g, uint32_t G, size_t n, std::vector<DD>& out) {
    out.assign(G, DD{});
    if (G == 1) { DD d; for (size_t i = 0; i < n; i++) d.add(v[i]); out[0] = d; return; }
    for (size_t i = 0; i < n; i++) out[g[i]].add(v[i]);
}
// Plain double accumulation (4 independent accumulators when G == 1): the CPU speed ceiling, not
// MongoDB's semantics.
static void cpuNaive(const double* v, const uint32_t* g, uint32_t G, size_t n, std::vector<double>& out) {
    out.assign(G, 0.0);
    if (G == 1) {
        double a0 = 0, a1 = 0, a2 = 0, a3 = 0;
        size_t i = 0;
        for (; i + 4 <= n; i += 4) { a0 += v[i]; a1 += v[i + 1]; a2 += v[i + 2]; a3 += v[i + 3]; }
        for (; i < n; i++) a0 += v[i];
        out[0] = (a0 + a1) + (a2 + a3);
        return;
    }
    for (size_t i = 0; i < n; i++) out[g[i]] += v[i];
}

// ---- GPU runner ----
struct Timing { double prep = 0, gpu = 0, combine = 0; bool fallback = false; };

struct Gpu {
    gpu::Ctx ctx;
    gpu::Kernel kSplitCpu, kSplitGpu;
    gpu::Buf in, gid, part, flag;
    size_t cap = 0;
    uint32_t wg = 256, curG = 0, curWg = 0;
    bool soft = getenv("POC2_SOFT") != nullptr;   // POC 12: DoubleDoubleSummation in integer-emulated binary64 (shaders/softsum.comp) instead of df64
    static constexpr uint32_t kMaxWgs = 2048;

    bool init(size_t maxN, uint32_t G, uint32_t wgSize) {
        if (!ctx.init(gpu::defaultLoaderPaths())) return false;
        if (G > 16) { ctx.error = "more than 16 groups"; return false; }   // kMaxGroups in the shaders
        wg = wgSize; curG = G;
        const char* sfx = getenv("POC_SHADER_SUFFIX");  // "_plain" = built without `precise`
        std::string x = sfx ? sfx : "";
        if (soft) {   // one kernel; the "cpusplit" slot is the same kernel and run(false) is routed to it
            kSplitGpu = ctx.makeKernel("build/shaders/softsum.spv", 4, 8, {wg, G});
            kSplitCpu = kSplitGpu;
        } else {
            kSplitCpu = ctx.makeKernel("build/shaders/sum_cpusplit" + x + ".spv", 4, 8, {wg, G});
            kSplitGpu = ctx.makeKernel("build/shaders/sum_gpusplit" + x + ".spv", 4, 8, {wg, G});
        }
        in = ctx.alloc(maxN * 8);
        gid = ctx.alloc(maxN * 4);
        part = ctx.alloc((size_t)kMaxWgs * G * (soft ? 16 : 8));
        flag = ctx.alloc(kMaxWgs * 4);
        cap = maxN;
        return true;
    }
    void shutdown() { ctx.release(in); ctx.release(gid); ctx.release(part); ctx.release(flag);
                      if (!soft) ctx.release(kSplitCpu);
                      ctx.release(kSplitGpu); ctx.destroy(); }

    // gpuSplit: convert double->df64 on the GPU from raw bits (else on the CPU while uploading).
    std::vector<double> run(bool gpuSplit, const double* v, const uint32_t* g, size_t n, Timing& t, uint32_t wgsOverride = 0) {
        if (soft) gpuSplit = true;
        double t0 = gpu::nowMs();
        bool badCpu = false;
        if (gpuSplit) {
            memcpy(in.ptr, v, n * 8);
        } else {
            float* p = (float*)in.ptr;
            uint32_t bad = 0;
            for (size_t i = 0; i < n; i++) {
                float hi = (float)v[i];
                float lo = (float)(v[i] - (double)hi);
                float a = std::fabs(hi);
                bad |= (a != 0.f) & ((a < 1e-30f) | (a > 1e30f));  // also catches Inf (NaN fails both compares: use below)
                bad |= (hi != hi);
                p[2 * i] = hi; p[2 * i + 1] = lo;
            }
            badCpu = bad;
        }
        if (curG > 1) memcpy(gid.ptr, g, n * 4);
        double t1 = gpu::nowMs();
        uint32_t wgs = wgsOverride ? wgsOverride : std::clamp<uint32_t>((n + wg * 16 - 1) / (wg * 16), 1, kMaxWgs);
        uint32_t pc[2] = {(uint32_t)n, wgs * wg};
        ctx.begin();
        ctx.upload(in, n * 8);
        if (curG > 1) ctx.upload(gid, n * 4);
        ctx.dispatch(gpuSplit ? kSplitGpu : kSplitCpu, {&in, &gid, &part, &flag}, pc, wgs);
        ctx.download(part, (size_t)wgs * curG * (soft ? 16 : 8));
        ctx.download(flag, (size_t)wgs * 4);
        ctx.submitWait();
        double t2 = gpu::nowMs();
        // Combine per-workgroup partials with DoubleDoubleSummation semantics on the host.
        std::vector<DD> acc(curG);
        bool bad = badCpu;
        for (uint32_t w = 0; w < wgs; w++) {
            bad |= ((uint32_t*)flag.ptr)[w] != 0;
            for (uint32_t k = 0; k < curG; k++) {
                if (soft) {
                    const double* pd = (const double*)part.ptr + 2 * (size_t)(w * curG + k);
                    acc[k].add(pd[0]);
                    acc[k].add(pd[1]);
                    continue;
                }
                const float* pf = (const float*)part.ptr + 2 * (size_t)(w * curG + k);
                acc[k].add((double)pf[0]);
                acc[k].add((double)pf[1]);
            }
        }
        std::vector<double> out(curG);
        for (uint32_t k = 0; k < curG; k++) { out[k] = acc[k].get(); bad |= !std::isfinite(out[k]); }
        double t3 = gpu::nowMs();
        t.prep = t1 - t0; t.gpu = t2 - t1; t.combine = t3 - t2; t.fallback = bad;
        return out;
    }
};

static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
static int64_t ulpDist(double a, double b) {
    auto ord = [](double x) { int64_t i; memcpy(&i, &x, 8); return i < 0 ? INT64_MIN - i : i; };
    int64_t d = ord(a) - ord(b);
    return d < 0 ? -d : d;
}

static std::vector<double> makeData(size_t n, std::mt19937_64& rng) {  // sensor-like values: mixed magnitude, mixed sign
    std::vector<double> v(n);
    std::normal_distribution<double> nd(0, 1);
    for (auto& x : v) x = nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng));
    return v;
}

static int bench(uint32_t G) {
    const size_t maxN = 32u << 20;
    Gpu gp;
    if (!gp.init(maxN, G, 256)) { printf("no GPU: %s\n", gp.ctx.error.c_str()); return 2; }
    if (gp.soft) printf("POC2_SOFT: DoubleDoubleSummation in integer-emulated binary64; the cpusplit and gpusplit columns below are the SAME soft kernel\n");
    printf("device=%s memory=%s G=%u pipeline create: cpusplit %.1f ms, gpusplit %.1f ms\n", gp.ctx.props.deviceName, gp.ctx.memMode(), G,
           gp.kSplitCpu.createMs, gp.kSplitGpu.createMs);
    std::mt19937_64 rng(7);
    auto data = makeData(maxN, rng);
    std::vector<uint32_t> gids(maxN);
    for (auto& x : gids) x = (uint32_t)(rng() % G);
    if (getenv("POC_SHADER_SUFFIX")) printf("shader suffix=%s MVK_CONFIG_FAST_MATH_ENABLED=%s\n", getenv("POC_SHADER_SUFFIX"), getenv("MVK_CONFIG_FAST_MATH_ENABLED") ? getenv("MVK_CONFIG_FAST_MATH_ENABLED") : "(default)");
    std::string csv = "G,n,cpu_dd_ms,cpu_naive_ms,gpu_cpusplit_total_ms,cs_prep,cs_gpu,cs_combine,gpu_gpusplit_total_ms,gs_prep,gs_gpu,gs_combine\n";
    printf("%-3s %10s | %9s %9s | %9s (prep/gpu/comb) | %9s (prep/gpu/comb) | speedup vs dd: cpusplit gpusplit\n",
           "G", "n", "cpu_dd", "cpu_naive", "cpusplit", "gpusplit");
    for (size_t n = 1024; n <= maxN; n *= 4) {
        int reps = n <= (1u << 20) ? 15 : 7;
        std::vector<double> tdd, tnv, tcs, tgs, pcs, gcs, ccs, pgs, ggs, cgs;
        std::vector<DD> o1; std::vector<double> o2;
        for (int r = -1; r < reps; r++) {   // phase 1: CPU baselines only
            double a = gpu::nowMs(); cpuDd(data.data(), gids.data(), G, n, o1); double b = gpu::nowMs();
            cpuNaive(data.data(), gids.data(), G, n, o2); double c = gpu::nowMs();
            if (r >= 0) { tdd.push_back(b - a); tnv.push_back(c - b); }
        }
        gpu::warmup([&] { Timing t; gp.run(true, data.data(), gids.data(), n, t); });   // phase 2: GPU only, after a warm-up
        for (int r = 0; r < reps; r++) {
            Timing t1, t2;
            if (gp.soft) { gp.run(true, data.data(), gids.data(), n, t2); t1 = t2; }   // soft: both columns are the one integer kernel
            else { gp.run(false, data.data(), gids.data(), n, t1); gp.run(true, data.data(), gids.data(), n, t2); }
            tcs.push_back(t1.prep + t1.gpu + t1.combine); pcs.push_back(t1.prep); gcs.push_back(t1.gpu); ccs.push_back(t1.combine);
            tgs.push_back(t2.prep + t2.gpu + t2.combine); pgs.push_back(t2.prep); ggs.push_back(t2.gpu); cgs.push_back(t2.combine);
        }
        double dd = median(tdd), nv = median(tnv), cs = median(tcs), gs = median(tgs);
        printf("%-3u %10zu | %9.3f %9.3f | %9.3f (%.2f/%.2f/%.2f) | %9.3f (%.2f/%.2f/%.2f) | %6.2fx %6.2fx\n", G, n, dd, nv, cs,
               median(pcs), median(gcs), median(ccs), gs, median(pgs), median(ggs), median(cgs), dd / cs, dd / gs);
        char line[512];
        snprintf(line, sizeof line, "%u,%zu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", G, n, dd, nv, cs, median(pcs),
                 median(gcs), median(ccs), gs, median(pgs), median(ggs), median(cgs));
        csv += line;
    }
    FILE* f = fopen(("out/poc2_bench_G" + std::to_string(G) + (getenv("POC_SHADER_SUFFIX") ? "_plain_fm0" : "") + (gp.soft ? "_soft" : "") + ".csv").c_str(), "w"); fputs(csv.c_str(), f); fclose(f);
    gp.shutdown();
    return 0;
}

static int accuracy() {
    Gpu gp;
    if (!gp.init(1u << 20, 1, 256)) { printf("no GPU: %s\n", gp.ctx.error.c_str()); return 2; }
    std::mt19937_64 rng(99);
    std::vector<uint32_t> nog;
    // 1. Random sensor-like data: how often is the GPU's final double bit-identical to $sum's?
    int trials = 200, sameGpu = 0, sameNaive = 0, sameCpuShuffle = 0;
    int64_t maxUlpGpu = 0, maxUlpNaive = 0, maxUlpShuffle = 0;
    for (int t = 0; t < trials; t++) {
        auto v = makeData(100000, rng);
        std::vector<DD> r; cpuDd(v.data(), nullptr, 1, v.size(), r);
        Timing tm;
        double gsum = gp.run(true, v.data(), nullptr, v.size(), tm)[0];
        std::vector<double> nv; cpuNaive(v.data(), nullptr, 1, v.size(), nv);
        std::shuffle(v.begin(), v.end(), rng);
        std::vector<DD> r2; cpuDd(v.data(), nullptr, 1, v.size(), r2);
        double ref = r[0].get();
        sameGpu += gsum == ref; maxUlpGpu = std::max(maxUlpGpu, ulpDist(gsum, ref));
        sameNaive += nv[0] == ref; maxUlpNaive = std::max(maxUlpNaive, ulpDist(nv[0], ref));
        sameCpuShuffle += r2[0].get() == ref; maxUlpShuffle = std::max(maxUlpShuffle, ulpDist(r2[0].get(), ref));
    }
    printf("random data (%d trials x 100k): identical to CPU $sum: %s %d/%d (max %lld ulp), naive double %d/%d (max %lld ulp), CPU $sum on shuffled input %d/%d (max %lld ulp)\n",
           trials, getenv("POC2_SOFT") ? "gpu-soft-binary64" : "gpu-df64", sameGpu, trials, (long long)maxUlpGpu, sameNaive, trials, (long long)maxUlpNaive, sameCpuShuffle, trials, (long long)maxUlpShuffle);
    // 2. Ill-conditioned: +big, many small, -big. True sum = sum of the small values.
    {
        size_t n = 1000000; std::vector<double> v(n);
        for (size_t i = 1; i + 1 < n; i++) v[i] = 1e-3 * (1 + (double)(rng() % 1000) / 1000);
        v[0] = 1e10; v[n - 1] = -1e10;
        std::vector<DD> r; cpuDd(v.data(), nullptr, 1, n, r);
        std::vector<double> nv; cpuNaive(v.data(), nullptr, 1, n, nv);
        Timing tm; double g = gp.run(true, v.data(), nullptr, n, tm)[0];
        // Exactly rounded sum of the small values (Shewchuk partials, as in Python's math.fsum).
        double exact = 0;
        {
            std::vector<double> p;
            for (size_t k = 1; k + 1 < n; k++) {
                double x = v[k]; size_t j = 0;
                for (double y : p) {
                    if (std::fabs(x) < std::fabs(y)) std::swap(x, y);
                    volatile double hi = x + y; double lo = y - (hi - x);
                    if (lo != 0) p[j++] = lo;
                    x = hi;
                }
                p.resize(j); p.push_back(x);
            }
            for (double y : p) exact += y;  // partials are non-overlapping; small count, ascending magnitude
        }
        printf("ill-conditioned (+1e10, 1M small, -1e10): small-values sum (reference) = %.12g | CPU $sum = %.12g (%lld ulp) | gpu %s = %.12g (%lld ulp) | naive double = %.12g (%lld ulp)\n",
               exact, r[0].get(), (long long)ulpDist(r[0].get(), exact), getenv("POC2_SOFT") ? "soft" : "df64", g, (long long)ulpDist(g, exact), nv[0], (long long)ulpDist(nv[0], exact));
    }
    // 3. Determinism: same data, repeated runs and different grids.
    {
        auto v = makeData(1000000, rng);
        Timing tm; std::map<uint64_t, int> runs, grids;
        for (int i = 0; i < 30; i++) { double s = gp.run(true, v.data(), nullptr, v.size(), tm)[0]; uint64_t b; memcpy(&b, &s, 8); runs[b]++; }
        for (uint32_t w : {16u, 64u, 256u, 1024u, 2048u}) { double s = gp.run(true, v.data(), nullptr, v.size(), tm, w)[0]; uint64_t b; memcpy(&b, &s, 8); grids[b]++; }
        printf("determinism: 30 identical runs -> %zu distinct result(s); 5 different grid sizes -> %zu distinct result(s)\n", runs.size(), grids.size());
    }
    // 4. Fallback detection: out-of-range values must set the flag.
    {
        std::vector<double> v(1000, 1.5);
        Timing tm;
        v[500] = 1e300; gp.run(true, v.data(), nullptr, v.size(), tm); bool f1 = tm.fallback;
        v[500] = std::nan(""); gp.run(true, v.data(), nullptr, v.size(), tm); bool f2 = tm.fallback;
        v[500] = 1e-310; gp.run(true, v.data(), nullptr, v.size(), tm); bool f3 = tm.fallback;
        v[500] = 1.5; gp.run(true, v.data(), nullptr, v.size(), tm); bool f4 = tm.fallback;
        printf("fallback flag: 1e300 -> %d, NaN -> %d, denormal -> %d (all expect 1), clean data -> %d (expect 0)\n", f1, f2, f3, f4);
    }
    gp.shutdown();
    return 0;
}

// Grouped correctness: every group's GPU sum against the CPU double-double sum, relative to that group's sum of |x|.
// (The accuracy test above uses one group; a wrong grouped kernel would otherwise go unnoticed.)
static int groupsCheck() {
    const size_t n = 1u << 20;
    std::mt19937_64 rng(31);
    auto data = makeData(n, rng);
    std::vector<uint32_t> gids(n);
    int bad = 0;
    printf("grouped correctness (n=1M): max over groups of |gpu - cpu| / sum|x|%s\n%3s %-9s %12s %s %s\n", getenv("POC2_SOFT") ? " (POC2_SOFT: integer binary64)" : "", "G", "split", "max rel err", "result", "bit-identical groups");
    for (uint32_t G : {1u, 2u, 4u, 16u}) {
        for (auto& x : gids) x = (uint32_t)(rng() % G);
        std::vector<DD> cpu; cpuDd(data.data(), gids.data(), G, n, cpu);
        std::vector<double> absSum(G, 0.0);
        for (size_t i = 0; i < n; i++) absSum[gids[i]] += std::fabs(data[i]);
        Gpu gp;
        if (!gp.init(n, G, 256)) { printf("no GPU: %s\n", gp.ctx.error.c_str()); return 2; }
        for (int split = 0; split < 2; split++) {
            Timing t;
            auto out = gp.run(split == 1, data.data(), gids.data(), n, t);
            double worst = 0; uint32_t same = 0;
            for (uint32_t k = 0; k < G; k++) { worst = std::max(worst, std::fabs(out[k] - cpu[k].get()) / absSum[k]); same += out[k] == cpu[k].get(); }
            bool ok = worst < 1e-9;
            bad += !ok;
            printf("%3u %-9s %12.2e %s   %u/%u\n", G, split ? "gpu" : "cpu", worst, ok ? "PASS" : "FAIL", same, G);
        }
        gp.shutdown();
    }
    return bad ? 1 : 0;
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "accuracy";
    if (mode == "groups") return groupsCheck();
    if (mode == "bench") return bench(argc > 2 ? atoi(argv[2]) : 1);
    return accuracy();
}
