// MP24 families (written after the pipeline was frozen, see lib/geogram/mp24/FROZEN_SHA256.txt).
//   gen_fidelity <outdir>     writes <outdir>/offset/ and <outdir>/thin/, each a corpus in the G1 layout (manifest.tsv + adversarial/<id>_{A,B}.msh)
// offset family: G1-style box templates, random lattice boxes and spheres at offsets 2^20, 2^30, 2^40, 2^50, 3*2^40+1, 2^45+3 and mixed per-axis vectors; every box
//   coordinate is offset + k*u with u the coarsest ulp of the offset vector, so the translated pair is the EXACT translate of a local pair (checked with TwoSum, a pair
//   that is not exact is skipped and counted); expectations are exact (per-axis interval arithmetic on the stored doubles, BigInt). Also: "b53" pairs whose half span is
//   about 2^52 lattice units (must pass; sensitive to the re-centring), and "span54" pairs whose half span exceeds 2^53 units (the pipeline must REFUSE, tag span54).
// thin family: slivers, gaps, overlaps and shells of width 2^-30, 2^-40, 2^-50 of the extent (extents 1, 2^8, 2^-8, boxes along x or z), plus spheres cut by a plate.
// Coordinates come only from + - * / sqrt and ldexp, every rnd() call is in its own statement, build with -O2 -ffp-contract=off.
#include "../../lib/geogram/mp21b/validator.h"
#include <array>
#include <map>
#include <sys/stat.h>
#include <cmath>

using namespace mesh;
static std::string g_dir; static FILE* g_manifest; static size_t g_pairs, g_skipped, g_invalid;

