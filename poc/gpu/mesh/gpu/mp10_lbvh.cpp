// MP10: deterministic GPU LBVH and box-pair emission (see PLAN.md, section MP10, for the registered bars).
//   mp10_lbvh device [scale]       run all classes on the Vulkan device against the exact CPU reference on the same binary32 boxes
//   mp10_lbvh mesh <primitives-dir> superset check on real meshes (binary64 boxes rounded outward) 
// Environment: MP10_MUT=1|2|3 loads the planted-mutant shaders. GPU_POC_CORRECTNESS_ONLY=1 shrinks the sizes (CPU Vulkan drivers).
#include "vkctx.h"
#include "../cpu/bvh_ref.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <random>
using bvhref::Pair;
using bvhref::Box;

struct FBox { float lo[3], hi[3]; };
static float roundDown(double x) { float f = (float)x; if ((double)f > x) f = nextafterf(f, -INFINITY); return f; }
static float roundUp(double x) { float f = (float)x; if ((double)f < x) f = nextafterf(f, INFINITY); return f; }
static FBox outward(const Box& b) { FBox f; for (int k = 0; k < 3; k++) { f.lo[k] = roundDown(b.lo[k]); f.hi[k] = roundUp(b.hi[k]); } return f; }
static Box widen(const FBox& f) { Box b; for (int k = 0; k < 3; k++) { b.lo[k] = f.lo[k]; b.hi[k] = f.hi[k]; } return b; }

struct Stats { double gpuMs = 0, totalMs = 0; bool overflow = false, stackErr = false; size_t pairs = 0; };

