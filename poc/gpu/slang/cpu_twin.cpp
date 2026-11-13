// CPU twin check: runs the *same* Slang df64 kernel (slang/df64.slang) two ways on identical inputs, compiled to C++ with
// `slangc -target cpp` and run on the CPU, and as SPIR-V on the Vulkan device, and compares every output bit for bit.
// Built by slang/build_cpu_twin.sh with -DTWIN_CPP="<generated .cpp>" -DTWIN_SPV="<.spv>" (variant fma or Dekker no-fma).
// Ops: 0 add, 1 mul, 2 div, 3 sqrt. add and mul use only IEEE-exact operations (so bit identity is the bar); div and sqrt rely on the
// device's divide and inversesqrt, which Vulkan allows to be approximate, so bit identity across devices is not guaranteed even GPU to GPU.
#include TWIN_CPP
#include "vkctx.h"
#include <cmath>
#include <random>

int main() {
    gpu::Ctx ctx;
    if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    printf("device=%s spv=%s\n", ctx.props.deviceName, TWIN_SPV);
    const uint32_t n = 1u << 16;
    std::mt19937_64 rng(7);
    std::vector<float> A(2 * n), B(2 * n);
    auto mk = [&](float* o, bool pos) {
        double d = (1.0 + std::uniform_real_distribution<double>(0, 1)(rng)) * std::ldexp(1.0, (int)(rng() % 21) - 10);
        if (!pos && (rng() & 1)) d = -d;
        float hi = (float)d; o[0] = hi; o[1] = (float)(d - (double)hi);
    };
    for (uint32_t i = 0; i < n; i++) { mk(&A[2 * i], true); mk(&B[2 * i], false); }
    auto k = ctx.makeKernel(TWIN_SPV, 3, 8);
    auto ga = ctx.alloc(n * 8), gb = ctx.alloc(n * 8), gr = ctx.alloc(n * 8);
    memcpy(ga.ptr, A.data(), n * 8); memcpy(gb.ptr, B.data(), n * 8);
    const char* names[4] = {"dfAdd", "dfMul", "dfDiv", "dfSqrt"};
    int rc = 0;
    for (uint32_t op = 0; op < 4; op++) {
        // CPU: the Slang-generated C++, launched over the same range of thread groups.
        std::vector<float> R(2 * n);
        P_0 pc{n, op};
        GlobalParams_0 gp;
        gp.pc_0 = &pc;
        gp.a_0 = {(Vector<float, 2>*)A.data(), n};
        gp.b_0 = {(Vector<float, 2>*)B.data(), n};
        gp.r_0 = {(Vector<float, 2>*)R.data(), n};
        ComputeVaryingInput vi{{0, 0, 0}, {(n + 63) / 64, 1, 1}};
        df64Main(&vi, nullptr, &gp);
        // GPU: the same source as SPIR-V.
        uint32_t cpc[2] = {n, op};
        ctx.begin(); ctx.upload(ga); ctx.upload(gb); ctx.dispatch(k, {&ga, &gb, &gr}, cpc, (n + 63) / 64); ctx.download(gr); ctx.submitWait();
        size_t diff = 0; double worst = 0;
        for (uint32_t i = 0; i < 2 * n; i++) {
            float g = ((float*)gr.ptr)[i];
            if (memcmp(&g, &R[i], 4) != 0) { diff++; worst = std::max(worst, std::fabs((double)g - R[i])); }
        }
        printf("  %-7s %s  (%zu/%u words differ, largest |cpu-gpu| %.3g)\n", names[op], diff ? "DIFFERS" : "bit-identical", diff, 2 * n, worst);
        if (diff && op < 2) rc = 3;   // add and mul are the bar
    }
    ctx.release(ga); ctx.release(gb); ctx.release(gr); ctx.release(k); ctx.destroy();
    return rc;
}
