// MP09: the GPU exact-sign filter for orient3d (Shewchuk stage A, in exact soft binary64).
//   mp09_orient_filter host [N]     CPU only: soft filter (the same text as the shader) vs the double-precision reference vs the exact sign, plus planted mutants
//   mp09_orient_filter device [N]   the shader on the Vulkan device vs the reference vs the exact sign, plus throughput
// Pass bars (PLAN.md, G2): 0 wrong definite signs against the exact sign; the device and the reference agree on EVERY case (tier 1); the uncertain rate is reported per class.
#include "vkctx.h"
#include "../harness/exact.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <thread>
#include <atomic>
extern "C" { void exactinit(void); double orient3d(double* pa, double* pb, double* pc, double* pd); }
int g_mut = 0;
#define F64SOFT_C 1
#include "shaders/f64soft.inc"
#include "orient_filter.inc"

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static double dbl(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static int sgn(double x) { return (x > 0) - (x < 0); }

// The reference: Shewchuk's stage-A filter in hardware doubles (-ffp-contract=off), with the same window rule.
static bool windowD(double x) { if (x == 0) return true; int e; std::frexp(x, &e); double a = std::fabs(x); return a >= std::ldexp(1.0, -100) && a < std::ldexp(1.0, 101); }
static int filterRef(const double* p, uint64_t* detBits = nullptr, uint64_t* boundBits = nullptr) {
    if (detBits) { *detBits = 0; *boundBits = 0; }
    for (int i = 0; i < 12; i++) if (!windowD(p[i])) return 0;
    const double eps = 1.1102230246251565e-16, errA = (7.0 + 56.0 * eps) * eps;
    const double *a = p, *b = p + 3, *c = p + 6, *d = p + 9;
    double adx = a[0] - d[0], bdx = b[0] - d[0], cdx = c[0] - d[0], ady = a[1] - d[1], bdy = b[1] - d[1], cdy = c[1] - d[1], adz = a[2] - d[2], bdz = b[2] - d[2], cdz = c[2] - d[2];
    double bdxcdy = bdx * cdy, cdxbdy = cdx * bdy, cdxady = cdx * ady, adxcdy = adx * cdy, adxbdy = adx * bdy, bdxady = bdx * ady;
    double det = adz * (bdxcdy - cdxbdy) + bdz * (cdxady - adxcdy) + cdz * (adxbdy - bdxady);
    double perm = (std::fabs(bdxcdy) + std::fabs(cdxbdy)) * std::fabs(adz) + (std::fabs(cdxady) + std::fabs(adxcdy)) * std::fabs(bdz) + (std::fabs(adxbdy) + std::fabs(bdxady)) * std::fabs(cdz);
    double bound = errA * perm;
    if (detBits) { *detBits = bits(det); *boundBits = bits(bound); }
    return det > bound ? 1 : (-det > bound ? -1 : 0);
}
static int filterSoft(const double* p, uint64_t* detBits = nullptr, uint64_t* boundBits = nullptr) {
    uint64_t u[12]; for (int i = 0; i < 12; i++) u[i] = bits(p[i]);
    uint64_t d, b; int s = orient3dFilterSoftEx(u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], d, b);
    if (detBits) { *detBits = d; *boundBits = b; }
    return s;
}

