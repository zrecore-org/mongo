// GPU radix select context shared by poc3_select and poc3b_percentile.
#pragma once
#include "vkctx.h"
#include <algorithm>
#include <cmath>
#include <random>

struct Sel {
    gpu::Ctx ctx;
    gpu::Kernel kh, kp;
    gpu::Buf in, hist, st;
    uint32_t R = 0;
    uint32_t maxWgs = 1024, passes = 8;   // knobs for the sweep (poc3_select sweep)
    bool init(size_t maxN, uint32_t ranks) {
        if (!ctx.init(gpu::defaultLoaderPaths())) return false;
        R = ranks;
        kh = ctx.makeKernel("build/shaders/rsel_hist.spv", 3, 12, {ranks});
        kp = ctx.makeKernel("build/shaders/rsel_pick.spv", 3, 12, {ranks});
        in = ctx.alloc(maxN * 8); hist = ctx.alloc(ranks * 256 * 4); st = ctx.alloc(ranks * 12);
        return true;
    }
    // Returns the values at the given 0-based ranks; t = {upload, gpu, readback} in ms.
    std::vector<double> select(const double* v, size_t n, const std::vector<uint32_t>& ranks, double t[3], bool upload = true) {
        double t0 = gpu::nowMs();
        if (upload) memcpy(in.ptr, v, n * 8);
        memset(hist.ptr, 0, R * 256 * 4);
        uint32_t* s = (uint32_t*)st.ptr;
        for (uint32_t r = 0; r < R; r++) { s[3 * r] = 0; s[3 * r + 1] = 0; s[3 * r + 2] = ranks[r]; }
        double t1 = gpu::nowMs();
        uint32_t wgs = std::clamp<size_t>((n + 4095) / 4096, 1, maxWgs);
        ctx.begin();
        if (upload) ctx.upload(in, n * 8);
        ctx.upload(hist, R * 256 * 4);
        ctx.upload(st, R * 12);
        for (uint32_t p = 0; p < passes; p++) {
            uint32_t pc[3] = {(uint32_t)n, wgs * 256, p};
            ctx.dispatch(kh, {&in, &hist, &st}, pc, wgs);
            ctx.dispatch(kp, {&in, &hist, &st}, pc, 1);
        }
        ctx.download(st, R * 12);
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

static constexpr uint32_t kRankChunk = 16;  // must equal kMaxR in shaders/rsel_hist.comp
// Selects any number of ranks by running chunks of kRankChunk (the last one padded); the data is uploaded once.
// g must have been init()ed with R == kRankChunk.
inline std::vector<double> selectMany(Sel& g, const double* v, size_t n, const std::vector<uint32_t>& ranks, double t[3]) {
    std::vector<double> out; t[0] = t[1] = t[2] = 0;
    for (size_t i = 0; i < ranks.size(); i += kRankChunk) {
        std::vector<uint32_t> c(ranks.begin() + i, ranks.begin() + std::min(ranks.size(), i + kRankChunk));
        size_t real = c.size(); c.resize(kRankChunk, c.back());
        double tt[3]; auto got = g.select(v, n, c, tt, i == 0);
        out.insert(out.end(), got.begin(), got.begin() + real);
        for (int k = 0; k < 3; k++) t[k] += tt[k];
    }
    return out;
}
