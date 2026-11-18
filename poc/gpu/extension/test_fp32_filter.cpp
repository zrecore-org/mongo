// SPDX-License-Identifier: SSPL-1.0
// MP17: checks the binary32 pre-filter of gpu_src/pip_filter.inc (FP32PRE) against the exact predicates of meshcontains_core.h. The C++ code below is a line for line
// twin of f32of / orient2dF32 / orient3dF32 in the GLSL (same truncating binary64 -> binary32 conversion, same expression order, same bound 2^-19 * P + 1e-20).
// For every input where the filter answers +1/-1, that answer must equal the exact sign (a "false definite" is a failure). The test also reports the smallest bound
// factor that would have been enough on the inputs seen (Kneeded = max |det| / P over the inputs the filter would have decided wrongly), so the margin of the
// chosen factor is measured, not assumed. Usage: test_fp32_filter [samples-per-class] [K-scale]   (K-scale 0.5 = the planted halved-bound mutant)
// Build with -ffp-contract=off, and again with -ffp-contract=fast (the GPU compiler may fuse multiply-adds): both must give 0 false definite.
#include "meshcontains_core.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <cstring>
#include <cmath>

static float f32of(double x) {
    uint64_t b; std::memcpy(&b, &x, 8);
    uint32_t hy = (uint32_t)(b >> 32), hx = (uint32_t)b;
    uint32_t e = (hy >> 20) & 0x7FFu;
    if (e < 1023u - 100u) return 0.0f;
    uint32_t m23 = ((hy & 0xFFFFFu) << 3) | (hx >> 29);
    uint32_t r = (hy & 0x80000000u) | ((e - 896u) << 23) | m23;
    float f; std::memcpy(&f, &r, 4); return f;
}
static bool f32ok(double x) { uint64_t b; std::memcpy(&b, &x, 8); return ((b >> 52) & 0x7FFu) < 1023u + 20u; }
static double g_scale = 1.0;
static int g_raw = 0;   // sign of the raw binary32 determinant, whatever the bound says
static const float FP32K = 1.9073486328125e-06f;
// returns the sign, or 0; ratio = |det| / P (infinity-safe) for the Kneeded statistic
static int o2(const double* a, const double* b, const double* c, double& ratio) {
    ratio = 0; g_raw = 0;
    if (!(f32ok(a[0]) && f32ok(a[1]) && f32ok(b[0]) && f32ok(b[1]) && f32ok(c[0]) && f32ok(c[1]))) return 0;
    float Ax = f32of(a[0]), Ay = f32of(a[1]), Bx = f32of(b[0]), By = f32of(b[1]), Cx = f32of(c[0]), Cy = f32of(c[1]);
    float l = (Ax - Cx) * (By - Cy), r = (Ay - Cy) * (Bx - Cx), det = l - r;
    float P = (std::fabs(Ax) + std::fabs(Cx)) * (std::fabs(By) + std::fabs(Cy)) + (std::fabs(Ay) + std::fabs(Cy)) * (std::fabs(Bx) + std::fabs(Cx));
    float bound = (float)(g_scale * FP32K) * P + 1e-20f;
    ratio = P > 0 ? std::fabs(det) / P : 0; g_raw = det > 0 ? 1 : det < 0 ? -1 : 0;
    if (det > bound) return 1;
    if (-det > bound) return -1;
    return 0;
}
static int o3(const double* a, const double* b, const double* c, const double* d, double& ratio) {
    ratio = 0; g_raw = 0;
    for (int i = 0; i < 3; i++) if (!(f32ok(a[i]) && f32ok(b[i]) && f32ok(c[i]) && f32ok(d[i]))) return 0;
    float Ax = f32of(a[0]), Ay = f32of(a[1]), Az = f32of(a[2]), Bx = f32of(b[0]), By = f32of(b[1]), Bz = f32of(b[2]);
    float Cx = f32of(c[0]), Cy = f32of(c[1]), Cz = f32of(c[2]), Dx = f32of(d[0]), Dy = f32of(d[1]), Dz = f32of(d[2]);
    float adx = Ax - Dx, bdx = Bx - Dx, cdx = Cx - Dx, ady = Ay - Dy, bdy = By - Dy, cdy = Cy - Dy, adz = Az - Dz, bdz = Bz - Dz, cdz = Cz - Dz;
    float det = adz * (bdx * cdy - cdx * bdy) + bdz * (cdx * ady - adx * cdy) + cdz * (adx * bdy - bdx * ady);
    float sax = std::fabs(Ax) + std::fabs(Dx), sbx = std::fabs(Bx) + std::fabs(Dx), scx = std::fabs(Cx) + std::fabs(Dx), say = std::fabs(Ay) + std::fabs(Dy),
          sby = std::fabs(By) + std::fabs(Dy), scy = std::fabs(Cy) + std::fabs(Dy);
    float saz = std::fabs(Az) + std::fabs(Dz), sbz = std::fabs(Bz) + std::fabs(Dz), scz = std::fabs(Cz) + std::fabs(Dz);
    float P = saz * (sbx * scy + scx * sby) + sbz * (scx * say + sax * scy) + scz * (sax * sby + sbx * say);
    float bound = (float)(g_scale * FP32K) * P + 1e-20f;
    ratio = P > 0 ? std::fabs(det) / P : 0; g_raw = det > 0 ? 1 : det < 0 ? -1 : 0;
    if (det > bound) return 1;
    if (-det > bound) return -1;
    return 0;
}
int main(int argc, char** argv) {
    const long N = argc > 1 ? atol(argv[1]) : 1000000;
    g_scale = argc > 2 ? atof(argv[2]) : 1.0;
    std::mt19937_64 rng(20261118);
    auto U = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
    auto ulps = [&](double x, int k) { for (int i = 0; i < std::abs(k); i++) x = std::nextafter(x, k > 0 ? 1e300 : -1e300); return x; };
    const char* names[] = {"random [-1,1]", "near-coplanar (few ulps)", "exactly coplanar (grid)", "large offset, small spread", "mixed magnitudes 2^-20..2^19", "near-identical points"};
    long bad3[6] = {0}, dec3[6] = {0}, n3[6] = {0}, bad2[6] = {0}, dec2[6] = {0}, n2[6] = {0};
    double need3 = 0, need2 = 0;
    for (int cls = 0; cls < 6; cls++) {
        for (long it = 0; it < N; it++) {
            double p[4][3];
            switch (cls) {
            case 0: for (auto& q : p) for (double& x : q) x = U(-1, 1); break;
            case 1: {   // d on the plane through a,b,c up to a few ulps per coordinate
                for (int k = 0; k < 3; k++) for (int i = 0; i < 3; i++) p[k][i] = U(-1, 1);
                double u = U(0, 1), v = U(0, 1 - u);
                for (int i = 0; i < 3; i++) p[3][i] = p[0][i] + u * (p[1][i] - p[0][i]) + v * (p[2][i] - p[0][i]);
                for (int i = 0; i < 3; i++) p[3][i] = ulps(p[3][i], (int)(rng() % 9) - 4);
                break; }
            case 2: for (auto& q : p) for (double& x : q) x = (double)((int)(rng() % 7) - 3); break;   // many exactly coplanar / collinear
            case 3: { double off[3] = {U(-2000, 2000), U(-2000, 2000), U(-2000, 2000)};
                for (auto& q : p) for (int i = 0; i < 3; i++) q[i] = off[i] + U(-1e-3, 1e-3); break; }
            case 4: for (auto& q : p) for (double& x : q) x = U(-1, 1) * std::ldexp(1.0, (int)(rng() % 40) - 20); break;
            case 5: { double base[3] = {U(-1, 1), U(-1, 1), U(-1, 1)};
                for (auto& q : p) for (int i = 0; i < 3; i++) q[i] = ulps(base[i], (int)(rng() % 41) - 20); break; }
            }
            double r3, r2;
            int f3 = o3(p[0], p[1], p[2], p[3], r3), e3 = meshcontains::orient3dSign(p[0], p[1], p[2], p[3]);
            n3[cls]++; if (f3) { dec3[cls]++; if (f3 != e3) bad3[cls]++; }
            if (g_raw != e3 && r3 > need3) need3 = r3;   // the raw sign is wrong (or the exact value is zero): a sound bound factor must exceed |det|/P here
            double a2[2] = {p[0][1], p[0][2]}, b2[2] = {p[1][1], p[1][2]}, c2[2] = {p[2][1], p[2][2]};
            int f2 = o2(a2, b2, c2, r2), e2 = meshcontains::orient2dSign(a2, b2, c2);
            n2[cls]++; if (f2) { dec2[cls]++; if (f2 != e2) bad2[cls]++; }
            if (g_raw != e2 && r2 > need2) need2 = r2;
        }
    }
    long tb = 0;
    for (int c = 0; c < 6; c++) {
        std::printf("%-30s orient3d: decided %5.1f%% falseDefinite %ld | orient2d: decided %5.1f%% falseDefinite %ld\n", names[c], 100.0 * dec3[c] / n3[c], bad3[c], 100.0 * dec2[c] / n2[c], bad2[c]);
        tb += bad3[c] + bad2[c];
    }
    std::printf("K scale %g (K = %g): total falseDefinite %ld; largest |det|/P among inputs whose filter sign differs from the exact one: orient3d %.3g, orient2d %.3g (K itself %.3g, ratio K/needed: o3 %.3g o2 %.3g)\n",
                g_scale, g_scale * FP32K, tb, need3, need2, (double)FP32K * g_scale, need3 > 0 ? FP32K * g_scale / need3 : INFINITY, need2 > 0 ? FP32K * g_scale / need2 : INFINITY);
    return tb == 0 ? 0 : 1;
}
