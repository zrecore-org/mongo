// SPDX-License-Identifier: SSPL-1.0
// MP14d step 2: the device path of $meshContains. The point-in-solid FILTER kernel of MP12c (exactmesh gpu/pip_bvh.comp: ray-parity through a BVH, soft binary64 predicates in
// 64-bit integers, verdict 0 OUT / 1 IN / 2 UNCERTAIN) run through the Vulkan runtime of vkctx.h. The tree is built on the CPU in the kernel's layout (internal nodes 0..n-2, leaf k = node
// n-1+k, outward-rounded binary32 boxes), not by the MP10 device build, so only one kernel is needed. The caller resolves every UNCERTAIN verdict with the exact CPU path:
// the device answer is only trusted when it is definite.
#pragma once
#include <atomic>
#include <chrono>
#include "meshvol_core.h"
#include "vkctx.h"
#include "pip_bvh_spv.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace gpupip {
// MP17 stage timers (microseconds, cumulative), read by the extension's config probe
struct Times { std::atomic<long long> rayBox{0}, allocCopy{0}, submitWait{0}, copyOut{0}; };
inline Times& times() { static Times t; return t; }
inline long long nowUs() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline float roundDown(double x) { float f = (float)x; if ((double)f > x) f = std::nextafterf(f, -INFINITY); return f; }
inline float roundUp(double x) { float f = (float)x; if ((double)f < x) f = std::nextafterf(f, INFINITY); return f; }

// The BVH in the kernel's layout. n >= 1 triangles; nb holds 2n-1 boxes of 6 floats; lc/rc are the children of internal nodes 0..n-2; idx maps leaf position -> triangle.
struct Tree { uint32_t n = 0; std::vector<float> nb; std::vector<uint32_t> lc, rc, idx; };
inline Tree buildTree(const std::vector<double>& v, const std::vector<uint32_t>& t) {
    Tree T; const size_t n = t.size() / 3; T.n = (uint32_t)n;
    std::vector<float> fb(6 * n);
    for (size_t k = 0; k < n; k++) for (int d = 0; d < 3; d++) {
        const double a = v[3 * (size_t)t[3 * k] + d], b = v[3 * (size_t)t[3 * k + 1] + d], c = v[3 * (size_t)t[3 * k + 2] + d];
        fb[6 * k + d] = roundDown(std::min(a, std::min(b, c))); fb[6 * k + 3 + d] = roundUp(std::max(a, std::max(b, c)));
    }
    T.idx.resize(n); for (size_t k = 0; k < n; k++) T.idx[k] = (uint32_t)k;
    T.nb.assign(6 * (2 * n - 1), 0.f); T.lc.assign(n ? n : 1, 0); T.rc.assign(n ? n : 1, 0);
    uint32_t nextInternal = 0;
    // iterative-free recursion (depth <= log2 n): returns the node id of the subtree over positions [b, e)
    struct Rec { Tree& T; const std::vector<float>& fb; uint32_t& next;
        uint32_t go(uint32_t b, uint32_t e) {
            const uint32_t n = T.n;
            if (e - b == 1) {   // leaf at position b = node n-1+b
                const uint32_t node = n - 1 + b; const uint32_t k = T.idx[b];
                for (int i = 0; i < 6; i++) T.nb[6 * (size_t)node + i] = fb[6 * (size_t)k + i];
                return node;
            }
            // median split on the longest centroid axis over the float boxes
            float lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
            for (uint32_t i = b; i < e; i++) for (int d = 0; d < 3; d++) { const float c = fb[6 * (size_t)T.idx[i] + d] + fb[6 * (size_t)T.idx[i] + 3 + d]; lo[d] = std::min(lo[d], c); hi[d] = std::max(hi[d], c); }
            int axis = 0; for (int d = 1; d < 3; d++) if (hi[d] - lo[d] > hi[axis] - lo[axis]) axis = d;
            const uint32_t mid = (b + e) / 2;
            std::nth_element(T.idx.begin() + b, T.idx.begin() + mid, T.idx.begin() + e, [&](uint32_t x, uint32_t y) {
                const float cx = fb[6 * (size_t)x + axis] + fb[6 * (size_t)x + 3 + axis], cy = fb[6 * (size_t)y + axis] + fb[6 * (size_t)y + 3 + axis];
                return cx != cy ? cx < cy : x < y; });
            const uint32_t me = next++;
            const uint32_t l = go(b, mid), r = go(mid, e);
            T.lc[me] = l; T.rc[me] = r;
            for (int d = 0; d < 3; d++) { T.nb[6 * (size_t)me + d] = std::min(T.nb[6 * (size_t)l + d], T.nb[6 * (size_t)r + d]); T.nb[6 * (size_t)me + 3 + d] = std::max(T.nb[6 * (size_t)l + 3 + d], T.nb[6 * (size_t)r + 3 + d]); }
            return me;
        } };
    Rec rec{T, fb, nextInternal};
    if (n) rec.go(0, (uint32_t)n);
    return T;
}

// The ray box of a query: lo = p rounded down, hi = (FLT_MAX, py, pz) rounded up.
inline void rayBox(const double* p, float* b) { for (int d = 0; d < 3; d++) { b[d] = roundDown(p[d]); b[3 + d] = d == 0 ? FLT_MAX : roundUp(p[d]); } }

