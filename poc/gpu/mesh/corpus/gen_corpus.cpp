// MP01: deterministic mesh corpus generator.
//   gen_corpus <outdir> [--many N] [--large]
// Writes <outdir>/{primitives,adversarial,sets}/... and <outdir>/manifest.tsv. Every coordinate is produced with only + - * / sqrt and ldexp (all
// correctly rounded or exact in IEEE 754), compiled with -ffp-contract=off, so the bytes are identical on every platform (checked in CI by comparing
// sha256 lists). Adversarial box pairs carry exactly computed expected volumes (6 * volume as num*2^exp, see harness/exact.h).
#include "../harness/exact.h"
#include "../harness/mesh_io.h"
#include <array>
#include <cstdlib>
#include <map>
#include <sys/stat.h>

using namespace mesh;
static std::string g_out;
static FILE* g_manifest;

static void mkdirp(const std::string& p) { mkdir(p.c_str(), 0755); }

// ---- primitives ----
struct Box { double lo[3], hi[3]; };
static Mesh makeBox(const Box& b) {
    Mesh m;
    for (int z = 0; z < 2; z++) { const double Z = z ? b.hi[2] : b.lo[2];
        m.addV(b.lo[0], b.lo[1], Z); m.addV(b.hi[0], b.lo[1], Z); m.addV(b.hi[0], b.hi[1], Z); m.addV(b.lo[0], b.hi[1], Z); }
    static const uint32_t T[12][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{0,4,7},{0,7,3},{1,2,6},{1,6,5}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return m;
}

static Mesh makeIcosphere(int sub, double radius, bool spiky = false) {
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
    for (size_t i = 0; i < m.nv(); i++) {
        double k = radius * (spiky && i % 3 == 0 ? 2.0 : 1.0);
        for (int c = 0; c < 3; c++) m.v[3*i+c] *= k;
    }
    return m;
}

// Torus with major radius 2 and minor radius 0.5, circles from the rational parametrization (no sin/cos): quadrant q, s = j/m.
static std::array<double, 2> circlePoint(int k, int m) {
    int q = k / m, j = k % m; double s = (double)j / m;
    double x = (1.0 - s * s) / (1.0 + s * s), y = 2.0 * s / (1.0 + s * s);
    for (int i = 0; i < q; i++) { double nx = -y, ny = x; x = nx; y = ny; }
    return {x, y};
}
static Mesh makeTorus(int mq, int nq) {   // 4*mq points around the major circle, 4*nq around the minor
    Mesh m; int M = 4 * mq, N = 4 * nq;
    for (int i = 0; i < M; i++) { auto c = circlePoint(i, mq);
        for (int j = 0; j < N; j++) { auto d = circlePoint(j, nq); double rr = 2.0 + 0.5 * d[0];
            m.addV(rr * c[0], rr * c[1], 0.5 * d[1]); } }
    for (int i = 0; i < M; i++) for (int j = 0; j < N; j++) {
        uint32_t a = i * N + j, b = ((i + 1) % M) * N + j, c = ((i + 1) % M) * N + (j + 1) % N, d = i * N + (j + 1) % N;
        m.addT(a, b, c); m.addT(a, c, d);
    }
    if (signExact(exactVolumeTimes6(m)) < 0) for (size_t k = 0; k < m.nt(); k++) std::swap(m.t[3*k+1], m.t[3*k+2]);
    return m;
}
static Mesh makeGrid(int n) {   // open surface in z = 0
    Mesh m;
    for (int j = 0; j <= n; j++) for (int i = 0; i <= n; i++) m.addV(i, j, 0);
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
        uint32_t a = j * (n + 1) + i, b = a + 1, c = a + n + 2, d = a + n + 1;
        m.addT(a, b, c); m.addT(a, c, d);
    }
    return m;
}
static Mesh translated(const Mesh& a, double dx, double dy, double dz) { Mesh m = a; for (size_t i = 0; i < m.nv(); i++) { m.v[3*i] += dx; m.v[3*i+1] += dy; m.v[3*i+2] += dz; } return m; }
static Mesh merged(const Mesh& a, const Mesh& b) {   // naive concatenation (no welding): self-intersecting or non-manifold if the inputs touch or overlap
    Mesh m = a; uint32_t off = (uint32_t)a.nv();
    m.v.insert(m.v.end(), b.v.begin(), b.v.end());
    for (uint32_t i : b.t) m.t.push_back(i + off);
    return m;
}