struct Rng { std::mt19937_64 g; explicit Rng(uint64_t s) : g(s) {} double u01() { return (double)(g() >> 11) / 9007199254740992.0; } double sym() { return u01() * 2.0 - 1.0; } int pick(int n) { return (int)(g() % (uint64_t)n); } };
enum Cls { kRandom, kNearCoplanar, kExactCoplanar, kLargeOffset, kMixedExp, kDegenerate, kOutOfWindow, kTieProne, kNumCls };
static const char* kName[] = {"random", "near-coplanar (affine + k ulp)", "exactly coplanar (dyadic)", "large offset 2^40 (coarse grid)", "mixed exponents (2^-30..2^30)", "degenerate (repeated points)", "outside the window (2^200, 2^-200)", "tie-prone (26-bit significands)"};
static double nudge(double x, int ulps) { for (int i = 0; i < std::abs(ulps); i++) x = std::nextafter(x, ulps > 0 ? INFINITY : -INFINITY); return x; }
static void gen(Rng& r, int cls, double q[12]) {
    double (*p)[3] = (double (*)[3])q;
    for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) p[i][k] = r.sym();
    switch (cls) {
        case kNearCoplanar: { double s = r.sym(), t = r.sym(); for (int k = 0; k < 3; k++) p[3][k] = p[0][k] + s * (p[1][k] - p[0][k]) + t * (p[2][k] - p[0][k]); int ax = r.pick(3); p[3][ax] = nudge(p[3][ax], r.pick(7) - 3); break; }
        case kExactCoplanar: for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) p[i][k] = (double)(r.pick(2001) - 1000);
                             { int i = r.pick(9) - 4, j = r.pick(9) - 4; for (int k = 0; k < 3; k++) p[3][k] = p[0][k] + i * (p[1][k] - p[0][k]) + j * (p[2][k] - p[0][k]); } break;
        case kLargeOffset: for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) p[i][k] += 1099511627776.0; break;
        case kMixedExp: for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) p[i][k] = std::ldexp(p[i][k], r.pick(61) - 30); break;
        case kDegenerate: { int a = r.pick(4), b = r.pick(4); for (int k = 0; k < 3; k++) p[b][k] = p[a][k]; break; }
        case kOutOfWindow: for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) p[i][k] = std::ldexp(p[i][k], (r.pick(2) ? 200 : -200)); break;
        case kTieProne:   // differences of 26-bit integers have up to 27 bits and their products up to 54: exact half-way ties are common, which exercises round-half-even
            for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) p[i][k] = std::ldexp((double)(int64_t)(r.g() & 0x3FFFFFF), r.pick(5) - 2);
            break;
        default: break;
    }
}

static int hostMode(long N) {
    long per = N / kNumCls; int rc = 0;
    // the error-bound constant used by the shared text must equal the one computed in double
    { const double eps = 1.1102230246251565e-16; double errA = (7.0 + 56.0 * eps) * eps; bool ok = bits(errA) == ORIENT_ERRBOUND_A_BITS; printf("error-bound constant 0x%016llx %s the one computed in double\n", (unsigned long long)ORIENT_ERRBOUND_A_BITS, ok ? "equals" : "DIFFERS FROM"); rc |= !ok; }
    printf("host: %ld cases per class; soft = the shader's text compiled as C++; ref = hardware doubles; \"soft==ref\" compares the sign AND the determinant and bound bits\n%-38s %10s %12s %12s %14s\n", per, "class", "soft==ref", "wrong(soft)", "wrong(ref)", "uncertain");
    long diffAll = 0, wrongAll = 0;
    long mutDiff[7] = {0};
    for (int cls = 0; cls < kNumCls; cls++) {
        Rng r(500 + cls); long diff = 0, wrongS = 0, wrongR = 0, unc = 0;
        for (long i = 0; i < per; i++) {
            double q[12]; gen(r, cls, q);
            int ex = mesh::exactOrient3d(q, q + 3, q + 6, q + 9);
            g_mut = 0; uint64_t sd, sb, fd, fb; int s = filterSoft(q, &sd, &sb), f = filterRef(q, &fd, &fb);
            diff += s != f || sd != fd || sb != fb; unc += s == 0; wrongS += s != 0 && s != ex; wrongR += f != 0 && f != ex;
            for (int m = 4; m <= 6; m++) { g_mut = m; uint64_t md, mb; int ms = filterSoft(q, &md, &mb); mutDiff[m] += ms != f || md != fd || mb != fb; } g_mut = 0;
        }
        printf("%-38s %10s %12ld %12ld %13.2f%%\n", kName[cls], diff ? "DIFFER" : "identical", wrongS, wrongR, 100.0 * unc / per);
        diffAll += diff; wrongAll += wrongS + wrongR;
    }
    printf("planted mutants of the soft multiply, seen through the filter's determinant and bound bits (a mutant is caught if soft differs from ref somewhere):\n");
    const char* mn[] = {"", "", "", "", "round half up", "truncate", "remainder low 20 bits ignored"};
    for (int m = 4; m <= 6; m++) {
        if (m == 6) { printf("  mutant %d (%s): %s (%ld cases differ); it only matters when a product's remainder is within 2^20 of the half-way point without being exactly on it, about 1 in 2^33 for random operands, so it is caught by the targeted multiply test (poc12_f64soft host: 10 of 600,012 pairs) and not required here\n", m, mn[m], mutDiff[m] ? "caught" : "not seen at filter level", mutDiff[m]); continue; }
        printf("  mutant %d (%s): %s (%ld cases differ)\n", m, mn[m], mutDiff[m] ? "caught" : "NOT CAUGHT", mutDiff[m]); rc |= mutDiff[m] == 0;
    }
    printf("summary: soft vs reference disagreements %ld, wrong definite signs vs exact %ld\n", diffAll, wrongAll);
    rc |= diffAll != 0 || wrongAll != 0;
    printf("%s\n", rc ? "FAIL" : "PASS");
    return rc ? 2 : 0;
}

