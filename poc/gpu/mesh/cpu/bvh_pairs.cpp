// MP05 CPU BVH pair finding (see bvh_ref.h) and bench: bvh_pairs_bench <A.msh> <B.msh> [--translate dx dy dz] | bvh_pairs_bench --set <file.mshc>
// Build: bash build_cpu.sh  (c++ -std=c++17 -O2 -ffp-contract=off -pthread)
#include "bvh_ref.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace bvhref {
std::vector<Pair> overlapPairs(const std::vector<Box>& a, const std::vector<Box>& b, unsigned threads, double* bs, double* qs) {
    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();
    Bvh bvh; bvh.build(b);
    auto t1 = clk::now();
    if (threads < 1) threads = 1;
    size_t nchunks = (a.size() + kChunk - 1) / kChunk;
    std::vector<std::vector<Pair>> slots(nchunks);
    std::atomic<size_t> next{0};
    auto work = [&]() {
        std::vector<uint32_t> js;
        for (;;) {
            size_t c = next.fetch_add(1);
            if (c >= nchunks) break;
            size_t lo = c * kChunk, hi = std::min(a.size(), lo + kChunk);
            for (size_t i = lo; i < hi; i++) {
                js.clear(); bvh.query(a[i], js);
                for (uint32_t j : js) slots[c].push_back({(uint32_t)i, j});
            }
        }
    };
    if (threads == 1) work();
    else { std::vector<std::thread> th; for (unsigned t = 0; t < threads; t++) th.emplace_back(work); for (auto& t : th) t.join(); }
    size_t total = 0; for (auto& s : slots) total += s.size();
    std::vector<Pair> out; out.reserve(total);
    for (auto& s : slots) { out.insert(out.end(), s.begin(), s.end()); std::vector<Pair>().swap(s); }   // fixed chunk order
    auto t2 = clk::now();
    if (bs) *bs = std::chrono::duration<double>(t1 - t0).count();
    if (qs) *qs = std::chrono::duration<double>(t2 - t1).count();
    return out;
}
std::vector<Pair> overlapPairs(const mesh::Mesh& A, const mesh::Mesh& B, unsigned threads) { return overlapPairs(triangleBoxes(A), triangleBoxes(B), threads); }
std::vector<Pair> bruteForcePairs(const std::vector<Box>& a, const std::vector<Box>& b) {
    std::vector<Pair> out;
    for (size_t i = 0; i < a.size(); i++) for (size_t j = 0; j < b.size(); j++) if (overlap(a[i], b[j])) out.push_back({(uint32_t)i, (uint32_t)j});
    return out;
}
std::vector<Pair> bruteForcePairs(const mesh::Mesh& A, const mesh::Mesh& B) { return bruteForcePairs(triangleBoxes(A), triangleBoxes(B)); }
}  // namespace bvhref

#ifdef BVH_PAIRS_MAIN
using namespace bvhref;
namespace sha {   // minimal SHA-256
struct Ctx { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; };
static const uint32_t K[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static inline uint32_t R(uint32_t x, int n) { return x >> n | x << (32 - n); }
static void block(Ctx& c, const uint8_t* p) {
    uint32_t w[64]; for (int i = 0; i < 16; i++) w[i] = p[4*i] << 24 | p[4*i+1] << 16 | p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) { uint32_t s0 = R(w[i-15],7) ^ R(w[i-15],18) ^ (w[i-15] >> 3), s1 = R(w[i-2],17) ^ R(w[i-2],19) ^ (w[i-2] >> 10); w[i] = w[i-16] + s0 + w[i-7] + s1; }
    uint32_t a=c.h[0],b=c.h[1],cc=c.h[2],d=c.h[3],e=c.h[4],f=c.h[5],g=c.h[6],h=c.h[7];
    for (int i = 0; i < 64; i++) { uint32_t S1 = R(e,6)^R(e,11)^R(e,25), ch = (e&f)^(~e&g), t1 = h+S1+ch+K[i]+w[i], S0 = R(a,2)^R(a,13)^R(a,22), mj = (a&b)^(a&cc)^(b&cc), t2 = S0+mj; h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2; }
    c.h[0]+=a;c.h[1]+=b;c.h[2]+=cc;c.h[3]+=d;c.h[4]+=e;c.h[5]+=f;c.h[6]+=g;c.h[7]+=h;
}
static void init(Ctx& c) { uint32_t i0[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}; memcpy(c.h, i0, 32); c.len = 0; c.n = 0; }
static void update(Ctx& c, const void* d, size_t n) { const uint8_t* p = (const uint8_t*)d; c.len += n; while (n) { size_t k = std::min(n, 64 - c.n); memcpy(c.buf + c.n, p, k); c.n += k; p += k; n -= k; if (c.n == 64) { block(c, c.buf); c.n = 0; } } }
static std::string final(Ctx& c) { uint64_t bits = c.len * 8; uint8_t pad = 0x80; update(c, &pad, 1); uint8_t z = 0; while (c.n != 56) update(c, &z, 1); uint8_t l[8]; for (int i = 0; i < 8; i++) l[i] = bits >> (56 - 8*i); update(c, l, 8); char s[65]; for (int i = 0; i < 8; i++) snprintf(s + 8*i, 9, "%08x", c.h[i]); return s; }
}
static std::string hashPairs(const std::vector<Pair>& p) { sha::Ctx c; sha::init(c); for (const Pair& x : p) { uint32_t v[2] = {x.i, x.j}; sha::update(c, v, 8); } return sha::final(c); }
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static void translate(mesh::Mesh& m, double dx, double dy, double dz) { for (size_t i = 0; i < m.nv(); i++) { m.v[3*i] += dx; m.v[3*i+1] += dy; m.v[3*i+2] += dz; } }

