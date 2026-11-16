// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org and exactmesh contributors
// MP14d independent checks for meshcontains (review findings from aalbino-1b, 2026-11-16). The oracle in test_contains_core.cpp is the same algorithm, so this adds:
//  (1) analytic truth (unit cube; integer octahedron and tetrahedron with exact face centroids, vertices, edges, plane extensions),
//  (2) metamorphic checks: axis permutations, axis negation (mirror) and power-of-two scaling must map every verdict identically (a +x ray becomes a +y, +z or -x ray),
//  (3) concave and nested meshes (torus, sphere with an inner cavity) against the hardened exactmesh oracle, BVH path == all-triangles path,
//  (4) predicate-level: orient2dSign/orient3dSign (filter + fallback) against an exact-only evaluation on millions of near-degenerate inputs,
//  (5) coverage: counters of the exact fallbacks, both tie-break terms, bbox skips, coplanar true/false must be non-zero in the unmutated build.
// Mutants (-DMESHCONTAINS_MUT=n, see the header) must make this program exit non-zero.
#define MESHCONTAINS_COUNTERS
#include "meshcontains_core.h"
#include "cpu/pip_hardened.h"
#include "gpu/pip_cases.h"
#include <cstdio>
#include <functional>
extern "C" void exactinit(void);

static long g_fail = 0;
static void expect(bool ok, const std::string& what) { if (!ok) { g_fail++; std::printf("FAIL %s\n", what.c_str()); } }

static Mesh polyhedron(const std::vector<std::array<double, 3>>& V, const std::vector<std::array<uint32_t, 3>>& T) {
    Mesh m; for (auto& v : V) m.addV(v[0], v[1], v[2]); for (auto& t : T) m.addT(t[0], t[1], t[2]); return m;
}
static Mesh octahedron(double r) { return polyhedron({{r,0,0},{-r,0,0},{0,r,0},{0,-r,0},{0,0,r},{0,0,-r}}, {{0,2,4},{2,1,4},{1,3,4},{3,0,4},{2,0,5},{1,2,5},{3,1,5},{0,3,5}}); }
static Mesh tetra() { return polyhedron({{0,0,0},{6,0,0},{0,6,0},{0,0,6}}, {{0,2,1},{0,1,3},{0,3,2},{1,2,3}}); }
static Mesh torus(int nu, int nv, double R, double r) {
    Mesh m; const double PI = 3.14159265358979323846;
    for (int i = 0; i < nu; i++) for (int j = 0; j < nv; j++) { double u = 2 * PI * i / nu, v = 2 * PI * j / nv; m.addV((R + r * std::cos(v)) * std::cos(u), (R + r * std::cos(v)) * std::sin(u), r * std::sin(v)); }
    auto V = [&](int i, int j) { return (uint32_t)((i % nu) * nv + (j % nv)); };
    for (int i = 0; i < nu; i++) for (int j = 0; j < nv; j++) { m.addT(V(i, j), V(i + 1, j), V(i + 1, j + 1)); m.addT(V(i, j), V(i + 1, j + 1), V(i, j + 1)); }
    return m;
}
static Mesh nested() {   // sphere with an inner spherical cavity (inner surface reversed): two closed surfaces in one mesh
    Mesh a = uvsphere(10, 12, 2.0, 0, 0, 0), b = uvsphere(8, 10, 0.75, 0.2, 0.1, -0.1);
    const uint32_t off = (uint32_t)a.nv(); for (size_t i = 0; i < b.nv(); i++) a.addV(b.v[3*i], b.v[3*i+1], b.v[3*i+2]);
    for (size_t k = 0; k < b.nt(); k++) a.addT(off + b.t[3*k], off + b.t[3*k+2], off + b.t[3*k+1]);
    return a;
}
using Pts = std::vector<double>;
static void add(Pts& q, double x, double y, double z) { q.push_back(x); q.push_back(y); q.push_back(z); }