static int deviceMode(long N) {
    gpu::Ctx ctx;
    if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    printf("device=%s memory=%s\n", ctx.props.deviceName, ctx.memMode());
    auto k = ctx.makeKernel("build/shaders/orient_filter.spv", 2, 4);
    long per = N / kNumCls; int rc = 0; long diffAll = 0, wrongAll = 0, total = 0;
    printf("(dev==ref compares the sign AND the determinant and bound bits)\n%-38s %10s %12s %14s\n", "class", "dev==ref", "wrong(dev)", "uncertain");
    std::vector<double> all; std::vector<int> ref; std::vector<int> ex; std::vector<uint64_t> refDet, refBound;
    for (int cls = 0; cls < kNumCls; cls++) {
        Rng r(900 + cls);
        for (long i = 0; i < per; i++) { double q[12]; gen(r, cls, q); all.insert(all.end(), q, q + 12); { uint64_t d, b; ref.push_back(filterRef(q, &d, &b)); refDet.push_back(d); refBound.push_back(b); } ex.push_back(mesh::exactOrient3d(q, q + 3, q + 6, q + 9)); }
    }
    const uint32_t n = (uint32_t)(all.size() / 12); total = n;
    auto A = ctx.alloc((size_t)n * 96), R = ctx.alloc((size_t)n * 24);
    memcpy(A.ptr, all.data(), (size_t)n * 96);
    // warm-up, then the timed run (upload and download included)
    for (int w = 0; w < 2; w++) { ctx.begin(); ctx.upload(A); ctx.dispatch(k, {&A, &R}, &n, (n + 63) / 64); ctx.download(R); ctx.submitWait(); }
    double t0 = gpu::nowMs(); ctx.begin(); ctx.upload(A); ctx.dispatch(k, {&A, &R}, &n, (n + 63) / 64); ctx.download(R); ctx.submitWait(); double t1 = gpu::nowMs();
    const uint64_t* dev = (const uint64_t*)R.ptr;
    for (int cls = 0; cls < kNumCls; cls++) {
        long diff = 0, wrong = 0, unc = 0;
        for (long i = cls * per; i < (cls + 1) * per; i++) { int ds = (int)dev[3 * i] - 1; diff += ds != ref[i] || dev[3 * i + 1] != refDet[i] || dev[3 * i + 2] != refBound[i]; unc += ds == 0; wrong += ds != 0 && ds != ex[i]; }
        printf("%-38s %10s %12ld %13.2f%%\n", kName[cls], diff ? "DIFFER" : "identical", wrong, 100.0 * unc / per);
        diffAll += diff; wrongAll += wrong;
    }
    // the CPU speed of the same job for scale (single thread, hardware doubles, and the soft text on the CPU)
    double c0 = gpu::nowMs(); long sink = 0; for (uint32_t i = 0; i < n; i++) sink += filterRef(&all[12 * (size_t)i]); double c1 = gpu::nowMs();
    double s0 = gpu::nowMs(); for (uint32_t i = 0; i < n; i++) sink += filterSoft(&all[12 * (size_t)i]); double s1 = gpu::nowMs();
    // multi-thread CPU baseline (hardware doubles, the same filter): the fair comparison for G4. n cases split into equal slices over T threads.
    { unsigned T8 = std::max(1u, std::min(8u, std::thread::hardware_concurrency())); std::vector<long> part(T8, 0); double m0 = gpu::nowMs();
      std::vector<std::thread> th; for (unsigned w = 0; w < T8; w++) th.emplace_back([&, w] { long sk = 0; uint32_t lo = (uint64_t)n * w / T8, hi = (uint64_t)n * (w + 1) / T8; for (uint32_t i = lo; i < hi; i++) sk += filterRef(&all[12 * (size_t)i]); part[w] = sk; });
      for (auto& x : th) x.join(); double m1 = gpu::nowMs(); for (long v : part) sink += v;
      printf("CPU hardware doubles, %u threads %.2f ms (%.1f M cases/s)\n", T8, m1 - m0, n / (m1 - m0) / 1000.0); }
    printf("throughput on %u cases: device total %.2f ms (%.1f M cases/s, upload and download included); CPU hardware doubles, 1 thread %.2f ms; CPU soft text, 1 thread %.2f ms (sink %ld)\n", n, t1 - t0, n / (t1 - t0) / 1000.0, c1 - c0, s1 - s0, sink);
    printf("summary: device vs reference disagreements %ld of %ld, wrong definite signs vs exact %ld\n", diffAll, total, wrongAll);
    rc = diffAll != 0 || wrongAll != 0;
    printf("%s\n", rc ? "FAIL" : "PASS");
    ctx.release(A); ctx.release(R); ctx.release(k); ctx.destroy();
    return rc ? 3 : 0;
}