int main(int argc, char** argv) {
    unsigned hc = std::max(1u, std::thread::hardware_concurrency());
    if (argc >= 3 && !strcmp(argv[1], "--set")) {
        FILE* f = fopen(argv[2], "rb"); uint32_t h[2]; if (!f || fread(h, 4, 2, f) != 2 || h[0] != mesh::kMagicCont) { fprintf(stderr, "bad container\n"); return 1; }
        std::vector<mesh::Mesh> ms(h[1]); for (auto& m : ms) if (!mesh::readRecord(f, m)) { fprintf(stderr, "bad record\n"); return 1; }
        fclose(f);
        size_t tot = 0; for (auto& m : ms) tot += m.nt();
        double t0 = now(); size_t pairs = 0, bf = 0; bool eq = true; std::string hh;
        std::vector<Pair> last;
        for (size_t k = 0; k + 1 < ms.size(); k++) { auto p = overlapPairs(ms[k], ms[k+1], 1); pairs += p.size(); }
        double t1 = now();
        for (size_t k = 0; k + 1 < ms.size(); k++) { auto p = overlapPairs(ms[k], ms[k+1], hc); (void)p; }
        double t2 = now();
        for (size_t k = 0; k + 1 < ms.size(); k++) { auto p = overlapPairs(ms[k], ms[k+1], 1); auto q = bruteForcePairs(ms[k], ms[k+1]); bf += q.size(); if (!(p == q)) eq = false; }
        double t3 = now();
        printf("many-small: %zu meshes (%zu triangles total), %zu consecutive pairs\n", ms.size(), tot, ms.size() - 1);
        printf("  box-overlap pairs total (BVH) %zu, brute force %zu, equal %s\n", pairs, bf, eq ? "YES" : "NO");
        printf("  all consecutive pairs, BVH 1 thread: %.4f s;  %u threads (threads spawned per pair): %.4f s;  (brute force + BVH, 1 thread: %.4f s)\n", t1 - t0, hc, t2 - t1, t3 - t2);
        return eq ? 0 : 1;
    }
    if (argc < 3) { fprintf(stderr, "usage: bvh_pairs_bench A.msh B.msh [--translate dx dy dz] [--threads-check] | --set file.mshc\n"); return 2; }
    mesh::Mesh A, B; if (!mesh::readMesh(argv[1], A) || !mesh::readMesh(argv[2], B)) { fprintf(stderr, "cannot read\n"); return 1; }
    bool tcheck = false, forceBf = false;
    for (int i = 3; i < argc; i++) { if (!strcmp(argv[i], "--translate") && i + 3 < argc) { translate(B, atof(argv[i+1]), atof(argv[i+2]), atof(argv[i+3])); i += 3; } else if (!strcmp(argv[i], "--threads-check")) tcheck = true; else if (!strcmp(argv[i], "--force-bf")) forceBf = true; }
    auto ba = triangleBoxes(A), bb = triangleBoxes(B);
    printf("A %zu tris, B %zu tris, hardware_concurrency %u\n", A.nt(), B.nt(), hc);
    double bs1, qs1, bsn, qsn;
    auto p1 = overlapPairs(ba, bb, 1, &bs1, &qs1);
    auto pn = overlapPairs(ba, bb, hc, &bsn, &qsn);
    printf("BVH pairs %zu  sha256 %s\n", p1.size(), hashPairs(p1).c_str());
    printf("timing: BVH build %.4f s; query 1 thread %.4f s; query %u threads %.4f s (speedup %.2fx; the build is single-threaded)\n", bs1, qs1, hc, qsn, qs1 / qsn);
    int rc = 0;
    if ((double)A.nt() * (double)B.nt() <= 4e8 || forceBf) {
        double t0 = now(); auto bf = bruteForcePairs(ba, bb); double t1 = now();
        bool eq = bf == p1; if (!eq) rc = 1;
        printf("brute force pairs %zu (%.4f s); BVH == brute force: %s\n", bf.size(), t1 - t0, eq ? "YES" : "NO");
    } else printf("brute force skipped (nA*nB = %.3g > 4e8)\n", (double)A.nt() * (double)B.nt());
    printf("%u-thread result identical to 1 thread: %s\n", hc, pn == p1 ? "YES" : "NO"); if (!(pn == p1)) rc = 1;
    if (tcheck) { for (unsigned t : {1u, 2u, 4u, 8u}) { auto p = overlapPairs(ba, bb, t); printf("threads %u sha256 %s\n", t, hashPairs(p).c_str()); } }
    return rc;
}
#endif