struct Lbvh {
    gpu::Ctx ctx;
    gpu::Kernel kMorton, kc, ks, kt, kBuild, kFit, kQuery;
    gpu::Buf bB, bA, keyA, keyB, idxA, idxB, bh, lc, rc, par, nb, flag, cnt, outp, err;
    size_t maxA = 0, maxB = 0, cap = 0;
    bool init(size_t mA, size_t mB, size_t capPairs, int mut) {
        if (!ctx.init(gpu::defaultLoaderPaths())) return false;
        std::string sfx = mut ? "_m" + std::to_string(mut) : "";
        std::string d = "build/shaders/";
        kMorton = ctx.makeKernel(d + "lbvh_morton.spv", 2, 20);
        kc = ctx.makeKernel(d + "sort_count.spv", 2, 12); ks = ctx.makeKernel(d + "sort_scan.spv", 1, 12); kt = ctx.makeKernel(d + "sort_scatter.spv", 5, 12);
        kBuild = ctx.makeKernel(d + (mut == 2 ? "lbvh_build_m2.spv" : "lbvh_build.spv"), 4, 4);
        kFit = ctx.makeKernel(d + "lbvh_fit.spv", 7, 4);
        kQuery = ctx.makeKernel(d + (mut == 1 ? "lbvh_query_m1.spv" : mut == 3 ? "lbvh_query_m3.spv" : "lbvh_query.spv"), 8, 16);
        maxA = mA; maxB = mB; cap = capPairs;
        bB = ctx.alloc(mB * 24); bA = ctx.alloc(mA * 24);
        keyA = ctx.alloc(mB * 8); keyB = ctx.alloc(mB * 8); idxA = ctx.alloc(mB * 4); idxB = ctx.alloc(mB * 4);
        bh = ctx.alloc(16 * ((mB + 4095) / 4096) * 4 + 16);
        lc = ctx.alloc(mB * 4 + 4); rc = ctx.alloc(mB * 4 + 4); par = ctx.alloc(2 * mB * 4 + 4);
        nb = ctx.alloc((2 * mB) * 24); flag = ctx.alloc(mB * 4 + 4); cnt = ctx.alloc(mA * 4 + 4);
        outp = ctx.alloc(capPairs * 8 + 8); err = ctx.alloc(16);
        return true;
    }
    // Returns the sorted pair list (query index i in A, box index j in B). On pair-cap overflow, st.overflow is set and the list is empty.
    std::vector<Pair> run(const std::vector<FBox>& A, const std::vector<FBox>& B, Stats& st) {
        double t0 = gpu::nowMs();
        size_t nA = A.size(), nB = B.size(); st = Stats();
        if (nA == 0 || nB == 0) return {};
        memcpy(bB.ptr, B.data(), nB * 24); memcpy(bA.ptr, A.data(), nA * 24);
        float lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
        for (auto& b : B) for (int k = 0; k < 3; k++) { float c = (b.lo[k] + b.hi[k]) * 0.5f; lo[k] = std::min(lo[k], c); hi[k] = std::max(hi[k], c); }
        float ext = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}); float scale = ext > 0 && std::isfinite(ext) ? 1023.0f / ext : 1.0f;
        struct { uint32_t n; float ox, oy, oz, scale; } pm = {(uint32_t)nB, lo[0], lo[1], lo[2], scale};
        memset(flag.ptr, 0, nB * 4 + 4); memset(err.ptr, 0, 16);
        uint32_t Bk = (uint32_t)((nB + 4095) / 4096);
        gpu::Buf *ki = &keyA, *ii = &idxA, *ko = &keyB, *io = &idxB;
        ctx.begin();
        ctx.upload(bB, nB * 24); ctx.upload(bA, nA * 24); ctx.upload(flag, nB * 4 + 4); ctx.upload(err, 16);
        ctx.dispatch(kMorton, {&bB, &keyA}, &pm, (uint32_t)((nB + 63) / 64));
        for (uint32_t p = 0; p < 8; p++) {    // 32-bit key: eight 4-bit passes; stable
            uint32_t pc[3] = {(uint32_t)nB, Bk, p};
            ctx.dispatch(kc, {ki, &bh}, pc, Bk); ctx.dispatch(ks, {&bh}, pc, 1); ctx.dispatch(kt, {ki, ii, ko, io, &bh}, pc, Bk);
            std::swap(ki, ko); std::swap(ii, io);
        }
        // after 8 passes the sorted keys and permutation are back in keyA / idxA
        int n = (int)nB;
        if (nB > 1) ctx.dispatch(kBuild, {&keyA, &lc, &rc, &par}, &n, (uint32_t)((nB + 63) / 64));
        uint32_t nn = (uint32_t)nB;
        ctx.dispatch(kFit, {&bB, &idxA, &lc, &rc, &par, &nb, &flag}, &nn, (uint32_t)((nB + 63) / 64));
        uint32_t pq[4] = {(uint32_t)nA, (uint32_t)nB, 0u, (uint32_t)cap};
        ctx.dispatch(kQuery, {&bA, &nb, &lc, &rc, &idxA, &cnt, &outp, &err}, pq, (uint32_t)((nA + 63) / 64));
        ctx.download(cnt, nA * 4); ctx.download(err, 16);
        double g1 = ctx.submitWait();
        const uint32_t* c = (const uint32_t*)cnt.ptr; const uint32_t* e = (const uint32_t*)err.ptr;
        if (e[0]) st.stackErr = true;
        std::vector<uint64_t> off(nA + 1, 0);
        for (size_t q = 0; q < nA; q++) off[q + 1] = off[q] + c[q];
        st.pairs = (size_t)off[nA];
        if (st.pairs > cap) { st.overflow = true; st.gpuMs = g1; st.totalMs = gpu::nowMs() - t0; return {}; }
        std::vector<uint32_t> offs(nA); for (size_t q = 0; q < nA; q++) offs[q] = (uint32_t)off[q];
        memcpy(cnt.ptr, offs.data(), nA * 4);
        ctx.begin(); ctx.upload(cnt, nA * 4);
        pq[2] = 1u;
        ctx.dispatch(kQuery, {&bA, &nb, &lc, &rc, &idxA, &cnt, &outp, &err}, pq, (uint32_t)((nA + 63) / 64));
        ctx.download(outp, st.pairs * 8 ? st.pairs * 8 : 8);
        double g2 = ctx.submitWait();
        const Pair* o = (const Pair*)outp.ptr;
        std::vector<Pair> res(o, o + st.pairs);
        // canonical order: queries are already in index order; sort each query's hits by B index (the tree shape never reaches the output)
        for (size_t q = 0; q < nA; q++) std::sort(res.begin() + off[q], res.begin() + off[q + 1]);
        st.gpuMs = g1 + g2; st.totalMs = gpu::nowMs() - t0;
        return res;
    }
    void shutdown() {
        for (auto* b : {&bB, &bA, &keyA, &keyB, &idxA, &idxB, &bh, &lc, &rc, &par, &nb, &flag, &cnt, &outp, &err}) ctx.release(*b);
        for (auto* k : {&kMorton, &kc, &ks, &kt, &kBuild, &kFit, &kQuery}) ctx.release(*k);
        ctx.destroy();
    }
};

