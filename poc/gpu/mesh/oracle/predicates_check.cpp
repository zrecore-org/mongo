// MP02: oracle agreement and planted-mutant check for the geometric predicates.
//   predicates_check [N]      N cases per predicate (default 2,000,000; the plan's bar is 1e7: pass 10000000)
// Checks, on random and adversarial inputs:
//   1. Shewchuk's adaptive orient2d/orient3d (predicates.c, public domain, fetched by oracle/fetch.sh) agrees in sign with the exact integer
//      computation (harness/exact.h, Boost.Multiprecision cpp_int) on every case.
//   2. A floating-point FILTER for orient3d (Shewchuk's stage-A error bound; this is what a GPU filter would compute) never returns a definite
//      sign that differs from the exact sign: it must answer +, - or "uncertain". The uncertain rate is reported per input class.
//   3. Planted mutants of that filter are caught (a check that cannot fail proves nothing; same pattern as poc12_f64soft.cpp).
// Only +, -, *, / and ldexp are used to build inputs, so the inputs are bit-identical on every platform.
#include "../harness/exact.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" {
void exactinit(void);
double orient2d(double* pa, double* pb, double* pc);
double orient3d(double* pa, double* pb, double* pc, double* pd);
}

static int g_mut = 0;   // 0 = correct filter

static int sgn(double x) { return (x > 0) - (x < 0); }

// Filter: +1 / -1 definite, 0 uncertain. Shewchuk's orient3d stage A (o3derrboundA).
static int filterOrient3d(const double* a, const double* b, const double* c, const double* d) {
    const double eps = 1.1102230246251565e-16;    // 2^-53
    const double o3derrboundA = (7.0 + 56.0 * eps) * eps;
    double adx = a[0] - d[0], bdx = b[0] - d[0], cdx = c[0] - d[0];
    double ady = a[1] - d[1], bdy = b[1] - d[1], cdy = c[1] - d[1];
    double adz = a[2] - d[2], bdz = b[2] - d[2], cdz = c[2] - d[2];
    double bdxcdy = bdx * cdy, cdxbdy = cdx * bdy;
    double cdxady = cdx * ady, adxcdy = adx * cdy;
    double adxbdy = adx * bdy, bdxady = bdx * ady;
    double det = adz * (bdxcdy - cdxbdy) + bdz * (cdxady - adxcdy) + cdz * (adxbdy - bdxady);
    double permanent = (std::fabs(bdxcdy) + std::fabs(cdxbdy)) * std::fabs(adz) + (std::fabs(cdxady) + std::fabs(adxcdy)) * std::fabs(bdz) +
                       (std::fabs(adxbdy) + std::fabs(bdxady)) * std::fabs(cdz);
    double bound = o3derrboundA * permanent;
    if (g_mut == 1) bound = eps * permanent;      // mutant 1: forgot the factor 7 in the bound
    if (g_mut == 2) bound = 0;                    // mutant 2: no filter, trust the sign of the rounded determinant
    int s;
    if (g_mut == 3) s = det >= bound ? 1 : (-det > bound ? -1 : 0);   // mutant 3: >= instead of >, a zero determinant counts as definite
    else s = det > bound ? 1 : (-det > bound ? -1 : 0);
    if (g_mut == 4) s = -s;                       // mutant 4: sign flipped
    return s;
}

struct Rng {
    std::mt19937_64 g;
    explicit Rng(uint64_t s) : g(s) {}
    double u01() { return (double)(g() >> 11) / 9007199254740992.0; }          // [0,1), exactly representable
    double sym() { return u01() * 2.0 - 1.0; }
    int pick(int n) { return (int)(g() % (uint64_t)n); }
};

enum Class3 { kRandom, kNearCoplanar, kExactCoplanar, kLargeOffset, kMixedExp, kDegenerate, kNumClasses3 };
static const char* kClassName3[] = {"random", "near-coplanar (affine + k ulp)", "exactly coplanar (dyadic)", "large offset 2^40 (coarse grid)", "mixed exponents", "degenerate (repeated points)"};

static double nudge(double x, int ulps) { for (int i = 0; i < std::abs(ulps); i++) x = std::nextafter(x, ulps > 0 ? INFINITY : -INFINITY); return x; }

static void gen3(Rng& r, int cls, double p[4][3]) {
    for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) p[i][k] = r.sym();
    switch (cls) {
        case kRandom: break;
        case kNearCoplanar: case kLargeOffset: {
            // large offset: shift a, b, c first, then derive d from the shifted points, so the near-coplanar structure survives the rounding
            if (cls == kLargeOffset) for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) p[i][k] += 1099511627776.0;   // 2^40
            double s = r.sym(), t = r.sym();
            for (int k = 0; k < 3; k++) p[3][k] = p[0][k] + s * (p[1][k] - p[0][k]) + t * (p[2][k] - p[0][k]);
            int u = r.pick(7) - 3;                    // 0..3 ulps in either direction on one coordinate
            int ax = r.pick(3);
            p[3][ax] = nudge(p[3][ax], u);
            break;
        }
        case kExactCoplanar: {       // integer coordinates, d = a + i (b - a) + j (c - a) with integer i, j: exactly coplanar
            for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) p[i][k] = (double)(r.pick(2001) - 1000);
            int i = r.pick(9) - 4, j = r.pick(9) - 4;
            for (int k = 0; k < 3; k++) p[3][k] = p[0][k] + i * (p[1][k] - p[0][k]) + j * (p[2][k] - p[0][k]);
            break;
        }
        case kMixedExp:
            for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) p[i][k] = std::ldexp(p[i][k], r.pick(61) - 30);
            break;
        case kDegenerate: {
            int a = r.pick(4), b = r.pick(4);
            for (int k = 0; k < 3; k++) p[b][k] = p[a][k];
            break;
        }
    }
}

