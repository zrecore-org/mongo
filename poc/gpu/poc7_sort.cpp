// POC 7: GPU LSD radix sort of (u64 key prefix, u32 original index) pairs vs CPU std::sort of the same pairs.
// This is the sorter-run / index-build case (plan item #3): the GPU sorts key prefixes only; equal prefixes
// would still need a CPU tie-fixup on the full keys (not measured here). Run via tools/with_gpu_lock.sh.
#include "vkctx.h"

#include <algorithm>
#include <random>

static volatile uint64_t sink;
static double med(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

struct GpuSort {
    gpu::Ctx ctx;
    gpu::Kernel kc, ks, kt;
    gpu::Buf keyA, keyB, idxA, idxB, bh;
    uint32_t kernelMask = 7;   // profile mode: bit0 count, bit1 scan, bit2 scatter (results are wrong unless 7)
    bool init(size_t maxN) {
        if (!ctx.init(gpu::defaultLoaderPaths())) return false;
        kc = ctx.makeKernel("build/shaders/sort_count.spv", 2, 12);
        ks = ctx.makeKernel("build/shaders/sort_scan.spv", 1, 12);
        kt = ctx.makeKernel("build/shaders/sort_scatter.spv", 5, 12);
        keyA = ctx.alloc(maxN * 8); keyB = ctx.alloc(maxN * 8); idxA = ctx.alloc(maxN * 4); idxB = ctx.alloc(maxN * 4);
        bh = ctx.alloc(16 * ((maxN + 4095) / 4096) * 4);
        return true;
    }
    // Sorts the low `bits` bits of the keys (multiple of 4). Returns the permutation; t = {upload, gpu, read}.
    std::vector<uint32_t> sort(const uint64_t* keys, size_t n, uint32_t bits, double t[3]) {
        double t0 = gpu::nowMs();
        memcpy(keyA.ptr, keys, n * 8);
        double t1 = gpu::nowMs();
        uint32_t B = (n + 4095) / 4096, passes = bits / 4;
        gpu::Buf *ki = &keyA, *ii = &idxA, *ko = &keyB, *io = &idxB;
        ctx.begin();
        ctx.upload(keyA, n * 8);
        for (uint32_t p = 0; p < passes; p++) {
            uint32_t pc[3] = {(uint32_t)n, B, p};
            if (kernelMask & 1) ctx.dispatch(kc, {ki, &bh}, pc, B);
            if (kernelMask & 2) ctx.dispatch(ks, {&bh}, pc, 1);
            if (kernelMask & 4) ctx.dispatch(kt, {ki, ii, ko, io, &bh}, pc, B);
            std::swap(ki, ko); std::swap(ii, io);
        }
        ctx.download(*ii, n * 4);
        ctx.submitWait();
        double t2 = gpu::nowMs();
        std::vector<uint32_t> out((uint32_t*)ii->ptr, (uint32_t*)ii->ptr + n);
        t[0] = t1 - t0; t[1] = t2 - t1; t[2] = gpu::nowMs() - t2;
        return out;
    }
    void shutdown() { for (auto* b : {&keyA, &keyB, &idxA, &idxB, &bh}) ctx.release(*b); ctx.release(kc); ctx.release(ks); ctx.release(kt); ctx.destroy(); }
};

// T4 investigation: which of the three kernels takes the time (about 50 ms per pass at 16M keys on the T4).
static int profile(GpuSort& g, const std::vector<uint64_t>& data) {
    printf("profile: median ms for 16 passes (64-bit keys): GPU submit incl. PCIe copies, and the host-side parts separately\n%10s | %9s %9s %9s %9s | host upload, readback\n", "n", "count", "scan", "scatter", "all");
    for (size_t n : {1u << 20, 1u << 22, 1u << 24}) {
        printf("%10zu |", n);
        std::string hostPart;
        for (uint32_t mask : {1u, 2u, 4u, 7u}) {
            g.kernelMask = mask;
            std::vector<double> t, tu, tr;
            for (int r = -1; r < 4; r++) { double tt[3]; g.sort(data.data(), n, 64, tt); if (r >= 0) { t.push_back(tt[1]); tu.push_back(tt[0]); tr.push_back(tt[2]); } }
            printf(" %9.2f", med(t));
            if (mask == 7) hostPart = std::to_string(med(tu)).substr(0, 5) + ", " + std::to_string(med(tr)).substr(0, 5);
        }
        printf(" | %s\n", hostPart.c_str());
    }
    g.kernelMask = 7;
    return 0;
}

int main(int argc, char** argv) {
    const bool quick = getenv("GPU_POC_CORRECTNESS_ONLY") != nullptr;  // CI on a CPU Vulkan driver: no timing sweep
    const size_t maxN = quick ? 2u << 20 : 16u << 20;
    GpuSort g;
    if (!g.init(maxN)) { printf("no GPU: %s\n", g.ctx.error.c_str()); return 2; }
    printf("device=%s memory=%s pipeline create: count %.1f, scan %.1f, scatter %.1f ms\n", g.ctx.props.deviceName, g.ctx.memMode(), g.kc.createMs, g.ks.createMs, g.kt.createMs);
    std::mt19937_64 rng(11);
    std::vector<uint64_t> data(maxN);
    for (auto& x : data) x = rng();
    // Correctness: the permutation must equal a stable sort by key (so equal keys keep index order).
    int bad = 0, cases = 0;
    for (size_t n : {1ul, 2ul, 17ul, 4095ul, 4096ul, 4097ul, 100000ul, 1000003ul}) {
        for (int mode = 0; mode < 4; mode++) {
            std::vector<uint64_t> k(data.begin(), data.begin() + n);
            if (mode == 1) for (auto& x : k) x %= 1000;                    // heavy duplicates
            if (mode == 2) std::sort(k.begin(), k.end());                  // already sorted
            if (mode == 3) for (auto& x : k) x = ~0ull - (x % 50);         // high bits all set
            std::vector<uint32_t> ref(n); for (size_t i = 0; i < n; i++) ref[i] = (uint32_t)i;
            std::stable_sort(ref.begin(), ref.end(), [&](uint32_t a, uint32_t b) { return k[a] < k[b]; });
            double t[3]; auto got = g.sort(k.data(), n, 64, t);
            cases++;
            if (got != ref) { bad++; printf("MISMATCH n=%zu mode=%d\n", n, mode); }
        }
    }
    printf("correctness: %d/%d cases give the same permutation as std::stable_sort\n", cases - bad, cases);
    if (argc > 1 && std::string(argv[1]) == "profile") { profile(g, data); g.shutdown(); return bad ? 1 : 0; }
    if (quick) { g.shutdown(); return bad ? 1 : 0; }
    printf("%10s | %9s | %-12s | speedup (64-bit keys) | %-18s | speedup (32-bit)\n", "n", "std::sort", "gpu 64-bit", "gpu 32-bit");
    std::string csv = "n,cpu_sort_ms,gpu64_ms,gpu64_gpu_ms,gpu32_ms,reorder_ms\n";
    for (size_t n = 65536; n <= maxN; n *= 4) {
        int reps = n <= (1u << 20) ? 7 : 3;
        std::vector<double> tc, t64, tg64, t32, tp, tu64, tr64;
        struct Rec { uint64_t w[4]; };
        std::vector<Rec> recs(n), outr(n); for (size_t i = 0; i < n; i++) recs[i].w[0] = i;
        std::vector<std::pair<uint64_t, uint32_t>> base(n);
        for (size_t i = 0; i < n; i++) base[i] = {data[i], (uint32_t)i};
        for (int r = -1; r < reps; r++) {
            auto c = base;
            double a = gpu::nowMs();
            std::sort(c.begin(), c.end());
            double b = gpu::nowMs();
            double t[3]; auto perm = g.sort(data.data(), n, 64, t);
            double pa = gpu::nowMs(); for (size_t i = 0; i < n; i++) outr[i] = recs[perm[i]]; double pb = gpu::nowMs(); sink = sink + outr[n / 2].w[0] + outr[n - 1].w[0];
            double u[3]; g.sort(data.data(), n, 32, u);
            if (r < 0) continue;
            tc.push_back(b - a); t64.push_back(t[0] + t[1] + t[2]); tg64.push_back(t[1]); tu64.push_back(t[0]); tr64.push_back(t[2]); tp.push_back(pb - pa); t32.push_back(u[0] + u[1] + u[2]);
        }
        double c = med(tc), g64 = med(t64), g32 = med(t32);
        printf("%10zu | %9.2f | %9.2f | %6.1fx | %9.2f | %6.1fx | reorder 32B recs: %.1f ms | 64-bit split: host upload %.1f, GPU %.1f, host readback %.1f\n", n, c, g64, c / g64, g32, c / g32, med(tp), med(tu64), med(tg64), med(tr64));
        char line[200]; snprintf(line, sizeof line, "%zu,%.3f,%.3f,%.3f,%.3f,%.3f\n", n, c, g64, med(tg64), g32, med(tp)); csv += line;
    }
    FILE* f = fopen("out/poc7_bench.csv", "w"); fputs(csv.c_str(), f); fclose(f);
    g.shutdown();
    return bad ? 1 : 0;
}