// ---- exact CPU reference on the same binary32 boxes (widened to binary64 exactly) ----
static std::vector<Pair> reference(const std::vector<FBox>& A, const std::vector<FBox>& B, double* ms = nullptr, unsigned threads = 1) {
    std::vector<Box> a, b; for (auto& x : A) a.push_back(widen(x)); for (auto& x : B) b.push_back(widen(x));
    double t0 = gpu::nowMs();
    auto r = (double)A.size() * (double)B.size() <= 4e8 ? bvhref::bruteForcePairs(a, b) : bvhref::overlapPairs(a, b, threads);
    if (ms) *ms = gpu::nowMs() - t0;
    return r;
}

struct Rng {
    std::mt19937_64 g; explicit Rng(uint64_t s) : g(s) {}
    double u01() { return (double)(g() >> 11) / 9007199254740992.0; }
    double range(double a, double b) { double u = u01(); return a + (b - a) * u; }   // one draw per statement: argument order is unspecified
};
static FBox mk(double cx, double cy, double cz, double hx, double hy, double hz) {
    Box b; b.lo[0] = cx - hx; b.lo[1] = cy - hy; b.lo[2] = cz - hz; b.hi[0] = cx + hx; b.hi[1] = cy + hy; b.hi[2] = cz + hz; return outward(b);
}
enum { kUniform, kClustered, kIdentical, kTouching, kPoints, kHuge, kBatched, kLarge, kNumCls };
static const char* kName[] = {"uniform random", "clustered", "all identical (equal Morton codes)", "touching lattice (faces, edges, corners)", "zero-extent points + duplicates", "few huge boxes among small", "many small meshes batched (1000 x 100)", "few large (1e6 x 1e6)"};

static void makeSet(int cls, double scale, Rng& r, std::vector<FBox>& A, std::vector<FBox>& B) {
    A.clear(); B.clear();
    auto uniform = [&](std::vector<FBox>& out, size_t n, double V) {
        double h = std::cbrt(5.0 * V / (64.0 * std::max<size_t>(n, 1))) * 1.0;
        for (size_t i = 0; i < n; i++) { double x = r.range(0, 100), y = r.range(0, 100), z = r.range(0, 100); double hx = r.range(0, h), hy = r.range(0, h), hz = r.range(0, h); out.push_back(mk(x, y, z, hx, hy, hz)); }
    };
    switch (cls) {
        case kUniform: { size_t n = (size_t)(100000 * scale); uniform(A, n, 1e6); uniform(B, n, 1e6); break; }
        case kClustered: {
            size_t n = (size_t)(60000 * scale); double cx[20], cy[20], cz[20];
            for (int c = 0; c < 20; c++) { cx[c] = r.range(0, 100); cy[c] = r.range(0, 100); cz[c] = r.range(0, 100); }
            for (int s = 0; s < 2; s++) for (size_t i = 0; i < n; i++) {
                int c = (int)(r.g() % 20); double dx = r.range(-1, 1), dy = r.range(-1, 1), dz = r.range(-1, 1); double hh = r.range(0, 0.05);
                (s ? B : A).push_back(mk(cx[c] + dx, cy[c] + dy, cz[c] + dz, hh, hh, hh));
            }
            break; }
        case kIdentical: { size_t n = (size_t)(1500 * std::min(scale, 1.0)); for (size_t i = 0; i < n; i++) { A.push_back(mk(0.5, 0.5, 0.5, 0.5, 0.5, 0.5)); B.push_back(mk(0.5, 0.5, 0.5, 0.5, 0.5, 0.5)); } break; }
        case kTouching: {
            int g = 16; for (int i = 0; i < g; i++) for (int j = 0; j < g; j++) for (int k = 0; k < g; k++) A.push_back(mk(i + 0.5, j + 0.5, k + 0.5, 0.5, 0.5, 0.5));
            for (int i = 0; i < g; i++) for (int j = 0; j < g; j++) for (int k = 0; k < g; k++) { int sx = (int)(r.g() % 3) - 1, sy = (int)(r.g() % 3) - 1, sz = (int)(r.g() % 3) - 1; B.push_back(mk(i + 0.5 + sx, j + 0.5 + sy, k + 0.5 + sz, 0.5, 0.5, 0.5)); }
            break; }
        case kPoints: { size_t n = (size_t)(20000 * scale); for (int s = 0; s < 2; s++) for (size_t i = 0; i < n; i++) { double x = (double)(r.g() % 40), y = (double)(r.g() % 40), z = (double)(r.g() % 40); (s ? B : A).push_back(mk(x, y, z, 0, 0, 0)); } break; }
        case kHuge: { size_t n = (size_t)(50000 * scale); uniform(A, n, 1e6); uniform(B, n, 1e6); for (int i = 0; i < 10; i++) { B.push_back(mk(50, 50, 50, 1e6, 1e6, 1e6)); A.push_back(mk(50, 50, 50, 1e6, 1e6, 1e6)); } break; }
        case kBatched: {
            for (int m = 0; m < 1000; m++) for (int s = 0; s < 2; s++) for (int i = 0; i < 100; i++) {
                double x = r.range(0, 5), y = r.range(0, 5), z = r.range(0, 5); double hh = r.range(0, 0.3);
                (s ? B : A).push_back(mk(x + 10.0 * m, y, z, hh, hh, hh));
            }
            break; }
        case kLarge: { size_t n = (size_t)(1000000 * scale); uniform(A, n, 1e6); uniform(B, n, 1e6); break; }
    }
}