static void mkdirp(const std::string& p) { mkdir(p.c_str(), 0755); }
struct Box { double lo[3], hi[3]; };
static Mesh makeBox(const Box& b) {
    Mesh m;
    for (int z = 0; z < 2; z++) { const double Z = z ? b.hi[2] : b.lo[2];
        m.addV(b.lo[0], b.lo[1], Z); m.addV(b.hi[0], b.lo[1], Z); m.addV(b.hi[0], b.hi[1], Z); m.addV(b.lo[0], b.hi[1], Z); }
    static const uint32_t T[12][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{0,4,7},{0,7,3},{1,2,6},{1,6,5}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return m;
}
static Mesh makeIcosphere(int sub, double radius, bool spiky = false) {   // same construction as corpus/gen_corpus.cpp
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    double base[12][3] = {{-1,phi,0},{1,phi,0},{-1,-phi,0},{1,-phi,0},{0,-1,phi},{0,1,phi},{0,-1,-phi},{0,1,-phi},{phi,0,-1},{phi,0,1},{-phi,0,-1},{-phi,0,1}};
    static const uint32_t F[20][3] = {{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
                                      {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
    Mesh m;
    auto norm = [&](double x, double y, double z) { double l = std::sqrt(x * x + y * y + z * z); return std::array<double, 3>{x / l, y / l, z / l}; };
    for (auto& b : base) { auto n = norm(b[0], b[1], b[2]); m.addV(n[0], n[1], n[2]); }
    std::vector<std::array<uint32_t, 3>> faces;
    for (auto& f : F) faces.push_back({f[0], f[1], f[2]});
    for (int s = 0; s < sub; s++) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = mid.find(key); if (it != mid.end()) return it->second;
            auto n = norm((m.v[3*a] + m.v[3*b]) / 2, (m.v[3*a+1] + m.v[3*b+1]) / 2, (m.v[3*a+2] + m.v[3*b+2]) / 2);
            return mid[key] = m.addV(n[0], n[1], n[2]);
        };
        std::vector<std::array<uint32_t, 3>> nf;
        for (auto& f : faces) {
            uint32_t a = midpoint(f[0], f[1]), b = midpoint(f[1], f[2]), c = midpoint(f[2], f[0]);
            nf.push_back({f[0], a, c}); nf.push_back({f[1], b, a}); nf.push_back({f[2], c, b}); nf.push_back({a, b, c});
        }
        faces.swap(nf);
    }
    for (auto& f : faces) m.addT(f[0], f[1], f[2]);
    for (size_t i = 0; i < m.nv(); i++) { double k = radius * (spiky && i % 3 == 0 ? 2.0 : 1.0); for (int c = 0; c < 3; c++) m.v[3*i+c] *= k; }
    return m;
}
static Mesh translated(const Mesh& a, const double* o) { Mesh m = a; for (size_t i = 0; i < m.nv(); i++) for (int k = 0; k < 3; k++) m.v[3*i+k] = m.v[3*i+k] + o[k]; return m; }

// ---- exact helpers ----
static double twoSumErr(double a, double b) { double s = a + b, bb = s - a; return (a - (s - bb)) + (b - bb); }
static double ulpOf(double x) { int e; std::frexp(std::fabs(x), &e); return std::ldexp(1.0, e - 53); }   // spacing of doubles in the binade of |x| (x != 0)

static void emit(const std::string& id, const std::string& cls, const std::string& tags, const Mesh& A, const Mesh& B, const std::string* ex) {
    std::string d = g_dir + "/adversarial/";
    writeMesh(d + id + "_A.msh", A); writeMesh(d + id + "_B.msh", B);
    fprintf(g_manifest, "%s\t%s\t%s\t%zu\t%zu\t%zu\t%zu", id.c_str(), cls.c_str(), tags.c_str(), A.nv(), A.nt(), B.nv(), B.nt());
    for (int i = 0; i < 5; i++) fprintf(g_manifest, "\t%s", ex ? ex[i].c_str() : "-");
    fprintf(g_manifest, "\n"); g_pairs++;
}
// box pair with exact expectations computed from the stored doubles. Returns false (and counts) if a box is degenerate.
static bool boxPair(const std::string& id, const std::string& cls, const std::string& tags, Box a, Box b) {
    for (int k = 0; k < 3; k++) if (!(a.hi[k] > a.lo[k]) || !(b.hi[k] > b.lo[k])) { g_invalid++; return false; }
    std::vector<double> xs = {a.lo[0], a.lo[1], a.lo[2], a.hi[0], a.hi[1], a.hi[2], b.lo[0], b.lo[1], b.lo[2], b.hi[0], b.hi[1], b.hi[2]};
    BigInt v[12]; toCommonInts({xs[0],xs[1],xs[2],xs[3],xs[4],xs[5],xs[6],xs[7],xs[8],xs[9],xs[10],xs[11]}, v);
    int emin = INT32_MAX; for (double x : xs) { if (x == 0) continue; int e; double f = std::frexp(x, &e); int64_t mant = (int64_t)std::ldexp(f, 53); (void)mant; emin = std::min(emin, e - 53); }
    if (emin == INT32_MAX) emin = 0;
    auto ext = [&](int lo, int hi) { return v[hi] - v[lo]; };
    BigInt va = ext(0, 3) * ext(1, 4) * ext(2, 5), vb = ext(6, 9) * ext(7, 10) * ext(8, 11), inter = 1;
    for (int k = 0; k < 3; k++) { BigInt lo = std::max(v[k], v[6 + k]), hi = std::min(v[3 + k], v[9 + k]); inter *= hi > lo ? hi - lo : BigInt(0); }
    int ex3 = 3 * emin;
    auto fmt = [&](const BigInt& x) { return Dyadic{x * 6, ex3}; };
    std::string ex[5] = {toString(fmt(va)), toString(fmt(vb)), toString(fmt(va + vb - inter)), toString(fmt(inter)), toString(fmt(va - inter))};
    Mesh A = makeBox(a), B = makeBox(b);
    if (!mp21b::validate(A).empty() || !mp21b::validate(B).empty()) { g_invalid++; return false; }
    emit(id, cls, tags, A, B, ex); return true;
}
static bool meshPair(const std::string& id, const std::string& cls, const std::string& tags, const Mesh& A, const Mesh& B, bool identical) {
    if (!mp21b::validate(A).empty() || !mp21b::validate(B).empty()) { g_invalid++; return false; }
    if (identical) { std::string v6 = toString(exactVolumeTimes6(A)), z = "0*2^0"; std::string ex[5] = {v6, v6, v6, v6, z}; emit(id, cls, tags, A, B, ex); }
    else emit(id, cls, tags, A, B, nullptr);
    return true;
}
static void openCorpus(const std::string& dir) {
    g_dir = dir; mkdirp(dir); mkdirp(dir + "/adversarial");
    g_manifest = fopen((dir + "/manifest.tsv").c_str(), "wb");
    fprintf(g_manifest, "id\tclass\ttags\tnvA\tntA\tnvB\tntB\tvol6_A\tvol6_B\tvol6_union\tvol6_inter\tvol6_diff\n");
    g_pairs = g_skipped = g_invalid = 0;
}
static void closeCorpus(const char* name) { fclose(g_manifest); printf("%s: %zu pairs written, %zu skipped (translation not exact), %zu skipped (degenerate or invalid operand)\n", name, g_pairs, g_skipped, g_invalid); }

// ---- templates: boxes in local coordinates, A = [0,E]^3 unless the template says otherwise; q = E/8, w = sliver width ----
static const int kTemplates = 18;
static const char* kTplName[kTemplates] = {"identical","face_touch_full","face_touch_partial","edge_touch","vertex_touch","nested_inside","nested_touching_face","overlap_generic","on_top_of_face",
    "coplanar_overlap","coplanar_chain","sliver_inside","sliver_crossing","shift_w","shift_w_all","gap_w","overlap_w","shell_w"};
static void tpl(int t, double E, double w, Box& A, Box& B) {
    double q = E / 8;
    A = {{0,0,0},{E,E,E}};
    switch (t) {
    case 0: B = A; break;
    case 1: B = {{E,0,0},{2*E,E,E}}; break;
    case 2: B = {{E,4*q,2*q},{2*E,12*q,6*q}}; break;
    case 3: B = {{E,E,0},{2*E,2*E,E}}; break;
    case 4: B = {{E,E,E},{2*E,2*E,2*E}}; break;
    case 5: A = {{0,0,0},{4*E,4*E,4*E}}; B = {{E,E,E},{2*E,2*E,2*E}}; break;
    case 6: A = {{0,0,0},{4*E,4*E,4*E}}; B = {{0,E,E},{2*E,2*E,2*E}}; break;
    case 7: B = {{4*q,2*q,-4*q},{12*q,10*q,4*q}}; break;
    case 8: B = {{2*q,2*q,E},{6*q,6*q,E+4*q}}; break;
    case 9: B = {{4*q,4*q,0},{12*q,12*q,E}}; break;
    case 10: A = {{0,0,0},{2*E,E,E}}; B = {{E,0,0},{3*E,E,E}}; break;
    case 11: B = {{0,0,4*q},{E,E,4*q+w}}; break;
    case 12: B = {{2*q,-E,4*q},{6*q,2*E,4*q+w}}; break;
    case 13: B = {{w,0,0},{E+w,E,E}}; break;
    case 14: B = {{w,w,w},{E+w,E+w,E+w}}; break;
    case 15: B = {{E+w,0,0},{2*E+w,E,E}}; break;
    case 16: B = {{E-w,0,0},{2*E-w,E,E}}; break;
    case 17: B = {{w,w,w},{E-w,E-w,E-w}}; break;
    }
}
static Box permuted(const Box& b, int axis) {   // axis 0 = as is; 2 = swap x and z
    if (axis == 0) return b; Box r = b; std::swap(r.lo[0], r.lo[2]); std::swap(r.hi[0], r.hi[2]); return r;
}

// ---- LCG, every call in its own statement ----
static uint64_t g_s = 0x4D50323446494445ull;
static uint32_t rnd() { g_s = g_s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(g_s >> 33); }

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: gen_fidelity <outdir>\n"); return 2; }
    std::string out = argv[1]; mkdirp(out);
    // ================= offset family =================
    openCorpus(out + "/offset");
    const double P20 = std::ldexp(1.0, 20), P30 = std::ldexp(1.0, 30), P40 = std::ldexp(1.0, 40), P50 = std::ldexp(1.0, 50);
    struct Vec { const char* name; double o[3]; };
    const Vec vecs[8] = {{"p20", {P20, P20, P20}}, {"p30", {P30, P30, P30}}, {"p40", {P40, P40, P40}}, {"p50", {P50, P50, P50}},
        {"odd3x40", {3 * P40 + 1, 3 * P40 + 1, 3 * P40 + 1}}, {"odd45", {std::ldexp(1.0, 45) + 3, std::ldexp(1.0, 45) + 3, std::ldexp(1.0, 45) + 3}},
        {"mixA", {P40, -(3 * P30 + 1), P20 + 5}}, {"mixB", {-(P40 + 7), std::ldexp(1.0, 33) + 1, -std::ldexp(1.0, 25)}}};
    for (const Vec& V : vecs) {
        double u = 0; for (int k = 0; k < 3; k++) u = std::max(u, ulpOf(V.o[k]));
        const double Es[3] = {8 * u, 1.0, 1024.0};
        for (int ei = 0; ei < 3; ei++) for (int t = 0; t < kTemplates; t++) {
            double E = Es[ei]; Box a, b; tpl(t, E, u, a, b);
            Box ta = a, tb = b; bool exact = true;
            for (int k = 0; k < 3; k++) {
                ta.lo[k] = a.lo[k] + V.o[k]; ta.hi[k] = a.hi[k] + V.o[k]; tb.lo[k] = b.lo[k] + V.o[k]; tb.hi[k] = b.hi[k] + V.o[k];
                if (twoSumErr(a.lo[k], V.o[k]) != 0 || twoSumErr(a.hi[k], V.o[k]) != 0 || twoSumErr(b.lo[k], V.o[k]) != 0 || twoSumErr(b.hi[k], V.o[k]) != 0) exact = false;
            }
            if (!exact) { g_skipped++; continue; }
            std::string id = std::string("off_") + kTplName[t] + "_" + V.name + "_E" + std::to_string(ei);
            boxPair(id, "offset", std::string("valid,offset,") + kTplName[t], ta, tb);
        }
        // random lattice boxes: lo = a*m*u, hi = lo + (1+b)*m*u (every draw in its own statement)
        for (int r = 0; r < 4; r++) {
            Box a, b; const double mults[3] = {1.0, 1024.0, 1048576.0}; uint32_t d0 = rnd(); double m = mults[d0 % 3];
            double ar[2][3][2];
            for (int bx = 0; bx < 2; bx++) for (int k = 0; k < 3; k++) { uint32_t l = rnd(); uint32_t w = rnd(); ar[bx][k][0] = (double)(l % 32) * m * u; ar[bx][k][1] = (double)(1 + w % 32) * m * u; }
            for (int k = 0; k < 3; k++) { a.lo[k] = ar[0][k][0] + V.o[k]; a.hi[k] = ar[0][k][0] + ar[0][k][1] + V.o[k]; b.lo[k] = ar[1][k][0] + V.o[k]; b.hi[k] = ar[1][k][0] + ar[1][k][1] + V.o[k]; }
            bool exact = true; for (int k = 0; k < 3; k++) for (int bx = 0; bx < 2; bx++) { if (twoSumErr(ar[bx][k][0], V.o[k]) != 0 || twoSumErr(ar[bx][k][0] + ar[bx][k][1], V.o[k]) != 0) exact = false; }
            if (!exact) { g_skipped++; continue; }
            boxPair(std::string("off_lattice_") + V.name + "_r" + std::to_string(r), "offset", "valid,offset,lattice", a, b);
        }
        // spheres: radius 1 up to offset 2^40, 2^10 at 2^50 (so the translated mesh keeps its shape); the translation of a full-mantissa mesh rounds, the pair is the stored doubles
        double R = std::ldexp(1.0, std::max(0, (int)std::ceil(std::log2(std::fabs(V.o[0]))) - 40));
        Mesh S = makeIcosphere(3, R), S2 = makeIcosphere(2, R, true);
        double sh[3] = {R / 64, 0, 0}; Mesh Ssh = translated(S, sh);
        Mesh tS = translated(S, V.o), tSsh = translated(Ssh, V.o), tS2 = translated(S2, V.o);
        Mesh bx = makeBox({{-R, -R, -R}, {R, R, R}}); Mesh tbx = translated(bx, V.o);
        meshPair(std::string("off_sphere_identical_") + V.name, "offset", "valid,offset,curved,identical", tS, tS, true);
        meshPair(std::string("off_sphere_shifted_") + V.name, "offset", "valid,offset,curved,overlap", tS, tSsh, false);
        meshPair(std::string("off_sphere_in_box_") + V.name, "offset", "valid,offset,curved,tangent", tS, tbx, false);
        meshPair(std::string("off_sphere_vs_star_") + V.name, "offset", "valid,offset,curved,non-convex", tS, tS2, false);
    }
    // b53: half span about 2^52 lattice units (centred near the origin), must PASS; lattice unit u = 2^-20, 2^-30, 2^-40
    for (int si = 0; si < 3; si++) {
        double u = std::ldexp(1.0, -20 - 10 * si); const double T52 = std::ldexp(1.0, 52), T51 = std::ldexp(1.0, 51), T50 = std::ldexp(1.0, 50);
        Box A = {{-(T52 + 3) * u, -(T52 + 3) * u, -(T52 + 3) * u}, {(T52 + 4) * u, (T52 + 4) * u, (T52 + 4) * u}};
        std::string s = std::to_string(si);
        boxPair("off_b53_corner_overlap_" + s, "offset", "valid,offset,b53", A, {{(T51 + 1) * u, (T51 + 1) * u, (T51 + 1) * u}, {(3 * T51 + 4) * u, (3 * T51 + 4) * u, (3 * T51 + 4) * u}});
        boxPair("off_b53_face_touch_" + s, "offset", "valid,offset,b53", A, {{(T52 + 4) * u, -(T50 + 5) * u, -(T50 + 9) * u}, {(T52 + 4 + T50 + 1) * u, (T50 + 7) * u, (T50 + 11) * u}});
        boxPair("off_b53_strip_" + s, "offset", "valid,offset,b53", A, {{-(T51 + 5) * u, (T50 + 1) * u, -(T51 + 3) * u}, {(T51 + 7) * u, (T52 + 9) * u, (T51 + 9) * u}});
    }
    // span54: half span > 2^53 lattice units with an odd lattice coordinate far from the centre: x - c is not representable, the pipeline must refuse (tag span54)
    for (int ax = 0; ax < 3; ax++) for (int vi = 0; vi < 2; vi++) {
        double u = vi ? std::ldexp(1.0, -31) : std::ldexp(1.0, -30), H = vi ? std::ldexp(1.0, 24) : std::ldexp(1.0, 25);
        Box a = {{0,0,0},{H,H,H}}, b = a; b.lo[ax] = u; b.hi[ax] = H + u;
        boxPair("off_span54_ax" + std::to_string(ax) + "_v" + std::to_string(vi), "offset", "valid,offset,span54", a, b);
    }
    closeCorpus("offset");

    // ================= thin family =================
    openCorpus(out + "/thin");
    const double Es[3] = {1.0, 256.0, std::ldexp(1.0, -8)}; const int ks[3] = {30, 40, 50}; const char* en[3] = {"E1", "E256", "Em8"};
    const int thinT[9] = {11, 12, 15, 16, 13, 14, 17, 100, 101};   // 100: flush plate on a face (coplanar), 101: plate crossing the top face
    const char* thinName[9] = {"sliver_inside", "sliver_crossing", "gap_w", "overlap_w", "shift_w", "shift_w_all", "shell_w", "flush_plate", "plate_over_top"};
    for (int ei = 0; ei < 3; ei++) for (int ki = 0; ki < 3; ki++) for (int ti = 0; ti < 9; ti++) for (int axis = 0; axis < 3; axis += 2) {
        double E = Es[ei], w = std::ldexp(E, -ks[ki]); Box a, b; int t = thinT[ti];
        if (t < 100) tpl(t, E, w, a, b);
        else { a = {{0,0,0},{E,E,E}}; b = t == 100 ? Box{{0,0,0},{E,E,w}} : Box{{0,0,E-w},{E,E,2*E}}; }
        std::string id = std::string("thin_") + thinName[ti] + "_" + en[ei] + "_k" + std::to_string(ks[ki]) + "_ax" + std::to_string(axis);
        boxPair(id, "thin", std::string("valid,thin,") + thinName[ti], permuted(a, axis), permuted(b, axis));
    }
    for (int ei = 0; ei < 3; ei++) for (int ki = 0; ki < 3; ki++) {   // sphere cut by a plate of width w (no volume expectation: point-in-solid only)
        double E = Es[ei], w = std::ldexp(E, -ks[ki]); Mesh S = makeIcosphere(3, E);
        Box pl = {{-2*E, -2*E, 0}, {2*E, 2*E, w}};
        meshPair(std::string("thin_sphere_plate_") + en[ei] + "_k" + std::to_string(ks[ki]), "thin", "valid,thin,curved,sliver", S, makeBox(pl), false);
    }
    closeCorpus("thin");
    return 0;
}