// One process-wide device engine, created lazily on the first eligible batch and destroyed at extension shutdown. All use is serialised by `mutex`
// (a Vulkan queue is not thread-safe; get_next runs on mongod query threads).
struct Engine {
    std::mutex mutex;
    gpu::Ctx ctx; gpu::Kernel kernel;
    bool created = false, failed = false; std::string failure, deviceName;
    long runs = 0, leaked = 0;
    // Creates the device and the pipeline. Returns false (and remembers why) on failure; never throws.
    bool ensure() {
        if (created) return true;
        if (failed) return false;
        try {
            if (!ctx.init(gpu::defaultLoaderPaths())) throw std::runtime_error(ctx.error.empty() ? "device creation failed" : ctx.error);
            kernel = ctx.makeKernelCode(std::vector<uint32_t>(kPipBvhSpv, kPipBvhSpv + kPipBvhSpvWords), 11, 12);
            deviceName = ctx.props.deviceName; created = true; return true;
        } catch (const std::exception& e) { failed = true; failure = e.what(); try { ctx.destroy(); } catch (...) {} return false; }
        catch (...) { failed = true; failure = "unknown exception creating the device"; return false; }
    }
    // Classifies all points against the mesh with the device filter. out[i] in {0,1,2}. Returns false on any device failure or timeout (caller falls back).
    // plant (test-only, from the stage's `plant` knob, inert by default): "gpuCreateFail" makes the lazy device creation fail, "gpuOOM" asks the driver for an impossible allocation after the real ones,
    // "gpuTimeout" waits on the fence for 1 ns (the in-flight work is then still running: its buffers must NOT be freed).
    bool classify(const std::vector<double>& v, const std::vector<uint32_t>& t, const Tree& tree, const std::vector<double>& pts, std::vector<uint32_t>* out, uint64_t timeoutNs, std::string* err, const std::string& plant = std::string()) {
        try {
            if (plant == "gpuCreateFail" && !created) { failed = true; failure = "planted failure of the lazy device creation"; }
            if (!ensure()) { *err = failure; return false; }
            const size_t nq = pts.size() / 3, n = t.size() / 3;
            if (!nq || !n) { out->assign(nq, 0); return true; }
            const long long t0 = nowUs();
            std::vector<float> qb(6 * nq); for (size_t i = 0; i < nq; i++) rayBox(&pts[3 * i], &qb[6 * i]);
            times().rayBox += nowUs() - t0; const long long t1 = nowUs();
            gpu::Buf bV{}, bT{}, bQ{}, bQB{}, bNB{}, bL{}, bR{}, bI{}, bMQ{}, bMT{}, bO{}, bHuge{};
            bool submitted = false;   // once work has been submitted, a failure may leave it running on the device: then nothing is freed (leaked on purpose) and the engine is poisoned
            auto freeAll = [&] { for (gpu::Buf* b : {&bV, &bT, &bQ, &bQB, &bNB, &bL, &bR, &bI, &bMQ, &bMT, &bO}) if (b->size) ctx.release(*b); };
            try {
                bV = ctx.alloc(v.size() * 8 + 8); bT = ctx.alloc(t.size() * 4 + 4); bQ = ctx.alloc(nq * 24 + 8); bQB = ctx.alloc(nq * 24 + 8); bNB = ctx.alloc(tree.nb.size() * 4 + 4);
                bL = ctx.alloc(n * 4 + 4); bR = ctx.alloc(n * 4 + 4); bI = ctx.alloc(n * 4 + 4); bMQ = ctx.alloc(8); bMT = ctx.alloc(8); bO = ctx.alloc(nq * 4 + 4);
                std::memcpy(bV.ptr, v.data(), v.size() * 8); std::memcpy(bT.ptr, t.data(), t.size() * 4); std::memcpy(bQ.ptr, pts.data(), nq * 24); std::memcpy(bQB.ptr, qb.data(), nq * 24);
                std::memcpy(bNB.ptr, tree.nb.data(), tree.nb.size() * 4); std::memcpy(bL.ptr, tree.lc.data(), n * 4); std::memcpy(bR.ptr, tree.rc.data(), n * 4); std::memcpy(bI.ptr, tree.idx.data(), n * 4);
                if (plant == "gpuOOM") bHuge = ctx.alloc((size_t)1 << 46);   // the driver must refuse: out of device memory mid-batch
                const uint32_t pc[3] = {(uint32_t)nq, (uint32_t)n, 0u};
                times().allocCopy += nowUs() - t1;
                ctx.begin();
                for (gpu::Buf* b : {&bV, &bT, &bQ, &bQB, &bNB, &bL, &bR, &bI, &bMQ, &bMT}) ctx.upload(*b);
                ctx.dispatch(kernel, {&bV, &bT, &bQ, &bQB, &bNB, &bL, &bR, &bI, &bMQ, &bMT, &bO}, pc, (uint32_t)((nq + 63) / 64));
                ctx.download(bO);
                submitted = true;
                const long long t2 = nowUs();
                ctx.submitWait(plant == "gpuTimeout" ? 1ull : timeoutNs);   // finite: a hung device must not make the stage unkillable
                const long long t3 = nowUs(); times().submitWait += t3 - t2;
                out->assign((uint32_t*)bO.ptr, (uint32_t*)bO.ptr + nq);
                times().copyOut += nowUs() - t3;
            } catch (...) { if (!submitted) freeAll(); else leaked++; throw; }   // in-flight work may still read these buffers: never free them after a failed wait
            freeAll(); runs++; return true;
        } catch (const std::exception& e) { *err = e.what(); failed = true; failure = *err; return false; }   // a failed submission poisons the context: do not reuse it
        catch (...) { *err = "unknown exception in the device path"; failed = true; failure = *err; return false; }
    }
    void shutdown() { std::lock_guard<std::mutex> g(mutex); if (created) { try { ctx.release(kernel); ctx.destroy(); } catch (...) {} created = false; } }
};
inline Engine& engine() { static Engine e; return e; }
}  // namespace gpupip