static uint64_t fnv(const std::vector<Pair>& v) { uint64_t h = 1469598103934665603ull; for (auto& p : v) { h ^= p.i; h *= 1099511628211ull; h ^= p.j; h *= 1099511628211ull; } return h; }

static int deviceMode(double scale) {
    int mut = getenv("MP10_MUT") ? atoi(getenv("MP10_MUT")) : 0;
    const bool small = getenv("GPU_POC_CORRECTNESS_ONLY") != nullptr; if (small) scale = std::min(scale, 0.1);
    size_t maxN = (size_t)(1000000 * std::max(scale, 0.1)) + 2000, cap = 64u << 20;
    Lbvh L;
    if (!L.init(maxN, maxN, cap, mut)) { printf("no GPU: %s\n", L.ctx.error.c_str()); return 2; }
    printf("device=%s memory=%s mutant=%d scale=%g cap=%zu pairs\n", L.ctx.props.deviceName, L.ctx.memMode(), mut, scale, cap);
    int bad = 0; double cpu1Total = 0, gpuTotal = 0;
    printf("%-44s %9s %9s %10s | %9s %9s %9s | result\n", "class", "nA", "nB", "pairs", "gpu ms", "cpu1 ms", "cpu8 ms");
    for (int cls = 0; cls < kNumCls; cls++) {
        Rng r(0xB10Cull * (cls + 1)); std::vector<FBox> A, B; makeSet(cls, scale, r, A, B);
        Stats st; auto got = L.run(A, B, st); double c1 = 0, c8 = 0; auto ref = reference(A, B, &c1, 1); reference(A, B, &c8, 8);
        bool ok = !st.overflow && !st.stackErr && got.size() == ref.size() && std::equal(got.begin(), got.end(), ref.begin());
        // timing run (second run, warm)
        Stats st2; L.run(A, B, st2);
        printf("%-44s %9zu %9zu %10zu | %9.2f %9.2f %9.2f | %s%s%s\n", kName[cls], A.size(), B.size(), ref.size(), st2.totalMs, c1, c8, ok ? "identical" : "DIFFERENT", st.overflow ? " (overflow)" : "", st.stackErr ? " (stack error)" : "");
        if (!ok) { bad++; printf("   got %zu pairs, reference %zu\n", got.size(), ref.size()); }
        cpu1Total += c1; gpuTotal += st2.totalMs;
    }
    // small sizes, including n = 1, 2, 3
    for (size_t nB : {1ul, 2ul, 3ul, 4ul, 5ul, 33ul}) {
        Rng r(77 + nB); std::vector<FBox> A, B; for (size_t i = 0; i < 40; i++) { double x = r.range(0, 4), y = r.range(0, 4), z = r.range(0, 4); A.push_back(mk(x, y, z, 1, 1, 1)); }
        for (size_t i = 0; i < nB; i++) { double x = r.range(0, 4), y = r.range(0, 4), z = r.range(0, 4); B.push_back(mk(x, y, z, 1, 1, 1)); }
        Stats st; auto got = L.run(A, B, st); auto ref = reference(A, B);
        bool ok = got.size() == ref.size() && std::equal(got.begin(), got.end(), ref.begin());
        printf("small nB=%zu: %zu pairs %s\n", nB, ref.size(), ok ? "identical" : "DIFFERENT"); if (!ok) bad++;
    }
    // determinism: 20 repeats of one class, byte-identical lists
    { Rng r(5); std::vector<FBox> A, B; makeSet(kUniform, std::min(scale, 1.0), r, A, B); uint64_t h0 = 0; bool same = true; Stats st;
      for (int rep = 0; rep < 20; rep++) { auto g = L.run(A, B, st); uint64_t h = fnv(g); if (rep == 0) h0 = h; else same = same && h == h0; }
      printf("determinism: 20 repeats %s (hash %016llx)\n", same ? "byte-identical" : "DIFFER", (unsigned long long)h0); if (!same) bad++; }
    // overflow: a cap smaller than the pair count must set the flag; the host fallback equals the reference
    { Rng r(9); std::vector<FBox> A, B; makeSet(kClustered, 0.2, r, A, B); size_t save = L.cap; L.cap = 1000; Stats st; auto g = L.run(A, B, st); L.cap = save;
      auto ref = reference(A, B); bool ok = st.overflow && g.empty(); printf("overflow: flag %s, counted %zu pairs (fallback reference %zu pairs) %s\n", st.overflow ? "set" : "NOT SET", st.pairs, ref.size(), ok && st.pairs == ref.size() ? "ok" : "WRONG"); if (!ok || st.pairs != ref.size()) bad++; }
    printf("summary: %d class(es) differ; total gpu %.1f ms vs cpu 1 thread %.1f ms over the classes (reported)\n%s\n", bad, gpuTotal, cpu1Total, bad ? "FAIL" : "PASS");
    L.shutdown();
    return bad ? 3 : 0;
}

