// CPU twin check for the `$sum` building blocks: slang/selftest.slang compiled to C++ (-target cpp) vs the same source as SPIR-V.
// op 1 twoProd, op 2 double -> df64 split (splitDouble, used by every GPU $sum). Compared bit for bit.
// Built by slang/build_cpu_twin.sh with -DTWIN_CPP / -DTWIN_SPV like cpu_twin.cpp.
#include TWIN_CPP
#include "vkctx.h"
#include <cmath>
#include <random>

int main() {
    gpu::Ctx ctx;
    if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    printf("device=%s spv=%s\n", ctx.props.deviceName, TWIN_SPV);
    const uint32_t n = 1u << 16;
    std::mt19937_64 rng(11);
    std::vector<uint32_t> X(2 * n);   // op 1: two float bit patterns; op 2: one double's raw bits (x = low word, y = high word)
    auto k = ctx.makeKernel(TWIN_SPV, 2, 8);
    auto ga = ctx.alloc(n * 8), gr = ctx.alloc(n * 8);
    int rc = 0;
    for (uint32_t op = 1; op < 3; op++) {
        for (uint32_t i = 0; i < n; i++) {
            if (op == 1) {
                float a = (float)((rng() & 1 ? 1 : -1) * (1.0 + std::uniform_real_distribution<double>(0, 1)(rng)) * std::ldexp(1.0, (int)(rng() % 21) - 10));
                float b = (float)((rng() & 1 ? 1 : -1) * (1.0 + std::uniform_real_distribution<double>(0, 1)(rng)) * std::ldexp(1.0, (int)(rng() % 21) - 10));
                memcpy(&X[2 * i], &a, 4); memcpy(&X[2 * i + 1], &b, 4);
            } else {
                double d = (rng() & 1 ? 1 : -1) * (1.0 + std::uniform_real_distribution<double>(0, 1)(rng)) * std::ldexp(1.0, (int)(rng() % 101) - 50);
                memcpy(&X[2 * i], &d, 8);
            }
        }
        std::vector<float> R(2 * n);
        P_0 pc{n, op};
        GlobalParams_0 gp;
        gp.pc_0 = &pc;
        gp.x_0 = {(Vector<uint32_t, 2>*)X.data(), n};
        gp.r_0 = {(Vector<float, 2>*)R.data(), n};
        ComputeVaryingInput vi{{0, 0, 0}, {(n + 63) / 64, 1, 1}};
        selftestMain(&vi, nullptr, &gp);
        memcpy(ga.ptr, X.data(), n * 8);
        uint32_t cpc[2] = {n, op};
        ctx.begin(); ctx.upload(ga); ctx.dispatch(k, {&ga, &gr}, cpc, (n + 63) / 64); ctx.download(gr); ctx.submitWait();
        size_t diff = 0;
        for (uint32_t i = 0; i < 2 * n; i++) { float g = ((float*)gr.ptr)[i]; if (memcmp(&g, &R[i], 4) != 0) diff++; }
        printf("  %-8s %s  (%zu/%u words differ)\n", op == 1 ? "twoProd" : "split", diff ? "DIFFERS" : "bit-identical", diff, 2 * n);
        if (diff) rc = 3;
    }
    ctx.release(ga); ctx.release(gr); ctx.release(k); ctx.destroy();
    return rc;
}