// queries for a mesh: random, vertices, edge midpoints, centroids, rays through edge interiors, plane extensions, lattice
static Pts queries(const Mesh& m, uint64_t seed, int lattice) {
    Pts q; Rng r(seed); double lo[3] = {1e300,1e300,1e300}, hi[3] = {-1e300,-1e300,-1e300};
    for (size_t i = 0; i < m.nv(); i++) for (int d = 0; d < 3; d++) { lo[d] = std::min(lo[d], m.v[3*i+d]); hi[d] = std::max(hi[d], m.v[3*i+d]); }
    for (int i = 0; i < 600; i++) add(q, lo[0]+(hi[0]-lo[0])*(1.3*r.u01()-0.15), lo[1]+(hi[1]-lo[1])*(1.3*r.u01()-0.15), lo[2]+(hi[2]-lo[2])*(1.3*r.u01()-0.15));
    const double sx = hi[0] - lo[0];
    for (size_t i = 0; i < m.nv(); i++) { const double* a = &m.v[3*i]; add(q, a[0], a[1], a[2]); add(q, a[0] - sx, a[1], a[2]); add(q, a[0] + sx, a[1], a[2]); }
    for (size_t k = 0; k < m.nt(); k++) {
        const double *a = &m.v[3*(size_t)m.t[3*k]], *b = &m.v[3*(size_t)m.t[3*k+1]], *c = &m.v[3*(size_t)m.t[3*k+2]];
        add(q, (a[0]+b[0])/2, (a[1]+b[1])/2, (a[2]+b[2])/2);                                   // edge midpoint (exactly on the edge for dyadic input)
        add(q, (a[0]+b[0]+c[0])/3, (a[1]+b[1]+c[1])/3, (a[2]+b[2]+c[2])/3);                    // centroid
        add(q, (a[0]+b[0])/2 - sx, (a[1]+b[1])/2, (a[2]+b[2])/2); add(q, (a[0]+b[0])/2 + sx, (a[1]+b[1])/2, (a[2]+b[2])/2);   // rays through an edge interior
        add(q, a[0] + 2*(b[0]-a[0]) - (c[0]-a[0]), a[1] + 2*(b[1]-a[1]) - (c[1]-a[1]), a[2] + 2*(b[2]-a[2]) - (c[2]-a[2]));   // in the plane of the face, outside it
        add(q, a[0] + 3*(b[0]-a[0]), a[1] + 3*(b[1]-a[1]), a[2] + 3*(b[2]-a[2]));              // on the line of an edge, beyond it
    }
    for (int x = -lattice; x <= lattice; x++) for (int y = -lattice; y <= lattice; y++) for (int z = -lattice; z <= lattice; z++) add(q, x, y, z);
    return q;
}
static std::vector<uint8_t> verdicts(const Mesh& m, const Pts& q, bool bvh) {
    std::vector<uint8_t> out(q.size() / 3); meshcontains::Bvh t; std::vector<uint32_t> sc; if (bvh) t.build(m.v, m.t);
    for (size_t i = 0; i < out.size(); i++) out[i] = bvh ? meshcontains::classifyBvh(m.v, m.t, t, &q[3*i], &sc) : meshcontains::classify(m.v, m.t, &q[3*i]);
    return out;
}
// exact-only predicates for the predicate-level test (independent of the filter)
static int exactO2(const double* a, const double* b, const double* c) {
    double in[6] = {a[0], a[1], b[0], b[1], c[0], c[1]}; meshcontains::BigInt v[6]; meshcontains::toInts(in, 6, v);
    return meshcontains::BigInt((v[0]-v[4])*(v[3]-v[5]) - (v[1]-v[5])*(v[2]-v[4])).sign();
}
static int exactO3(const double* a, const double* b, const double* c, const double* d) {
    double in[12] = {a[0],a[1],a[2],b[0],b[1],b[2],c[0],c[1],c[2],d[0],d[1],d[2]}; meshcontains::BigInt v[12]; meshcontains::toInts(in, 12, v);
    using B = meshcontains::BigInt; B x0=v[0]-v[9], y0=v[1]-v[10], z0=v[2]-v[11], x1=v[3]-v[9], y1=v[4]-v[10], z1=v[5]-v[11], x2=v[6]-v[9], y2=v[7]-v[10], z2=v[8]-v[11];
    return B(x0*(y1*z2-z1*y2) + y0*(z1*x2-x1*z2) + z0*(x1*y2-y1*x2)).sign();
}