// Superset check on real meshes: binary64 boxes rounded outward must give every pair the exact binary64 box test finds.
static int meshMode(const char* dir) {
    Lbvh L; if (!L.init(300000, 300000, 32u << 20, 0)) { printf("no GPU: %s\n", L.ctx.error.c_str()); return 2; }
    DIR* d = opendir(dir); if (!d) { printf("cannot open %s\n", dir); return 4; }
    std::vector<std::string> files; while (auto* e = readdir(d)) { std::string n = e->d_name; if (n.size() > 4 && n.substr(n.size() - 4) == ".msh") files.push_back(std::string(dir) + "/" + n); }
    closedir(d); std::sort(files.begin(), files.end());
    int bad = 0; size_t totExact = 0, totGpu = 0;
    for (auto& f : files) {
        mesh::Mesh m; if (!mesh::readMesh(f, m) || m.nt() == 0 || m.nt() > 250000) continue;
        for (int rep = 0; rep < 3; rep++) {
            Rng r(1234 + rep); mesh::Mesh t = m; double ox = r.range(-0.3, 0.3), oy = r.range(-0.3, 0.3), oz = r.range(-0.3, 0.3);
            for (size_t i = 0; i < t.v.size(); i += 3) { t.v[i] += ox; t.v[i + 1] += oy; t.v[i + 2] += oz; }
            auto a = bvhref::triangleBoxes(m), b = bvhref::triangleBoxes(t);
            auto exact = bvhref::overlapPairs(a, b, 1);
            std::vector<FBox> fa, fb; for (auto& x : a) fa.push_back(outward(x)); for (auto& x : b) fb.push_back(outward(x));
            Stats st; auto got = L.run(fa, fb, st);
            auto want = reference(fa, fb);
            bool same = !st.overflow && got.size() == want.size() && std::equal(got.begin(), got.end(), want.begin());
            bool sup = std::includes(got.begin(), got.end(), exact.begin(), exact.end());
            totExact += exact.size(); totGpu += got.size();
            printf("%-60s rep %d: exact binary64 pairs %9zu, gpu pairs %9zu (+%.3f%%), gpu==binary32 reference %s, superset %s\n", f.substr(f.rfind('/') + 1).c_str(), rep, exact.size(), got.size(), exact.empty() ? 0.0 : 100.0 * (double)(got.size() - exact.size()) / exact.size(), same ? "yes" : "NO", sup ? "yes" : "NO");
            if (!same || !sup) bad++;
        }
    }
    printf("summary: %d failing case(s); gpu pairs %zu vs exact %zu (false positives %.4f%%)\n%s\n", bad, totGpu, totExact, totExact ? 100.0 * (double)(totGpu - totExact) / totExact : 0.0, bad ? "FAIL" : "PASS");
    L.shutdown(); return bad ? 3 : 0;
}

int main(int argc, char** argv) {
    std::string m = argc > 1 ? argv[1] : "device";
    if (m == "mesh") return meshMode(argc > 2 ? argv[2] : "../../storage/mesh/corpus_large/primitives");
    return deviceMode(argc > 2 ? atof(argv[2]) : 1.0);
}