// ---- streaming run for the G2 bar (>= 1e9 random inputs plus every adversarial class) ----
// Inputs are generated in chunks by all cores (deterministic: the seed depends on class, chunk and thread), run through the shader, and every case is checked:
// the device's sign, determinant bits and bound bits against the hardware-double reference, and every DEFINITE device sign against Shewchuk's adaptive orient3d (exact).
static int streamMode(long randomN, long advN) {
    gpu::Ctx ctx;
    if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    exactinit();
    printf("device=%s memory=%s; stream: %ld random cases + %ld per adversarial class\n", ctx.props.deviceName, ctx.memMode(), randomN, advN);
    auto k = ctx.makeKernel("build/shaders/orient_filter.spv", 2, 4);
    const uint32_t C = 2000000;
    auto A = ctx.alloc((size_t)C * 96), R = ctx.alloc((size_t)C * 24);
    const unsigned T = std::max(1u, std::thread::hardware_concurrency());
    std::vector<uint64_t> refDet(C), refBound(C); std::vector<int8_t> refSign(C);
    struct Tot { long n = 0, bitDiff = 0, wrong = 0, unc = 0, pos = 0, neg = 0; double devMs = 0; } all;
    int rc = 0; double wall0 = gpu::nowMs();
    for (int cls = 0; cls < kNumCls; cls++) {
        long want = cls == kRandom ? randomN : advN, done = 0, chunk = 0; Tot t;
        while (done < want) {
            uint32_t n = (uint32_t)std::min<long>(C, want - done);
            std::vector<std::thread> th;
            for (unsigned w = 0; w < T; w++) th.emplace_back([&, w] {
                uint32_t lo = (uint64_t)n * w / T, hi = (uint64_t)n * (w + 1) / T;
                Rng r(0x9E3779B97F4A7C15ull * (cls + 1) + 1000003ull * chunk + w);
                for (uint32_t i = lo; i < hi; i++) { double q[12]; gen(r, cls, q); memcpy((char*)A.ptr + (size_t)i * 96, q, 96); uint64_t d, b; refSign[i] = (int8_t)filterRef(q, &d, &b); refDet[i] = d; refBound[i] = b; }
            });
            for (auto& x : th) x.join();
            double t0 = gpu::nowMs();
            ctx.begin(); ctx.upload(A, (size_t)n * 96); ctx.dispatch(k, {&A, &R}, &n, (n + 63) / 64); ctx.download(R, (size_t)n * 24); ctx.submitWait();
            t.devMs += gpu::nowMs() - t0;
            const uint64_t* dev = (const uint64_t*)R.ptr;
            std::atomic<long> bitDiff{0}, wrong{0}, unc{0}, pos{0}, neg{0};
            th.clear();
            for (unsigned w = 0; w < T; w++) th.emplace_back([&, w] {
                uint32_t lo = (uint64_t)n * w / T, hi = (uint64_t)n * (w + 1) / T; long bd = 0, wr = 0, un = 0, ps = 0, ng = 0;
                for (uint32_t i = lo; i < hi; i++) {
                    int ds = (int)dev[3 * (size_t)i] - 1;
                    bd += ds != refSign[i] || dev[3 * (size_t)i + 1] != refDet[i] || dev[3 * (size_t)i + 2] != refBound[i];
                    if (ds == 0) un++; else { double q[12]; memcpy(q, (char*)A.ptr + (size_t)i * 96, 96); int ex = sgn(orient3d(q, q + 3, q + 6, q + 9)); wr += ex != ds; (ds > 0 ? ps : ng)++; }
                }
                bitDiff += bd; wrong += wr; unc += un; pos += ps; neg += ng;
            });
            for (auto& x : th) x.join();
            t.n += n; t.bitDiff += bitDiff; t.wrong += wrong; t.unc += unc; t.pos += pos; t.neg += neg; done += n; chunk++;
            if (chunk % 50 == 0) { printf("  %-38s %12ld / %ld cases, bit differences %ld, wrong %ld\n", kName[cls], done, want, t.bitDiff, t.wrong); fflush(stdout); }
        }
        printf("%-38s n=%-11ld device==ref %s (%ld bit differences)  wrong definite signs vs Shewchuk exact: %ld  uncertain %.3f%%  (+%ld, -%ld)  device time %.2f s\n", kName[cls], t.n, t.bitDiff ? "NO" : "yes", t.bitDiff, t.wrong, 100.0 * t.unc / t.n, t.pos, t.neg, t.devMs / 1000);
        all.n += t.n; all.bitDiff += t.bitDiff; all.wrong += t.wrong; all.unc += t.unc; all.devMs += t.devMs;
    }
    printf("summary: %ld cases, device vs reference bit differences %ld, wrong definite signs vs Shewchuk exact %ld, uncertain %.2f%% overall, device time %.1f s, wall %.1f s\n", all.n, all.bitDiff, all.wrong, 100.0 * all.unc / all.n, all.devMs / 1000, (gpu::nowMs() - wall0) / 1000);
    rc = all.bitDiff != 0 || all.wrong != 0;
    printf("%s\n", rc ? "FAIL" : "PASS");
    ctx.release(A); ctx.release(R); ctx.release(k); ctx.destroy();
    return rc ? 3 : 0;
}

int main(int argc, char** argv) {
    std::string m = argc > 1 ? argv[1] : "host"; long N = argc > 2 ? atol(argv[2]) : 700000;
    if (m == "stream") return streamMode(argc > 2 ? atol(argv[2]) : 1000000000L, argc > 3 ? atol(argv[3]) : 50000000L);
    return m == "device" ? deviceMode(N) : hostMode(N);
}