int main(int argc, char** argv) {
    long N = argc > 1 ? atol(argv[1]) : 2000000;
    exactinit();
    auto t0 = std::chrono::steady_clock::now();
    int rc = 0;

    // ---- 1 + 2: orient3d, per class ----
    long perClass = N / kNumClasses3;
    long dis3 = 0, wrongF = 0, tot3 = 0;
    printf("orient3d: %ld cases per class\n%-42s %10s %12s %14s\n", perClass, "class", "vs exact", "filter wrong", "filter uncertain");
    std::vector<std::vector<std::pair<int, int>>> mutantStats(5);
    for (int cls = 0; cls < kNumClasses3; cls++) {
        Rng r(1000 + cls);
        long dis = 0, wrong = 0, unc = 0, zero = 0;
        long mutWrong[5] = {0, 0, 0, 0, 0};
        for (long i = 0; i < perClass; i++) {
            double p[4][3]; gen3(r, cls, p);
            int ex = mesh::exactOrient3d(p[0], p[1], p[2], p[3]);
            double sh = orient3d(p[0], p[1], p[2], p[3]);
            if (ex == 2 || sgn(sh) != ex) dis++;
            zero += ex == 0;
            g_mut = 0;
            int f = filterOrient3d(p[0], p[1], p[2], p[3]);
            if (f == 0) unc++; else if (f != ex) wrong++;
            for (int m = 1; m <= 4; m++) { g_mut = m; int fm = filterOrient3d(p[0], p[1], p[2], p[3]); if (fm != 0 && fm != ex) mutWrong[m]++; }
            g_mut = 0;
        }
        printf("%-42s %10s %12ld %13.2f%%   (exact zeros: %ld)\n", kClassName3[cls], dis ? "DISAGREE" : "agree", wrong, 100.0 * unc / perClass, zero);
        dis3 += dis; wrongF += wrong; tot3 += perClass;
        for (int m = 1; m <= 4; m++) mutantStats[m].push_back({cls, (int)std::min<long>(mutWrong[m], 2000000000L)});
    }

    // ---- 1: orient2d ----
    long dis2 = 0, tot2 = 0;
    {
        Rng r(77);
        long per = N / 4;
        for (int cls = 0; cls < 4; cls++) for (long i = 0; i < per; i++) {
            double p[3][2];
            for (int a = 0; a < 3; a++) for (int k = 0; k < 2; k++) p[a][k] = r.sym();
            if (cls == 1 || cls == 3) {      // near-collinear: c = a + t (b - a), nudged
                double t = r.sym(); for (int k = 0; k < 2; k++) p[2][k] = p[0][k] + t * (p[1][k] - p[0][k]);
                p[2][r.pick(2)] = nudge(p[2][r.pick(2)], r.pick(5) - 2);
            }
            if (cls == 2) {                  // exactly collinear integers
                for (int a = 0; a < 2; a++) for (int k = 0; k < 2; k++) p[a][k] = (double)(r.pick(2001) - 1000);
                int j = r.pick(9) - 4; for (int k = 0; k < 2; k++) p[2][k] = p[0][k] + j * (p[1][k] - p[0][k]);
            }
            if (cls == 3) for (int a = 0; a < 3; a++) for (int k = 0; k < 2; k++) p[a][k] += 1099511627776.0;
            int ex = mesh::exactOrient2d(p[0], p[1], p[2]);
            if (ex == 2 || sgn(orient2d(p[0], p[1], p[2])) != ex) dis2++;
            tot2++;
        }
    }
    printf("orient2d: %ld cases (random, near-collinear, exactly collinear, large offset): %s (%ld disagree)\n", tot2, dis2 ? "DISAGREE" : "agree", dis2);

    // ---- 3: mutants ----
    const char* mname[] = {"", "bound without the factor 7", "no filter (sign of rounded determinant)", "'>=' so a zero determinant is definite", "sign flipped"};
    printf("planted mutants of the orient3d filter (a mutant is caught if it returns a wrong definite sign somewhere):\n");
    for (int m = 1; m <= 4; m++) {
        long total = 0; std::string where;
        for (auto& pr : mutantStats[m]) { total += pr.second; if (pr.second) where += std::string(where.empty() ? "" : ", ") + std::to_string(pr.second) + " in '" + kClassName3[pr.first] + "'"; }
        printf("  mutant %d (%s): %s%s%s\n", m, mname[m], total ? "caught" : "NOT CAUGHT", total ? " : " : "", where.c_str());
        if (!total) rc = 3;
    }

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("summary: Shewchuk vs exact disagreements: orient3d %ld of %ld, orient2d %ld of %ld; correct filter wrong definite signs: %ld; %.1f s\n", dis3, tot3, dis2, tot2, wrongF, secs);
    if (dis3 || dis2 || wrongF) rc = 2;
    printf("%s\n", rc ? "FAIL" : "PASS");
    return rc;
}
