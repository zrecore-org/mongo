// POC 10: in-place reads on a unified-memory GPU. Today's batch path copies scattered ~1000-value blocks into a mapped
// Vulkan buffer (the "gather", POC 5). With VK_EXT_external_memory_host the server could instead decode blocks straight
// into a page-aligned arena and hand that memory to the GPU as a buffer: no gather and no upload. Compares, for the
// POC 2 `$sum` kernel (1 group, GPU-side split):
//   gather : copy scattered blocks into a mapped Vulkan buffer, then run
//   import : import the arena (per batch: import + bind + release), then run
//   reuse  : arena imported once and reused across batches (a pool), then run
// The decode itself is not measured; it is needed in every variant. Skips itself where the import is unavailable
// or the device is discrete (importing host memory there means reading it over PCIe, not what a design would do).
#include "vkctx.h"

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <random>

struct DD { double s = 0, c = 0; void add(double x) { double t = s + x, bp = t - s; c += (s - (t - bp)) + (x - bp); s = t; } double get() const { return s + c; } };
static double med(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

int main() {
    gpu::Ctx ctx;
    if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    printf("device=%s memory=%s hostImport=%d\n", ctx.props.deviceName, ctx.memMode(), (int)ctx.hostImport);
    if (!ctx.hostImport || !ctx.unifiedMemory) { printf("skipped: needs VK_EXT_external_memory_host on a unified-memory device\n"); ctx.destroy(); return 0; }
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    const size_t perBlock = 1000, maxBlocks = 16384, maxN = perBlock * maxBlocks;
    const uint32_t wg = 256, kMaxWgs = 2048;
    auto k = ctx.makeKernel("build/shaders/sum_gpusplit.spv", 4, 8, {wg, 1u});
    auto in = ctx.alloc(maxN * 8), gid = ctx.alloc(16), part = ctx.alloc((size_t)kMaxWgs * 8), flag = ctx.alloc(kMaxWgs * 4);

    std::mt19937_64 rng(10);
    std::normal_distribution<double> nd(0, 1);
    std::vector<std::vector<double>> blocks(maxBlocks, std::vector<double>(perBlock));  // separately allocated blocks
    for (auto& b : blocks) for (auto& x : b) x = nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng));
    std::vector<size_t> order(maxBlocks); for (size_t i = 0; i < maxBlocks; i++) order[i] = i;
    std::shuffle(order.begin(), order.end(), rng);
    // The arena: the same values already laid out contiguously and page-aligned (what decode-into-arena would produce).
    size_t arenaBytes = (maxN * 8 + page - 1) / page * page;
    void* arena = nullptr;
    if (posix_memalign(&arena, page, arenaBytes)) return 3;
    memset(arena, 0, arenaBytes);

    auto runKernel = [&](gpu::Buf& src, size_t n) {
        uint32_t wgs = std::clamp<uint32_t>((n + wg * 16 - 1) / (wg * 16), 1, kMaxWgs);
        uint32_t pc[2] = {(uint32_t)n, wgs * wg};
        ctx.begin();
        ctx.dispatch(k, {&src, &gid, &part, &flag}, pc, wgs);
        ctx.submitWait();
        DD acc;
        for (uint32_t w = 0; w < wgs; w++) { const float* p = (const float*)part.ptr + 2 * (size_t)w; acc.add(p[0]); acc.add(p[1]); }
        return acc.get();
    };

    printf("%8s %10s | %9s %9s %9s | %s\n", "blocks", "n", "gather", "import", "reuse", "results identical");
    std::string csv = "blocks,n,gather_ms,import_ms,reuse_ms\n";
    int mismatches = 0;
    for (size_t nb : {256ul, 1024ul, 4096ul, 16384ul}) {
        size_t n = nb * perBlock, bytes = (n * 8 + page - 1) / page * page;
        double* a = (double*)arena;
        for (size_t i = 0; i < nb; i++) memcpy(a + i * perBlock, blocks[order[i]].data(), perBlock * 8);  // arena content = same values in the same order
        gpu::Buf pool;
        try { pool = ctx.importHost(arena, bytes); } catch (const std::exception& e) { printf("import failed: %s\n", e.what()); return 4; }
        std::vector<double> tg, ti, tr; double rg = 0, ri = 0, rr = 0;
        for (int r = -2; r < 7; r++) {
            double t0 = gpu::nowMs();
            double* dst = (double*)in.ptr;
            for (size_t i = 0; i < nb; i++) memcpy(dst + i * perBlock, blocks[order[i]].data(), perBlock * 8);
            rg = runKernel(in, n);
            double t1 = gpu::nowMs();
            gpu::Buf imp = ctx.importHost(arena, bytes);
            ri = runKernel(imp, n);
            ctx.release(imp);
            double t2 = gpu::nowMs();
            rr = runKernel(pool, n);
            double t3 = gpu::nowMs();
            if (r >= 0) { tg.push_back(t1 - t0); ti.push_back(t2 - t1); tr.push_back(t3 - t2); }
        }
        bool same = memcmp(&rg, &ri, 8) == 0 && memcmp(&rg, &rr, 8) == 0;
        mismatches += !same;
        printf("%8zu %10zu | %9.3f %9.3f %9.3f | %s\n", nb, n, med(tg), med(ti), med(tr), same ? "yes" : "NO");
        char line[160]; snprintf(line, sizeof line, "%zu,%zu,%.4f,%.4f,%.4f\n", nb, n, med(tg), med(ti), med(tr)); csv += line;
        ctx.release(pool);
    }
    FILE* f = fopen("out/poc10_bench.csv", "w"); fputs(csv.c_str(), f); fclose(f);
    free(arena);
    printf(mismatches ? "FAIL: results differ\n" : "PASS: identical results\n");
    return mismatches ? 1 : 0;
}
