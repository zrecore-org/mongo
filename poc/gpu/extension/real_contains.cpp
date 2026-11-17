// MP14d bar 1 on the MP12e corpus: the extension's CPU path (meshcontains::classifyBvh, own BVH) versus the exactmesh oracle (cpu/pip_hardened.h, Shewchuk
// plus big-integer fallback, candidates from exactmesh's independent bvhref BVH) on real models, with the MP12c query classes seeded from the model id.
// Usage: real_contains <list of STL paths, one per line> [threads]. Prints per-model disagreements, totals, the resolved-verdict FNV-1a hash and the
// ON-surface count (MP12c reported 68436 exact boundary queries on this list). Exit 0 only with 0 disagreements.
#include "meshcontains_core.h"
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
    if (argc < 2) { std::printf("usage: real_contains <list> [threads]\n"); return 2; }
    unsigned T = argc > 2 ? (unsigned)atoi(argv[2]) : std::max(1u, std::thread::hardware_concurrency() / 2);
    const std::string dump = argc > 3 ? argv[3] : "";   // optional: write <id>.mesh (packed MSH1), <id>.pts (n*3 binary64 LE) and <id>.exp (one char O/I/B per point) for the mongod test
    std::ifstream in(argv[1]); std::string path;
    long total = 0, bad = 0, on = 0, nin = 0, nout = 0; uint64_t h = 1469598103934665603ull; int models = 0;
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
        std::vector<uint8_t> mine(n), ref(n);
        meshcontains::Bvh bvh; bvh.build(c.m.v, c.m.t);
        auto boxes = bvhref::triangleBoxes(c.m); bvhref::Bvh rb; rb.build(boxes);
        std::vector<std::thread> th;
        for (unsigned w = 0; w < T; w++) th.emplace_back([&, w] {
            std::vector<uint32_t> scratch, cand; Mesh sub; sub.v = c.m.v;
            for (size_t i = n * w / T; i < n * (w + 1) / T; i++) {
                const double* p = &q[3 * i];
                mine[i] = meshcontains::classifyBvh(c.m.v, c.m.t, bvh, p, &scratch);
                bvhref::Box b; b.lo[0] = p[0]; b.lo[1] = p[1]; b.lo[2] = p[2]; b.hi[0] = DBL_MAX; b.hi[1] = p[1]; b.hi[2] = p[2];
                cand.clear(); rb.query(b, cand); sub.t.clear();
                for (uint32_t k : cand) { sub.t.push_back(c.m.t[3 * (size_t)k]); sub.t.push_back(c.m.t[3 * (size_t)k + 1]); sub.t.push_back(c.m.t[3 * (size_t)k + 2]); }
                mesh::Pip o = mesh::classifyPointHardened(sub, p);
                ref[i] = o.boundary ? 2 : (o.inside ? 1 : 0);
            }
        });
        for (auto& t : th) t.join();
        if (!dump.empty()) {
            FILE* f = fopen((dump + "/" + id + ".mesh").c_str(), "wb"); mesh::writeRecord(f, c.m); fclose(f);
            f = fopen((dump + "/" + id + ".pts").c_str(), "wb"); fwrite(q.data(), 8, q.size(), f); fclose(f);
            f = fopen((dump + "/" + id + ".exp").c_str(), "wb"); for (size_t i = 0; i < n; i++) fputc(ref[i] == 2 ? 'B' : ref[i] == 1 ? 'I' : 'O', f); fclose(f);
        }
        long mb = 0;
        for (size_t i = 0; i < n; i++) { mb += mine[i] != ref[i]; h ^= ref[i]; h *= 1099511628211ull; (ref[i] == 2 ? on : ref[i] == 1 ? nin : nout)++; }
        total += (long)n; bad += mb; models++;
        std::printf("%-8s tris=%-7zu queries=%-6zu disagreements=%ld\n", id.c_str(), c.m.nt(), n, mb); std::fflush(stdout);
    }
    std::printf("TOTAL models=%d queries=%ld (OUT %ld, IN %ld, ON %ld) disagreements=%ld resolved-verdict hash=%016llx\n", models, total, nout, nin, on, bad, (unsigned long long)h);
#ifdef MESHCONTAINS_RATIO
    {   // MP17: fraction of the double filter's in-window determinants that an fp32 filter (error factor u*(7+56u) with u = 2^-24, plus a slack factor for the rounding of the inputs) would still resolve
        const auto& R = meshcontains::g_ratio;
        auto cum = [](const long* h, int K) { long tot = 0, ok = 0; for (int b = 0; b < 65; b++) { tot += h[b]; if (b <= K) ok += h[b]; } return tot ? (double)ok / (double)tot : 0.0; };
        long t3 = 0, t2 = 0; for (int b = 0; b < 65; b++) { t3 += R.o3[b]; t2 += R.o2[b]; }
        std::printf("RATIO triangle steps=%ld per query=%.1f; orient3d calls=%ld orient2d calls=%ld (threads must be 1)\n", R.steps, total ? (double)R.steps / (double)total : 0.0, t3, t2);
        // orient3d: pass iff ratio > B, B = 4.17e-7 * slack; bucket b = floor(-log2 ratio), ratio > 2^-(b+1): pass when b <= K, K = floor(-log2 B) - 1
        for (const auto& kv : std::vector<std::pair<const char*, int>>{{"double (B=7.8e-16)", 49}, {"fp32 B=4.2e-7 (no input slack)", 20}, {"fp32 x8 slack", 17}, {"fp32 x64 slack", 14}, {"fp32 x1024 slack", 10}})
            std::printf("RATIO orient3d resolved by %-32s %.4f   orient2d %.4f\n", kv.first, cum(R.o3, kv.second), cum(R.o2, kv.second + 1));
    }
#endif
    return bad == 0 ? 0 : 1;
}