// Like merged(), but vertices with exactly equal coordinates are identified, so geometrically shared edges become combinatorially shared edges.
static Mesh weldedMerge(const Mesh& a, const Mesh& b) {
    Mesh m = a; std::map<std::array<double, 3>, uint32_t> at;
    for (size_t i = 0; i < a.nv(); i++) at[{a.v[3*i], a.v[3*i+1], a.v[3*i+2]}] = (uint32_t)i;
    std::vector<uint32_t> remap(b.nv());
    for (size_t i = 0; i < b.nv(); i++) {
        std::array<double, 3> k = {b.v[3*i], b.v[3*i+1], b.v[3*i+2]};
        auto it = at.find(k);
        remap[i] = it != at.end() ? it->second : (at[k] = m.addV(k[0], k[1], k[2]));
    }
    for (uint32_t i : b.t) m.t.push_back(remap[i]);
    return m;
}

// ---- manifest ----
static void emit(const std::string& id, const std::string& cls, const std::string& tags, const Mesh& A, const Mesh& B, const std::string* ex /*6 values or null*/) {
    std::string d = g_out + "/adversarial/";
    writeMesh(d + id + "_A.msh", A); writeMesh(d + id + "_B.msh", B);
    fprintf(g_manifest, "%s\t%s\t%s\t%zu\t%zu\t%zu\t%zu", id.c_str(), cls.c_str(), tags.c_str(), A.nv(), A.nt(), B.nv(), B.nt());
    for (int i = 0; i < 5; i++) fprintf(g_manifest, "\t%s", ex ? ex[i].c_str() : "-");
    fprintf(g_manifest, "\n");
}
static void boxPair(const std::string& id, const std::string& cls, const std::string& tags, Box a, Box b) {
    BigInt v[12];
    toCommonInts({a.lo[0], a.lo[1], a.lo[2], a.hi[0], a.hi[1], a.hi[2], b.lo[0], b.lo[1], b.lo[2], b.hi[0], b.hi[1], b.hi[2]}, v);
    int emin = INT32_MAX; for (double x : {a.lo[0], a.lo[1], a.lo[2], a.hi[0], a.hi[1], a.hi[2], b.lo[0], b.lo[1], b.lo[2], b.hi[0], b.hi[1], b.hi[2]}) {
        if (x == 0) continue; int e; double f = std::frexp(x, &e); (void)f; int64_t mant = (int64_t)std::ldexp(std::frexp(x, &e), 53); (void)mant; emin = std::min(emin, e - 53); }
    if (emin == INT32_MAX) emin = 0;
    auto ext = [&](int lo, int hi) { return v[hi] - v[lo]; };
    BigInt va = ext(0, 3) * ext(1, 4) * ext(2, 5), vb = ext(6, 9) * ext(7, 10) * ext(8, 11), inter = 1;
    for (int k = 0; k < 3; k++) {
        BigInt lo = std::max(v[k], v[6 + k]), hi = std::min(v[3 + k], v[9 + k]);
        inter *= hi > lo ? hi - lo : BigInt(0);
    }
    int ex3 = 3 * emin;
    auto fmt = [&](const BigInt& x) { return Dyadic{x * 6, ex3}; };
    std::string ex[5] = {toString(fmt(va)), toString(fmt(vb)), toString(fmt(va + vb - inter)), toString(fmt(inter)), toString(fmt(va - inter))};
    emit(id, cls, tags, makeBox(a), makeBox(b), ex);
}
static void identicalMeshPair(const std::string& id, const std::string& cls, const std::string& tags, const Mesh& A) {
    std::string v6 = toString(exactVolumeTimes6(A)), z = "0*2^0";
    std::string ex[5] = {v6, v6, v6, v6, z};     // A with A: union = intersection = A, difference empty
    emit(id, cls, tags, A, A, ex);
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: gen_corpus <outdir> [--many N] [--large]\n"); return 2; }
    g_out = argv[1]; long many = 1000; bool large = false;
    for (int i = 2; i < argc; i++) { if (!strcmp(argv[i], "--many") && i + 1 < argc) many = atol(argv[++i]); else if (!strcmp(argv[i], "--large")) large = true; }
    mkdirp(g_out); mkdirp(g_out + "/primitives"); mkdirp(g_out + "/adversarial"); mkdirp(g_out + "/sets");
    g_manifest = fopen((g_out + "/manifest.tsv").c_str(), "wb");
    fprintf(g_manifest, "id\tclass\ttags\tnvA\tntA\tnvB\tntB\tvol6_A\tvol6_B\tvol6_union\tvol6_inter\tvol6_diff\n");

    // primitives
    writeMesh(g_out + "/primitives/box.msh", makeBox({{0,0,0},{1,1,1}}));
    for (int s = 0; s <= 5; s++) writeMesh(g_out + "/primitives/icosphere_" + std::to_string(s) + ".msh", makeIcosphere(s, 1.0));
    writeMesh(g_out + "/primitives/star_2.msh", makeIcosphere(2, 1.0, true));
    writeMesh(g_out + "/primitives/torus_8x4.msh", makeTorus(8, 4));
    writeMesh(g_out + "/primitives/torus_32x16.msh", makeTorus(32, 16));
    writeMesh(g_out + "/primitives/grid_16.msh", makeGrid(16));

    // adversarial box pairs with exact expectations
    const double u = 1.0, e40 = std::ldexp(1.0, -40), e52 = std::ldexp(1.0, -52), big = std::ldexp(1.0, 40);
    Box A{{0,0,0},{u,u,u}};
    boxPair("box_identical", "identical", "valid,identical", A, A);
    boxPair("box_face_touch_full", "touch", "valid,face-touch,coplanar-faces", A, {{1,0,0},{2,1,1}});
    boxPair("box_face_touch_partial", "touch", "valid,face-touch,coplanar-faces", A, {{1,0.5,0.25},{2,1.5,0.75}});
    boxPair("box_edge_touch", "touch", "valid,edge-touch", A, {{1,1,0},{2,2,1}});
    boxPair("box_vertex_touch", "touch", "valid,vertex-touch", A, {{1,1,1},{2,2,2}});
    boxPair("box_nested_inside", "nested", "valid,nested", {{0,0,0},{4,4,4}}, {{1,1,1},{2,2,2}});
    boxPair("box_nested_touching_face", "nested", "valid,nested,coplanar-faces", {{0,0,0},{4,4,4}}, {{0,1,1},{2,2,2}});
    boxPair("box_overlap_generic", "overlap", "valid,overlap", A, {{0.5,0.25,-0.5},{1.5,1.25,0.5}});
    boxPair("box_on_top_of_face", "touch", "valid,face-touch,coplanar-faces", A, {{0.25,0.25,1},{0.75,0.75,1.5}});
    boxPair("box_coplanar_overlap", "overlap", "valid,overlap,coplanar-faces", A, {{0.5,0.5,0},{1.5,1.5,1}});
    boxPair("box_coplanar_chain", "overlap", "valid,overlap,coplanar-faces", {{0,0,0},{2,1,1}}, {{1,0,0},{3,1,1}});
    boxPair("box_sliver_inside", "sliver", "valid,sliver,nested", A, {{0,0,0.5},{1,1,0.5 + e40}});
    boxPair("box_sliver_crossing", "sliver", "valid,sliver,overlap", A, {{0.25,-1,0.5},{0.75,2,0.5 + e40}});
    for (int k : {20, 40, 52}) {
        double d = std::ldexp(1.0, -k);
        boxPair("box_shift_2^-" + std::to_string(k), "perturbed", "valid,perturbed,overlap", A, {{d,0,0},{1 + d,1,1}});
    }
    boxPair("box_gap_one_ulp", "perturbed", "valid,gap,perturbed", A, {{1 + e52,0,0},{2,1,1}});
    boxPair("box_huge_offset_overlap", "offset", "valid,huge-offset,overlap", {{big,big,big},{big+1,big+1,big+1}}, {{big+0.5,big+0.5,big+0.5},{big+1.5,big+1.5,big+1.5}});
    boxPair("box_huge_offset_one_ulp", "offset", "valid,huge-offset,perturbed", {{big,big,big},{big+1,big+1,big+1}}, {{big+std::ldexp(1.0,-12),big,big},{big+1+std::ldexp(1.0,-12),big+1,big+1}});

    // non-box pairs: expectations only where they follow from the definition
    Mesh S = makeIcosphere(3, 1.0);
    identicalMeshPair("sphere_identical", "identical", "valid,identical,curved", S);
    emit("sphere_shift_2^-30", "perturbed", "valid,perturbed,curved,overlap", S, translated(S, std::ldexp(1.0, -30), 0, 0), nullptr);
    emit("sphere_in_box_tangent", "touch", "valid,tangent,curved", S, makeBox({{-1,-1,-1},{1,1,1}}), nullptr);
    emit("sphere_vs_star", "overlap", "valid,curved,non-convex,overlap", S, makeIcosphere(2, 1.0, true), nullptr);
    emit("torus_vs_sphere", "overlap", "valid,curved,genus-1,overlap", makeTorus(16, 8), translated(makeIcosphere(3, 1.0), 2, 0, 0), nullptr);

    // malformed operands (the expected behaviour is a clean refusal, a documented repair, or a correct result; never a crash or a wrong answer)
    Mesh unit = makeBox(A), other = makeBox({{0.5,0.5,0.5},{1.5,1.5,1.5}});
    { Mesh o = unit; o.t.resize(o.t.size() - 3); emit("bad_open_box", "malformed", "open,expect=refuse-or-repair", o, other, nullptr); }
    emit("bad_nonmanifold_edge", "malformed", "non-manifold-edge,expect=refuse-or-repair", weldedMerge(makeBox({{0,0,0},{1,1,1}}), makeBox({{1,1,0},{2,2,1}})), other, nullptr);
    emit("bad_self_intersecting", "malformed", "self-intersecting,expect=refuse-or-repair", merged(unit, other), makeBox({{0.25,0.25,0.25},{0.75,0.75,0.75}}), nullptr);
    { Mesh o; for (size_t k = 0; k < unit.nt(); k++) { uint32_t b = (uint32_t)o.nv(); for (int c = 0; c < 3; c++) { uint32_t i = unit.t[3*k+c]; o.addV(unit.v[3*i], unit.v[3*i+1], unit.v[3*i+2]); } o.addT(b, b+1, b+2); }
      emit("bad_unwelded", "malformed", "unwelded-vertices,expect=weld-or-refuse", o, other, nullptr); }
    { Mesh o = unit; uint32_t a = o.addV(2, 2, 2), b = o.addV(3, 3, 3), c = o.addV(4, 4, 4); o.addT(a, b, c); emit("bad_degenerate_triangle", "malformed", "zero-area-triangle,expect=refuse-or-repair", o, other, nullptr); }
    { Mesh o = unit; for (size_t k = 0; k < o.nt(); k++) std::swap(o.t[3*k+1], o.t[3*k+2]); emit("bad_inverted", "malformed", "inverted,expect=refuse-or-repair", o, other, nullptr); }
    { Mesh o = unit; o.addT(unit.t[0], unit.t[1], unit.t[2]); emit("bad_duplicate_triangle", "malformed", "duplicate-triangle,expect=refuse-or-repair", o, other, nullptr); }
    fclose(g_manifest);

    // sets: many small (container file) and few large
    {
        FILE* f = fopen((g_out + "/sets/many_small_" + std::to_string(many) + ".mshc").c_str(), "wb");
        uint32_t h[2] = {kMagicCont, (uint32_t)many}; fwrite(h, 4, 2, f);
        uint64_t s = 0x9E3779B97F4A7C15ull;
        auto next = [&] { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(s >> 33); };
        Mesh base[4]; for (int i = 0; i < 4; i++) base[i] = makeIcosphere(i + 0, 1.0);   // 20, 80, 320, 1280 triangles
        for (long i = 0; i < many; i++) {
            int kind = 1 + (int)(next() % 3);                                          // 80 to 1280 triangles
            double sc = std::ldexp(1.0, (int)(next() % 3) - 1);
            Mesh m = base[kind]; for (double& x : m.v) x *= sc;
            // sequenced on purpose: the order in which function arguments are evaluated is unspecified in C++ (clang goes left to right, GCC on x86_64 does not)
            double tx = (double)(int)(next() % 64);
            double ty = (double)(int)(next() % 64);
            double tz = (double)(int)(next() % 64);
            m = translated(m, tx, ty, tz);
            writeRecord(f, m);
        }
        fclose(f);
    }
    if (large) for (int s : {8, 9}) writeMesh(g_out + "/sets/large_icosphere_" + std::to_string(s) + ".msh", makeIcosphere(s, 1.0));
    printf("corpus written to %s\n", g_out.c_str());
    return 0;
}