int main() {
    exactinit();
    using namespace meshcontains;
    // ---------- (1) analytic truth
    { Mesh cube = block(1); Pts q; for (int x = -2; x <= 6; x++) for (int y = -2; y <= 6; y++) for (int z = -2; z <= 6; z++) add(q, x / 4.0, y / 4.0, z / 4.0);
      auto v = verdicts(cube, q, false); long bad = 0, on = 0;
      for (size_t i = 0; i < v.size(); i++) { double x = q[3*i], y = q[3*i+1], z = q[3*i+2]; bool in = x > 0 && x < 1 && y > 0 && y < 1 && z > 0 && z < 1, out = x < 0 || x > 1 || y < 0 || y > 1 || z < 0 || z > 1; int want = in ? 1 : out ? 0 : 2; on += want == 2; bad += v[i] != want; }
      expect(bad == 0 && on > 0, "analytic unit cube, quarter lattice: wrong=" + std::to_string(bad)); }
    { Mesh oc = octahedron(6); Pts q; for (int x = -8; x <= 8; x++) for (int y = -8; y <= 8; y++) for (int z = -8; z <= 8; z++) add(q, x, y, z);
      auto v = verdicts(oc, q, false); long bad = 0, on = 0;
      for (size_t i = 0; i < v.size(); i++) { int s = (int)(std::fabs(q[3*i]) + std::fabs(q[3*i+1]) + std::fabs(q[3*i+2])); int want = s < 6 ? 1 : s == 6 ? 2 : 0; on += want == 2; bad += v[i] != want; }
      expect(bad == 0 && on > 0, "analytic octahedron |x|+|y|+|z|<6 (oblique faces, exact ON), wrong=" + std::to_string(bad)); }
    { Mesh tt = tetra(); Pts q; for (int x = -2; x <= 8; x++) for (int y = -2; y <= 8; y++) for (int z = -2; z <= 8; z++) add(q, x, y, z);
      auto v = verdicts(tt, q, false); long bad = 0, on = 0;
      for (size_t i = 0; i < v.size(); i++) { int x = (int)q[3*i], y = (int)q[3*i+1], z = (int)q[3*i+2]; bool anyNeg = x < 0 || y < 0 || z < 0; int s = x + y + z; int want = anyNeg || s > 6 ? 0 : (x == 0 || y == 0 || z == 0 || s == 6) ? 2 : 1; on += want == 2; bad += v[i] != want; }
      expect(bad == 0 && on > 0, "analytic tetrahedron x,y,z>=0, x+y+z<=6 (exact ON on 4 oblique/axis faces), wrong=" + std::to_string(bad)); }
    // ---------- (2) and (3): per mesh, against the hardened oracle, BVH == all-triangles, metamorphic
    struct Item { std::string name; Mesh m; int lattice; };
    std::vector<Item> items; items.push_back({"octahedron r=6", octahedron(6), 7}); items.push_back({"tetrahedron 6", tetra(), 7}); items.push_back({"torus 16x10", torus(16, 10, 2.0, 0.75), 0});
    { Mesh d = octahedron(6); const uint32_t i0 = d.addV(0, 0, 0), i1 = d.addV(0, 0, 1), i2 = d.addV(0, 0, 2); d.addT(i0, i1, i2); d.addT(i0, i2, i1); items.push_back({"octahedron + two zero-area slivers (collinear vertices)", d, 7}); }
    items.push_back({"nested spheres (cavity)", nested(), 0}); items.push_back({"uv sphere 10x14", uvsphere(10, 14, 1.0, 0.25, 0, -0.125), 0});
    items.push_back({"block 3", block(3), 4}); items.push_back({"mixed scale (x,y 2^-170, z 2^150) block 2", mapped(block(2), 0, 0, 0, 0), 3});
    { Mesh& m = items.back().m; for (size_t i = 0; i < m.nv(); i++) { m.v[3*i] = std::ldexp(m.v[3*i], -170); m.v[3*i+1] = std::ldexp(m.v[3*i+1], -170); m.v[3*i+2] = std::ldexp(m.v[3*i+2], 150); } }
    long totalQ = 0, totalMeta = 0; long nIn = 0, nOut = 0, nOn = 0; int mi = 0;
    for (auto& it : items) {
        Pts q = queries(it.m, 1000 + mi++, it.lattice);
        if (it.name.rfind("mixed", 0) == 0) { for (size_t i = 0; i < q.size() / 3; i++) { q[3*i] = std::ldexp(q[3*i], -170); q[3*i+1] = std::ldexp(q[3*i+1], -170); q[3*i+2] = std::ldexp(q[3*i+2], 150); } }
        auto mine = verdicts(it.m, q, false), viaBvh = verdicts(it.m, q, true);
        long dBvh = 0, dOr = 0;
        for (size_t i = 0; i < mine.size(); i++) {
            dBvh += mine[i] != viaBvh[i];
            mesh::Pip o = mesh::classifyPointHardened(it.m, &q[3*i]); uint8_t w = o.boundary ? 2 : o.inside ? 1 : 0; dOr += mine[i] != w;
            (mine[i] == 2 ? nOn : mine[i] == 1 ? nIn : nOut)++;
        }
        expect(dBvh == 0, it.name + ": BVH path equals all-triangles path, diffs=" + std::to_string(dBvh));
        expect(dOr == 0, it.name + ": equals the hardened oracle, diffs=" + std::to_string(dOr));
        totalQ += (long)mine.size();
        // metamorphic: exact maps of mesh and points; every verdict must be unchanged
        auto xf = [&](const char* name, const std::function<void(double*)>& f) {
            Mesh t = it.m; for (size_t i = 0; i < t.nv(); i++) f(&t.v[3*i]); Pts tq = q; for (size_t i = 0; i < tq.size() / 3; i++) f(&tq[3*i]);
            auto tv = verdicts(t, tq, false); long d = 0; for (size_t i = 0; i < tv.size(); i++) d += tv[i] != mine[i]; totalMeta += (long)tv.size();
            expect(d == 0, it.name + ": metamorphic " + name + ", diffs=" + std::to_string(d)); };
        xf("permute (x,y,z)->(y,z,x)", [](double* p) { double a = p[0], b = p[1], c = p[2]; p[0] = b; p[1] = c; p[2] = a; });
        xf("permute (x,y,z)->(z,x,y)", [](double* p) { double a = p[0], b = p[1], c = p[2]; p[0] = c; p[1] = a; p[2] = b; });
        xf("swap x,z", [](double* p) { std::swap(p[0], p[2]); });
        xf("negate x (ray becomes -x)", [](double* p) { p[0] = -p[0]; });
        xf("negate y", [](double* p) { p[1] = -p[1]; });
        xf("negate z", [](double* p) { p[2] = -p[2]; });
        if (it.name.rfind("mixed", 0) != 0) xf("scale by 2^7", [](double* p) { for (int d = 0; d < 3; d++) p[d] = std::ldexp(p[d], 7); });
    }
    std::printf("meshes=%zu queries=%ld (OUT %ld IN %ld ON %ld) metamorphic comparisons=%ld\n", items.size(), totalQ, nOut, nIn, nOn, totalMeta);
    expect(nIn > 0 && nOut > 0 && nOn > 0, "all three verdict classes occur");
    // ---------- (4) predicate level: filter+fallback versus exact-only on near-degenerate inputs
    { Rng r(4242); long n2 = 0, n3 = 0, bad2 = 0, bad3 = 0;
      auto jig = [&](double x) { if (r.pick(2)) return x; return ulps(x, r.pick(9) - 4); };
      for (int it2 = 0; it2 < 1500000; it2++) {
          double a[3], b[3], c[3], d[3]; const double sc = std::ldexp(1.0, r.pick(60) - 30);
          for (int k = 0; k < 3; k++) { a[k] = (r.pick(17) - 8) * sc; b[k] = (r.pick(17) - 8) * sc; c[k] = (r.pick(17) - 8) * sc; }
          const int i = r.pick(5) - 2, j = r.pick(5) - 2;
          for (int k = 0; k < 3; k++) d[k] = jig(a[k] + i * (b[k] - a[k]) + j * (c[k] - a[k]));          // (near-)coplanar with abc
          for (int k = 0; k < 3; k++) { a[k] = jig(a[k]); }
          bad3 += orient3dSign(a, b, c, d) != exactO3(a, b, c, d); n3++;
          const double a2[2] = {a[0], a[1]}, b2[2] = {b[0], b[1]}, c2[2] = {jig(a[0] + i * (b[0] - a[0])), jig(a[1] + i * (b[1] - a[1]))};   // near-collinear
          bad2 += orient2dSign(a2, b2, c2) != exactO2(a2, b2, c2); n2++;
      }
      expect(bad2 == 0 && bad3 == 0, "predicates: orient2d wrong " + std::to_string(bad2) + "/" + std::to_string(n2) + ", orient3d wrong " + std::to_string(bad3) + "/" + std::to_string(n3)); }
    // ---------- (5) coverage (unmutated build only; the counters are exact event counts)
    const Counters& c = g_cnt;
    std::printf("coverage: o2 filter %ld exact %ld | o3 filter %ld exact %ld | tie first %ld second %ld none %ld | bbox skip %ld | coplanar true %ld false %ld\n",
                c.o2Filter, c.o2Exact, c.o3Filter, c.o3Exact, c.tieFirst, c.tieSecond, c.tieNone, c.bboxSkip, c.coplanarTrue, c.coplanarFalse);
    if (MESHCONTAINS_MUT == 0) expect(c.o2Filter > 0 && c.o2Exact > 0 && c.o3Filter > 0 && c.o3Exact > 0 && c.tieFirst > 0 && c.tieSecond > 0 && c.bboxSkip > 0 && c.coplanarTrue > 0 && c.coplanarFalse > 0, "coverage counters all non-zero");
    std::printf("FAILS %ld\n", g_fail);
    return g_fail ? 1 : 0;
}
