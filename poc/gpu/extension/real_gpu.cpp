// SPDX-License-Identifier: SSPL-1.0
// MP14d bar 2 on the MP12e corpus: the extension's DEVICE path (CPU-built tree in the kernel layout, MP12c filter kernel, UNCERTAIN resolved by the exact CPU path) versus the exactmesh oracle,
// 60 real models, 540,000 queries. Prints per-model counts of definite and uncertain device verdicts, false definite (device verdict contradicting the oracle), final disagreements, and the
// resolved-verdict FNV-1a hash (reference ae59d9976297a67b). Run under the GPU lock. Usage: real_gpu <list>
#include "meshcontains_core.h"
#include "gpu_pip.h"
#include "cpu/pip_hardened.h"
#include "cpu/bvh_ref.h"
#include "gpu/pip_cases.h"
#include "gpu/stl_read.h"
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <thread>
extern "C" void exactinit(void);
int main(int argc, char** argv) {
    exactinit();
    if (argc < 2) return 2;
    std::ifstream in(argv[1]); std::string path;
    long total = 0, bad = 0, unc = 0, falseDef = 0, dev = 0, on = 0; uint64_t h = 1469598103934665603ull; int models = 0; double devMs = 0, cpuMs = 0;
    while (std::getline(in, path)) {
        if (path.empty()) continue;
        Case c; size_t deg = 0;
        if (!stlr::readStl(path, c.m, &deg)) { std::printf("unreadable: %s\n", path.c_str()); return 1; }
        size_t sl = path.find_last_of('/'), dt = path.find_last_of('.');
        const std::string id = path.substr(sl == std::string::npos ? 0 : sl + 1, dt - (sl == std::string::npos ? 0 : sl + 1));
        uint64_t sd = 1469598103934665603ull; for (char ch : id) { sd ^= (uint8_t)ch; sd *= 1099511628211ull; }
        Rng r(sd); addClasses(c, 0, r, 0, 0, 0, 0);
        std::vector<double> q; for (auto& k : c.classes) q.insert(q.end(), k.q.begin(), k.q.end());
        const size_t n = q.size() / 3;
        // oracle
        std::vector<uint8_t> ref(n); { auto boxes = bvhref::triangleBoxes(c.m); bvhref::Bvh rb; rb.build(boxes); std::vector<uint32_t> cand; Mesh sub; sub.v = c.m.v;
            for (size_t i = 0; i < n; i++) { const double* p = &q[3*i]; bvhref::Box b; b.lo[0] = p[0]; b.lo[1] = p[1]; b.lo[2] = p[2]; b.hi[0] = DBL_MAX; b.hi[1] = p[1]; b.hi[2] = p[2];
                cand.clear(); rb.query(b, cand); sub.t.clear(); for (uint32_t k : cand) { sub.t.push_back(c.m.t[3*(size_t)k]); sub.t.push_back(c.m.t[3*(size_t)k+1]); sub.t.push_back(c.m.t[3*(size_t)k+2]); }
                mesh::Pip o = mesh::classifyPointHardened(sub, p); ref[i] = o.boundary ? 2 : (o.inside ? 1 : 0); } }
        // device path
        const double t0 = gpu::nowMs();
        std::vector<uint32_t> dv; std::string err;
        gpupip::Tree tree = gpupip::buildTree(c.m.v, c.m.t);
        { std::lock_guard<std::mutex> lk(gpupip::engine().mutex);
          if (!gpupip::engine().classify(c.m.v, c.m.t, tree, q, &dv, 60'000'000'000ull, &err)) { std::printf("device failure: %s\n", err.c_str()); return 3; } }
        const double t1 = gpu::nowMs();
        meshcontains::Bvh bvh; bvh.build(c.m.v, c.m.t); std::vector<uint32_t> sc; std::vector<uint8_t> fin(n); long mu = 0, mf = 0;
        for (size_t i = 0; i < n; i++) {
            if (dv[i] == 2) { fin[i] = meshcontains::classifyBvh(c.m.v, c.m.t, bvh, &q[3*i], &sc); mu++; }
            else { fin[i] = (uint8_t)dv[i]; if (ref[i] == 2 || dv[i] != ref[i]) mf++; }   // a definite verdict on a boundary point, or one that contradicts the oracle
        }
        const double t2 = gpu::nowMs(); devMs += t1 - t0; cpuMs += t2 - t1;
        long mb = 0; for (size_t i = 0; i < n; i++) { mb += fin[i] != ref[i]; h ^= ref[i]; h *= 1099511628211ull; on += ref[i] == 2; }
        total += (long)n; bad += mb; unc += mu; falseDef += mf; dev += (long)n - mu; models++;
        std::printf("%-8s tris=%-7zu queries=%-6zu uncertain=%-6ld falseDefinite=%ld disagreements=%ld\n", id.c_str(), c.m.nt(), n, mu, mf, mb); std::fflush(stdout);
    }
    std::printf("TOTAL models=%d queries=%ld definite-on-device=%ld uncertain=%ld falseDefinite=%ld ON=%ld disagreements=%ld resolved-verdict hash=%016llx device+prep %.0f ms, resolve %.0f ms\n",
                models, total, dev, unc, falseDef, on, bad, (unsigned long long)h, devMs, cpuMs);
    return bad == 0 && falseDef == 0 ? 0 : 1;
}
