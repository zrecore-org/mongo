// MP17 step 1: micro-benchmark, single thread. ns/op for the plane-plane-plane vertex sign at 256/384/512 bits (full-width mul and the
// zero-limb-skipping mulMag), versus Shewchuk's adaptive orient3d (predicates.c) and versus Boost cpp_int, on the same random inputs
// (L = 26 integer coordinates, so the doubles are exact). Also reports limb multiply-add counts.
//   planebased_bench
// Inputs differ in kind: orient3d takes 4 points (12 coordinates); the plane-based vertex test takes 4 PLANES (16 integers, precomputed once per
// plane, which is how a plane-based boolean stores them). Both are timed on the same random points (planes built from points 0..11, orient3d on
// points 0..3). The "pipeline" row also builds the 4 planes from the 12 points inside the timed loop.
#include "predicates.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>

extern "C" {
void exactinit(void);
double orient3d(double* pa, double* pb, double* pc, double* pd);
}

using namespace pb;
using Big = boost::multiprecision::cpp_int;

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
};

static const int NC = 2048;
static const int64_t V = ((int64_t)1 << 26) - 1;
struct Pts { int64_t P[12][3]; double D[12][3]; };
static std::vector<Pts> g_pts(NC);

static double nowNs() { return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

template <class F> static double timeIt(F&& f, int reps) {   // ns per call, best of 5 passes over NC cases
    double best = 1e30;
    for (int pass = 0; pass < 5; pass++) {
        double t0 = nowNs();
        for (int r = 0; r < reps; r++) for (int i = 0; i < NC; i++) f(i);
        double t1 = nowNs();
        best = std::min(best, (t1 - t0) / ((double)reps * NC));
    }
    return best;
}

static volatile long g_sink = 0;

// cpp_int version of the same 28-multiplication evaluation
static int bigVertexSide(const Big* A0, const Big* A1, const Big* A2, const Big* q) {
    Big m01 = A1[0] * A2[1] - A1[1] * A2[0], m02 = A1[0] * A2[2] - A1[2] * A2[0], m03 = A1[0] * A2[3] - A1[3] * A2[0];
    Big m12 = A1[1] * A2[2] - A1[2] * A2[1], m13 = A1[1] * A2[3] - A1[3] * A2[1], m23 = A1[2] * A2[3] - A1[3] * A2[2];
    Big M0 = A0[1] * m23 - A0[2] * m13 + A0[3] * m12, M1 = A0[0] * m23 - A0[2] * m03 + A0[3] * m02, M2 = A0[0] * m13 - A0[1] * m03 + A0[3] * m01;
    Big W = A0[0] * m12 - A0[1] * m02 + A0[2] * m01;
    Big Num = -q[0] * M0 + q[1] * M1 - q[2] * M2 + q[3] * W;
    if (W == 0) return 2;
    return Num.sign() * W.sign();
}

template <int N> struct Planes { Plane<N> p[NC][4]; };

template <int N> static void runWidth(Planes<N>& pl, std::vector<int>& ref, const char* name) {
    for (int i = 0; i < NC; i++) for (int k = 0; k < 4; k++)
        pl.p[i][k] = planeFromPoints<N, false>(g_pts[i].P[3 * k], g_pts[i].P[3 * k + 1], g_pts[i].P[3 * k + 2]);
    // agreement check (naive, trim, reference)
    for (int i = 0; i < NC; i++) {
        int a = vertexSide<N, false>(pl.p[i][0], pl.p[i][1], pl.p[i][2], pl.p[i][3]);
        int b = vertexSide<N, true>(pl.p[i][0], pl.p[i][1], pl.p[i][2], pl.p[i][3]);
        if (a != ref[i] || b != ref[i]) { printf("DISAGREEMENT at %d width %d\n", i, 32 * N); exit(3); }
    }
    g_fi.macs = 0;
    for (int i = 0; i < NC; i++) vertexSide<N, false>(pl.p[i][0], pl.p[i][1], pl.p[i][2], pl.p[i][3]);
    double macsNaive = (double)g_fi.macs / NC;
    g_fi.macs = 0;
    for (int i = 0; i < NC; i++) vertexSide<N, true>(pl.p[i][0], pl.p[i][1], pl.p[i][2], pl.p[i][3]);
    double macsTrim = (double)g_fi.macs / NC;
    g_fi.macs = 0;
    for (int i = 0; i < NC; i++) planeFromPoints<N, false>(g_pts[i].P[0], g_pts[i].P[1], g_pts[i].P[2]);
    double macsPlaneNaive = (double)g_fi.macs / NC;
    g_fi.macs = 0;
    for (int i = 0; i < NC; i++) planeFromPoints<N, true>(g_pts[i].P[0], g_pts[i].P[1], g_pts[i].P[2]);
    double macsPlaneTrim = (double)g_fi.macs / NC;
    g_fi.macs = 0;
    for (int i = 0; i < NC; i++) pointSide<N, false>(pl.p[i][3], g_pts[i].P[0]);
    double macsPsNaive = (double)g_fi.macs / NC;
    g_fi.macs = 0;
    for (int i = 0; i < NC; i++) pointSide<N, true>(pl.p[i][3], g_pts[i].P[0]);
    double macsPsTrim = (double)g_fi.macs / NC;

    long sink = 0;
    int reps = 40;
    double tv = timeIt([&](int i) { sink += vertexSide<N, false>(pl.p[i][0], pl.p[i][1], pl.p[i][2], pl.p[i][3]); }, reps);
    double tvT = timeIt([&](int i) { sink += vertexSide<N, true>(pl.p[i][0], pl.p[i][1], pl.p[i][2], pl.p[i][3]); }, reps);
    double tp = timeIt([&](int i) { sink += planeFromPoints<N, false>(g_pts[i].P[0], g_pts[i].P[1], g_pts[i].P[2]).c[3].w[0]; }, reps);
    double tpT = timeIt([&](int i) { sink += planeFromPoints<N, true>(g_pts[i].P[0], g_pts[i].P[1], g_pts[i].P[2]).c[3].w[0]; }, reps);
    double ts = timeIt([&](int i) { sink += pointSide<N, false>(pl.p[i][3], g_pts[i].P[0]); }, reps);
    double tsT = timeIt([&](int i) { sink += pointSide<N, true>(pl.p[i][3], g_pts[i].P[0]); }, reps);
    double tpipe = timeIt([&](int i) {
        Plane<N> q[4];
        for (int k = 0; k < 4; k++) q[k] = planeFromPoints<N, false>(g_pts[i].P[3 * k], g_pts[i].P[3 * k + 1], g_pts[i].P[3 * k + 2]);
        sink += vertexSide<N, false>(q[0], q[1], q[2], q[3]);
    }, reps / 2);
    double tpipeT = timeIt([&](int i) {
        Plane<N> q[4];
        for (int k = 0; k < 4; k++) q[k] = planeFromPoints<N, true>(g_pts[i].P[3 * k], g_pts[i].P[3 * k + 1], g_pts[i].P[3 * k + 2]);
        sink += vertexSide<N, true>(q[0], q[1], q[2], q[3]);
    }, reps / 2);
    g_sink += sink;
    printf("%-5s vertex sign (4 planes given): full-width mul %9.1f ns, %6.0f limb MACs | mulMag %9.1f ns, %6.0f limb MACs\n", name, tv, macsNaive, tvT, macsTrim);
    printf("%-5s plane from 3 points:           full-width mul %9.1f ns, %6.0f limb MACs | mulMag %9.1f ns, %6.0f limb MACs\n", name, tp, macsPlaneNaive, tpT, macsPlaneTrim);
    printf("%-5s point-plane side:              full-width mul %9.1f ns, %6.0f limb MACs | mulMag %9.1f ns, %6.0f limb MACs\n", name, ts, macsPsNaive, tsT, macsPsTrim);
    printf("%-5s pipeline (12 points -> 4 planes -> vertex sign): full %9.1f ns | mulMag %9.1f ns\n", name, tpipe, tpipeT);
    printf("RESULT %s %.2f %.2f %.2f %.2f\n", name, tv, tvT, tpipe, tpipeT);
}

int main() {
    exactinit();
    Rng r(777);
    for (int i = 0; i < NC; i++) for (int p = 0; p < 12; p++) for (int k = 0; k < 3; k++) {
        int64_t x = (int64_t)(r.next() % (uint64_t)(2 * V + 1)) - V;
        g_pts[i].P[p][k] = x;
        g_pts[i].D[p][k] = (double)x;
    }
    // reference results by cpp_int; also times
    std::vector<int> ref(NC);
    std::vector<std::vector<Big>> bp(NC * 4, std::vector<Big>(4));
    for (int i = 0; i < NC; i++) for (int k = 0; k < 4; k++) {
        const int64_t *a = g_pts[i].P[3 * k], *b = g_pts[i].P[3 * k + 1], *c = g_pts[i].P[3 * k + 2];
        Big e1[3], e2[3];
        for (int j = 0; j < 3; j++) { e1[j] = Big(b[j]) - Big(a[j]); e2[j] = Big(c[j]) - Big(a[j]); }
        auto& pl = bp[i * 4 + k];
        pl[0] = e1[1] * e2[2] - e1[2] * e2[1]; pl[1] = e1[2] * e2[0] - e1[0] * e2[2]; pl[2] = e1[0] * e2[1] - e1[1] * e2[0];
        pl[3] = -(pl[0] * Big(a[0]) + pl[1] * Big(a[1]) + pl[2] * Big(a[2]));
    }
    for (int i = 0; i < NC; i++) ref[i] = bigVertexSide(bp[i * 4].data(), bp[i * 4 + 1].data(), bp[i * 4 + 2].data(), bp[i * 4 + 3].data());

    long sink = 0;
    double tBig = timeIt([&](int i) { sink += bigVertexSide(bp[i * 4].data(), bp[i * 4 + 1].data(), bp[i * 4 + 2].data(), bp[i * 4 + 3].data()); }, 4);
    double tBigPipe = timeIt([&](int i) {
        Big pl[4][4];
        for (int k = 0; k < 4; k++) {
            const int64_t *a = g_pts[i].P[3 * k], *b = g_pts[i].P[3 * k + 1], *c = g_pts[i].P[3 * k + 2];
            Big e1[3], e2[3];
            for (int j = 0; j < 3; j++) { e1[j] = Big(b[j]) - Big(a[j]); e2[j] = Big(c[j]) - Big(a[j]); }
            pl[k][0] = e1[1] * e2[2] - e1[2] * e2[1]; pl[k][1] = e1[2] * e2[0] - e1[0] * e2[2]; pl[k][2] = e1[0] * e2[1] - e1[1] * e2[0];
            pl[k][3] = -(pl[k][0] * Big(a[0]) + pl[k][1] * Big(a[1]) + pl[k][2] * Big(a[2]));
        }
        sink += bigVertexSide(pl[0], pl[1], pl[2], pl[3]);
    }, 2);
    // Shewchuk adaptive orient3d on points 0..3 of each case (doubles, exact at L=26)
    double tSh = timeIt([&](int i) { sink += orient3d(g_pts[i].D[0], g_pts[i].D[1], g_pts[i].D[2], g_pts[i].D[3]) > 0; }, 2000);
    // Shewchuk on the exact-fallback-heavy inputs for context: 4 exactly coplanar points (p3 = p0 + (p1-p0) + (p2-p0))
    static double cop[NC][4][3];
    for (int i = 0; i < NC; i++) for (int j = 0; j < 3; j++) {
        cop[i][0][j] = g_pts[i].D[0][j] / 4; cop[i][1][j] = g_pts[i].D[1][j] / 4; cop[i][2][j] = g_pts[i].D[2][j] / 4;
        cop[i][3][j] = cop[i][1][j] + cop[i][2][j] - cop[i][0][j];
    }
    double tShCop = timeIt([&](int i) { sink += orient3d(cop[i][0], cop[i][1], cop[i][2], cop[i][3]) > 0; }, 200);
    g_sink += sink;
    printf("Shewchuk adaptive orient3d, random L=26 points:            %9.2f ns/op\n", tSh);
    printf("Shewchuk adaptive orient3d, exactly coplanar (full path):  %9.2f ns/op   (context only; not the kill-criterion input)\n", tShCop);
    printf("cpp_int vertex sign (4 planes given, same 28-mul scheme):  %9.1f ns/op\n", tBig);
    printf("cpp_int pipeline (12 points -> planes -> vertex sign):     %9.1f ns/op\n", tBigPipe);
    Planes<8>* p8 = new Planes<8>; Planes<12>* p12 = new Planes<12>; Planes<16>* p16 = new Planes<16>;
    runWidth<8>(*p8, ref, "256");
    runWidth<12>(*p12, ref, "384");
    runWidth<16>(*p16, ref, "512");
    printf("(agreement: all fixed-width variants equal cpp_int on all %d cases)\n", NC);
    printf("SHEWCHUK %.2f\n", tSh);
    return 0;
}
