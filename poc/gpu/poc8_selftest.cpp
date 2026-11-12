// POC 8: startup known-answer self-test for df64 (the gate the plan requires before any GPU `$sum`).
// Probes, each against an exact CPU result: fma, twoProd, the GPU-side double -> df64 split, and df64
// mul/div/sqrt. Run with the variant tag ("precise" = fma twoProd, "nofma" = Dekker twoProd):
//   ./run.sh poc8_selftest [precise|nofma]      exit 0 = all probes pass, 3 = at least one failed, 2 = no GPU
#include "vkctx.h"

#include <cmath>
#include <random>

static void splitD(double d, float& hi, float& lo) { hi = (float)d; lo = (float)(d - (double)hi); }

int main(int argc, char** argv) {
    const std::string tag = argc > 1 ? argv[1] : "precise";
    const std::string selftest = tag == "nofma" ? "build/shaders/selftest_nofma.spv" : "build/shaders/selftest.spv";
    gpu::Ctx ctx;
    if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    printf("device=%s variant=%s\n", ctx.props.deviceName, tag.c_str());
    const uint32_t n = 4096;
    std::mt19937_64 rng(2024);
    auto u01 = [&] { return std::uniform_real_distribution<double>(0, 1)(rng); };
    auto sgn = [&] { return (rng() & 1) ? 1.0 : -1.0; };
    auto mant = [&] { return 1.0 + u01(); };
    auto A = ctx.alloc(n * 8), R = ctx.alloc(n * 8), B = ctx.alloc(n * 8);
    int failed = 0;
    auto report = [&](const char* name, size_t bad, double worst) {
        printf("  %-34s %s (%zu/%u bad, worst %.3g x 2^-44)\n", name, bad ? "FAIL" : "PASS", bad, n, worst);
        failed += bad != 0;
    };

    // Probes 0 and 1: float pairs.
    auto ks = ctx.makeKernel(selftest, 2, 8);
    std::vector<float> fa(n), fb(n);
    for (uint32_t i = 0; i < n; i++) { fa[i] = (float)(sgn() * mant() * std::ldexp(1.0, (int)(rng() % 21) - 10)); fb[i] = (float)(sgn() * mant() * std::ldexp(1.0, (int)(rng() % 21) - 10)); }
    for (uint32_t op = 0; op < 2; op++) {
        for (uint32_t i = 0; i < n; i++) { ((float*)A.ptr)[2 * i] = fa[i]; ((float*)A.ptr)[2 * i + 1] = fb[i]; }
        uint32_t pc[2] = {n, op};
        ctx.begin(); ctx.dispatch(ks, {&A, &R}, pc, (n + 63) / 64); ctx.submitWait();
        size_t bad = 0; double worst = 0;
        for (uint32_t i = 0; i < n; i++) {
            float p = fa[i] * fb[i]; float e = (float)((double)fa[i] * (double)fb[i] - (double)p);  // exact: 48-bit product fits a double
            float gp = ((float*)R.ptr)[2 * i], ge = ((float*)R.ptr)[2 * i + 1];
            double err = std::fabs((double)gp + ge - ((double)fa[i] * fb[i])) / std::fabs((double)fa[i] * fb[i]) / std::ldexp(1.0, -44);
            worst = std::max(worst, err);
            if (gp != p || ge != e) bad++;
        }
        report(op == 0 ? "fma(a, b, -a*b) is the exact error" : "twoProd(a, b) is exact", bad, worst);
    }
    // Probe 2: double -> df64 split, on raw bits.
    {
        std::vector<double> d(n);
        for (auto& x : d) x = sgn() * mant() * std::ldexp(1.0, (int)(rng() % 101) - 50);
        memcpy(A.ptr, d.data(), n * 8);
        uint32_t pc[2] = {n, 2};
        ctx.begin(); ctx.dispatch(ks, {&A, &R}, pc, (n + 63) / 64); ctx.submitWait();
        size_t bad = 0; double worst = 0;
        for (uint32_t i = 0; i < n; i++) {
            double got = (double)((float*)R.ptr)[2 * i] + (double)((float*)R.ptr)[2 * i + 1];
            double err = std::fabs(got - d[i]) / std::fabs(d[i]) / std::ldexp(1.0, -44);
            worst = std::max(worst, err);
            if (err > 4) bad++;
        }
        report("GPU double -> df64 split", bad, worst);
    }
    ctx.release(ks);

    // Probes 3-5: df64 mul/div/sqrt through the POC 1 kernel of the same variant.
    auto k = ctx.makeKernel("build/shaders/df64_" + tag + ".spv", 3, 8);
    std::vector<double> a(n), b(n);
    for (uint32_t i = 0; i < n; i++) { a[i] = sgn() * mant() * std::ldexp(1.0, (int)(rng() % 21) - 10); b[i] = sgn() * mant() * std::ldexp(1.0, (int)(rng() % 21) - 10); }
    auto Bb = B;
    for (uint32_t i = 0; i < n; i++) { float h, l; splitD(a[i], h, l); ((float*)A.ptr)[2 * i] = h; ((float*)A.ptr)[2 * i + 1] = l; splitD(b[i], h, l); ((float*)Bb.ptr)[2 * i] = h; ((float*)Bb.ptr)[2 * i + 1] = l; }
    const char* names[3] = {"df64 mul vs exact", "df64 div vs exact", "df64 sqrt vs exact"};
    for (uint32_t op = 1; op < 4; op++) {
        uint32_t pc[2] = {n, op};
        ctx.begin(); ctx.dispatch(k, {&A, &Bb, &R}, pc, (n + 63) / 64); ctx.submitWait();
        size_t bad = 0; double worst = 0;
        for (uint32_t i = 0; i < n; i++) {
            // Reference on the split inputs (hi+lo as doubles); a double carries 53 bits, enough to see 2^-41.
            double x = (double)((float*)A.ptr)[2 * i] + ((float*)A.ptr)[2 * i + 1], y = (double)((float*)Bb.ptr)[2 * i] + ((float*)Bb.ptr)[2 * i + 1];
            double ref = op == 1 ? x * y : op == 2 ? x / y : std::sqrt(std::fabs(x));
            double got = (double)((float*)R.ptr)[2 * i] + ((float*)R.ptr)[2 * i + 1];
            double err = std::fabs(got - ref) / std::fabs(ref) / std::ldexp(1.0, -44);
            worst = std::max(worst, err);
            if (!(err <= 4)) bad++;
        }
        report(names[op - 1], bad, worst);
    }
    ctx.release(A); ctx.release(R); ctx.release(B); ctx.release(k); ctx.destroy();
    printf("SELFTEST %s: %s\n", tag.c_str(), failed ? "FAIL (do not use GPU df64 on this device)" : "PASS");
    return failed ? 3 : 0;
}
