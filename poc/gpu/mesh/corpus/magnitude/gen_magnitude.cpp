// MP25: deterministic magnitude/offset sweep generator for the Geogram coordinate-magnitude characterisation.
//   gen_magnitude <outdir>
// Build: c++ -std=c++17 -O2 -ffp-contract=off gen_magnitude.cpp -o gen_magnitude
// Writes <outdir>/pairs/<id>_{A,B}.msh and <outdir>/manifest.tsv. Every shape is an axis-aligned box or an octahedron with integer corner
// coordinates m (|m| < 2^53) times a power of two 2^e, so scaling by any power of two is exact and the expected volumes are exact integers
// at every scale. Volumes are written as N (decimal), meaning volume = N/3 * 2^E (E = 3e), i.e. N = 3 * (volume in unit^3).
// Operations: union = A+B, intersection = A*B, difference = A-B. The only randomness is splitmix64 (seeded below), one rnd() per statement.
#include "../../harness/mesh_io.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
using namespace mesh;
typedef __int128 i128;
typedef long long ll;
static unsigned long long g_state = 0x4d503235ull;  // "MP25"
static unsigned long long rnd() {
    unsigned long long z = (g_state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static std::string str(i128 v) {
    if (v == 0) return "0";
    bool neg = v < 0; if (neg) v = -v;
    std::string s; while (v > 0) { s.insert(s.begin(), char('0' + (int)(v % 10))); v /= 10; }
    return neg ? "-" + s : s;
}
static std::string g_out; static FILE* g_man;
struct B3 { ll lo[3], hi[3]; };
static double sc(ll m, int e) { return std::ldexp((double)m, e); }   // exact: |m| < 2^53
static Mesh makeBox(const B3& b, int e) {
    Mesh m;
    for (int z = 0; z < 2; z++) { const ll Z = z ? b.hi[2] : b.lo[2];
        m.addV(sc(b.lo[0], e), sc(b.lo[1], e), sc(Z, e)); m.addV(sc(b.hi[0], e), sc(b.lo[1], e), sc(Z, e));
        m.addV(sc(b.hi[0], e), sc(b.hi[1], e), sc(Z, e)); m.addV(sc(b.lo[0], e), sc(b.hi[1], e), sc(Z, e)); }
    static const uint32_t T[12][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{0,4,7},{0,7,3},{1,2,6},{1,6,5}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return m;
}
static Mesh makeOcta(const ll c[3], ll r, int e) {
    Mesh m;
    const ll d[6][3] = {{r,0,0},{-r,0,0},{0,r,0},{0,-r,0},{0,0,r},{0,0,-r}};
    for (auto& p : d) m.addV(sc(c[0] + p[0], e), sc(c[1] + p[1], e), sc(c[2] + p[2], e));
    static const uint32_t T[8][3] = {{0,2,4},{0,5,2},{0,4,3},{0,3,5},{1,4,2},{1,2,5},{1,3,4},{1,5,3}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return m;
}
static i128 boxVol3(const B3& b) { i128 v = 1; for (int k = 0; k < 3; k++) v *= (i128)(b.hi[k] - b.lo[k]); return v * 3; }   // 3 * volume in units^3
static i128 interVol3(const B3& a, const B3& b) {
    i128 v = 1;
    for (int k = 0; k < 3; k++) { ll lo = std::max(a.lo[k], b.lo[k]), hi = std::min(a.hi[k], b.hi[k]); if (hi <= lo) return 0; v *= (i128)(hi - lo); }
    return v * 3;
}
static void wr(const std::string& id, const std::string& fam, int k, int extra, int e, const Mesh& A, const Mesh& B,
               i128 nA, i128 nB, i128 nU, i128 nI, i128 nD) {
    std::string pa = "pairs/" + id + "_A.msh", pb = "pairs/" + id + "_B.msh";
    if (!writeMesh(g_out + "/" + pa, A) || !writeMesh(g_out + "/" + pb, B)) { fprintf(stderr, "write failed %s\n", id.c_str()); exit(1); }
    fprintf(g_man, "%s\t%s\t%d\t%d\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", id.c_str(), fam.c_str(), k, extra, e, pa.c_str(), pb.c_str(),
            str(nA).c_str(), str(nB).c_str(), str(nU).c_str(), str(nI).c_str(), str(nD).c_str());
}
static void boxPair(const std::string& id, const std::string& fam, int k, int extra, int e, const B3& a, const B3& b) {
    i128 va = boxVol3(a), vb = boxVol3(b), vi = interVol3(a, b);
    wr(id, fam, k, extra, e, makeBox(a, e), makeBox(b, e), va, vb, va + vb - vi, vi, va - vi);
}
static B3 cube(ll lo, ll hi) { return B3{{lo, lo, lo}, {hi, hi, hi}}; }
static B3 shifted(const B3& b, ll dx, ll dy, ll dz) { B3 r = b; r.lo[0] += dx; r.hi[0] += dx; r.lo[1] += dy; r.hi[1] += dy; r.lo[2] += dz; r.hi[2] += dz; return r; }
static std::string nm(const char* f, int k) { char b[96]; snprintf(b, sizeof b, "%s_k%+03d", f, k); return b; }

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: gen_magnitude <outdir>\n"); return 2; }
    g_out = argv[1]; mkdir(g_out.c_str(), 0755); mkdir((g_out + "/pairs").c_str(), 0755);
    g_man = fopen((g_out + "/manifest.tsv").c_str(), "w");
    fprintf(g_man, "id\tfamily\tk\textra\te\tA\tB\tN_A\tN_B\tN_union\tN_inter\tN_diff\n");
    const ll H = 1LL << 21;
    // --- absolute-scale sweep: container half-width 2^k, k = -20..45 (unit 2^(k-21), container half-width H units) ---
    for (int k = -20; k <= 45; k++) {
        const int e = k - 21; const B3 box = cube(-H, H);
        boxPair(nm("nest21", k), "nest21", k, 0, e, box, cube(-1, 1));                       // inner edge 2^(k-20), ratio 2^20 (k=20: container 2^20, inner edge 1)
        boxPair(nm("nesthalf", k), "nesthalf", k, 0, e, box, cube(-H / 2, H / 2));           // ratio 2
        boxPair(nm("overlap", k), "overlap", k, 0, e, box, shifted(box, H / 2, H / 2, H / 2));
        boxPair(nm("touchface", k), "touchface", k, 0, e, box, shifted(box, 2 * H, 0, 0));
        boxPair(nm("touchedge", k), "touchedge", k, 0, e, box, shifted(box, 2 * H, 2 * H, 0));
        boxPair(nm("touchvert", k), "touchvert", k, 0, e, box, shifted(box, 2 * H, 2 * H, 2 * H));
        boxPair(nm("disjoint", k), "disjoint", k, 0, e, box, shifted(box, 3 * H, 0, 0));
        boxPair(nm("identical", k), "identical", k, 0, e, box, box);
        {   // octahedron radius H/2 nested in the box: union = box, A*B = octa, A-B = box - octa; octa volume = 4 r^3 / 3
            const ll c[3] = {0, 0, 0}; const ll r = H / 2; const i128 vo3 = (i128)4 * r * r * r;   // 3*volume = 4 r^3
            const i128 va = boxVol3(box);
            wr(nm("octain", k), "octain", k, 0, e, makeBox(box, e), makeOcta(c, r, e), va, vo3, va, vo3, va - vo3);
        }
        {   // box x in [-2H,0], y,z in [-2H,2H] cuts an octahedron of radius H at its x=0 symmetry plane: A*B = octa/2
            const B3 a = {{-2 * H, -2 * H, -2 * H}, {0, 2 * H, 2 * H}}; const ll c[3] = {0, 0, 0}; const ll r = H;
            const i128 vo3 = (i128)4 * r * r * r, va = boxVol3(a), vi = vo3 / 2;
            wr(nm("octahalf", k), "octahalf", k, 0, e, makeBox(a, e), makeOcta(c, r, e), va, vo3, va + vo3 - vi, vi, va - vi);
        }
        {   // random inner box in the container (half-widths 1..64 units, centre a multiple of 1024 units within +-H/2)
            B3 in; for (int a = 0; a < 3; a++) { ll hw = 1 + (ll)(rnd() % 64); ll cc = ((ll)(rnd() % (H / 1024)) - H / 2048) * 1024; in.lo[a] = cc - hw; in.hi[a] = cc + hw; }
            boxPair(nm("randnest", k), "randnest", k, 0, e, box, in);
        }
        {   // random box pair partly overlapping the container (corner within +-H/2, widths multiples of 1024 up to 2H)
            B3 bb; for (int a = 0; a < 3; a++) { ll lo = ((ll)(rnd() % (H / 512)) - H / 1024) * 1024; ll w = (1 + (ll)(rnd() % (2 * H / 1024))) * 1024; bb.lo[a] = lo; bb.hi[a] = lo + w; }
            boxPair(nm("randover", k), "randover", k, 0, e, box, bb);
        }
    }
    // --- ratio sweep at fixed absolute scale: container half-width 2^k (k = 10, 20, 30, 40), centred inner half-width 2^j, j = k-40 .. k-1 ---
    for (int k = 10; k <= 40; k += 10) for (int j = k - 40; j <= k - 1; j++) {
        char b[96]; snprintf(b, sizeof b, "ratio_k%02d_j%+03d", k, j);
        boxPair(b, "ratio", k, j, j, cube(-(1LL << (k - j)), 1LL << (k - j)), cube(-1, 1));
    }
    // --- offset sweep: unit features at offset o = 2^t, t = 0..50 (unit 2^-1; coordinates 2o, 2o+1, ... in half units) ---
    for (int t = 0; t <= 50; t++) {
        const ll o2 = 2LL << t;   // 2*o
        char b[96];
        snprintf(b, sizeof b, "off_overlap_t%02d", t); boxPair(b, "off_overlap", t, 0, -1, cube(o2, o2 + 2), cube(o2 + 1, o2 + 3));
        snprintf(b, sizeof b, "off_nest_t%02d", t); boxPair(b, "off_nest", t, 0, -1, cube(o2 - 8, o2 + 8), cube(o2, o2 + 2));
        snprintf(b, sizeof b, "off_touch_t%02d", t); boxPair(b, "off_touch", t, 0, -1, cube(o2, o2 + 2), shifted(cube(o2, o2 + 2), 2, 0, 0));
    }
    fclose(g_man);
    return 0;
}
