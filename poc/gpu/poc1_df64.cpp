// POC 1: df64 add/mul/div/sqrt on the GPU. Writes inputs and results to out/ for verify_df64.py,
// which checks them against an exact (arbitrary precision) oracle. Run once per shader variant.
#include "vkctx.h"

#include <cmath>
#include <random>

static const uint32_t kPerCat = 20000;
enum Cat { kMixed, kCancel, kWide, kTiny, kHuge, kNumCats };

static void split(double d, float& hi, float& lo) {
    hi = (float)d;
    lo = std::isfinite(hi) ? (float)(d - (double)hi) : 0.f;
}

// Operation-aware preflight: true if the batch element must go to the CPU path.
static bool needsCpu(int op, double a, double b) {
    auto bad = [](double x) { double m = std::fabs(x); return !std::isfinite(x) || (m != 0 && (m < 1e-30 || m > 1e30)); };
    if (bad(a) || ((op != 3) && bad(b))) return true;
    double r = op == 0 ? a + b : op == 1 ? a * b : op == 2 ? a / b : std::sqrt(std::fabs(a));
    return bad(r) || (op == 2 && b == 0);
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "build/shaders";
    const std::string tag = argc > 2 ? argv[2] : "run";
    std::mt19937_64 rng(42);
    auto u01 = [&] { return std::uniform_real_distribution<double>(0, 1)(rng); };
    auto sgn = [&] { return (rng() & 1) ? 1.0 : -1.0; };
    auto mant = [&] { return 1.0 + u01(); };
    const uint32_t n = kPerCat * kNumCats;
    std::vector<double> a(n), b(n);
    std::vector<uint8_t> cat(n);
    for (uint32_t c = 0; c < kNumCats; c++)
        for (uint32_t i = 0; i < kPerCat; i++) {
            uint32_t j = c * kPerCat + i;
            cat[j] = c;
            double x = sgn() * mant() * std::ldexp(1.0, (int)(rng() % 41) - 20), y;
            switch (c) {
                case kMixed: y = sgn() * mant() * std::ldexp(1.0, (int)(rng() % 41) - 20); break;
                case kCancel: y = -x * (1.0 + std::ldexp(u01() + 0.5, -(int)(20 + rng() % 25))); break;
                case kWide: y = x * std::ldexp(mant(), (int)(rng() % 101) - 50) * sgn(); break;
                case kTiny: x = sgn() * mant() * std::pow(10.0, -36 + 5 * u01()); y = sgn() * mant() * std::pow(10.0, -36 + 5 * u01()); break;
                default: x = sgn() * mant() * std::pow(10.0, 30 + 8 * u01()); y = sgn() * mant() * std::pow(10.0, 30 + 8 * u01()); break;
            }
            a[j] = x; b[j] = y;
        }
    std::vector<float> sa(2 * n), sb(2 * n);
    for (uint32_t i = 0; i < n; i++) { split(a[i], sa[2 * i], sa[2 * i + 1]); split(b[i], sb[2 * i], sb[2 * i + 1]); }
    auto wr = [&](const std::string& f, const void* p, size_t bytes) {
        FILE* fp = fopen(("out/" + f).c_str(), "wb"); fwrite(p, 1, bytes, fp); fclose(fp);
    };
    wr("df64_a64.bin", a.data(), n * 8); wr("df64_b64.bin", b.data(), n * 8);
    wr("df64_asplit.bin", sa.data(), n * 8); wr("df64_bsplit.bin", sb.data(), n * 8);
    wr("df64_cat.bin", cat.data(), n);
    for (int op = 0; op < 4; op++) {
        std::vector<uint8_t> fl(n);
        for (uint32_t i = 0; i < n; i++) fl[i] = needsCpu(op, a[i], b[i]);
        wr("df64_flag_" + std::to_string(op) + ".bin", fl.data(), n);
    }

    gpu::Ctx ctx;
    if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    const char* fm = getenv("MVK_CONFIG_FAST_MATH_ENABLED");
    printf("device=%s MVK_CONFIG_FAST_MATH_ENABLED=%s variant=%s\n", ctx.props.deviceName, fm ? fm : "(default)", tag.c_str());
    auto A = ctx.alloc(n * 8), B = ctx.alloc(n * 8), R = ctx.alloc(n * 8);
    memcpy(A.ptr, sa.data(), n * 8); memcpy(B.ptr, sb.data(), n * 8);
    auto k = ctx.makeKernel(dir + "/df64_" + tag + ".spv", 3, 8);
    for (uint32_t op = 0; op < 4; op++) {
        uint32_t pc[2] = {n, op};
        ctx.begin();
        ctx.dispatch(k, {&A, &B, &R}, pc, (n + 63) / 64);
        ctx.submitWait();
        wr("df64_res_" + tag + "_" + std::to_string(op) + ".bin", R.ptr, n * 8);
    }
    ctx.release(A); ctx.release(B); ctx.release(R); ctx.release(k); ctx.destroy();
    return 0;
}
