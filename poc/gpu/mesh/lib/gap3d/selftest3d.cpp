// MP07 checker self-test: known answers on a unit cube and small hand cases. selftest3d_m0 must pass; every selftest3d_mK (K=1..7) must FAIL (planted checker bugs, see DECLARED.txt).
#include "oracle3d.h"
#include <cstdio>
using namespace o3;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("  FAILED: %s\n", msg); fails++; } } while (0)
static c3d::IMesh box(long x0, long y0, long z0, long x1, long y1, long z1) {
    c3d::IMesh m; for (int i = 0; i < 8; i++) m.v.push_back({(i & 1) ? x1 : x0, (i & 2) ? y1 : y0, (i & 4) ? z1 : z0});
    int q[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}}; for (auto& f : q) { m.t.push_back({f[0], f[1], f[2]}); m.t.push_back({f[0], f[2], f[3]}); } return m; }
static mesh::Mesh dbl(const c3d::IMesh& m) { mesh::Mesh o; for (auto& v : m.v) for (int d = 0; d < 3; d++) o.v.push_back((double)v[d]); for (auto& t : m.t) o.addT(t[0], t[1], t[2]); return o; }
int main() {
    IMesh cube = fromCase(box(0, 0, 0, 1, 1, 1), 0);
    CHECK(vol6(cube) == 6, "unit cube 6V == 6"); QV c; CHECK(centroid(cube, c), "centroid exists");
    CHECK(c[0] == Q(1) / 2 && c[1] == Q(1) / 2 && c[2] == Q(1) / 2, "unit cube centroid (1/2,1/2,1/2)");
    IMesh b = fromCase(box(0, 0, 0, 3, 4, 12), 0); Area a = area(b); CHECK(a.exact && a.q == 192, "3x4x12 box area exact 192");
    CHECK(cmpScalar(192.0, a.q, 0).cls == CORRECT, "area 192 correct");
    // distance: point (2,2,2) to the unit cube: D = 3
    PM pm = pointMesh({BigInt(2), BigInt(2), BigInt(2)}, cube); CHECK(pm.d2 == 3, "point-cube D=3");
    CHECK(cmpDist(std::sqrt(3.0), 3, 0).cls == CORRECT, "RN(sqrt 3) is correct");
    CHECK(cmpDist(std::nextafter(std::sqrt(3.0), 2.0), 3, 0).cls != CORRECT, "sqrt3 + 1 ulp must not be correct");
    PM mid = pointMesh({BigInt(1), BigInt(1), BigInt(1)}, fromCase(box(0, 0, 0, 2, 2, 2), 0)); CHECK(mid.pts.size() == 6 && mid.d2 == 1, "cube centre has 6 closest points");
    CHECK(solidDist2(cube, fromCase(box(3, 0, 0, 4, 1, 1), 0)) == 4, "cube gap 2 -> D=4");
    CHECK(solidDist2(cube, fromCase(box(1, 0, 0, 2, 1, 1), 0)) == 0, "touching cubes D=0");
    CHECK(solidDist2(fromCase(box(0, 0, 0, 10, 10, 10), 0), fromCase(box(2, 2, 2, 4, 4, 4), 0)) == 0, "nested D=0");
    // tessellation of the square 0..4 with a square hole 1..3 in the plane z = 5 (outer ccw, hole cw)
    c3d::Case t; t.exp = 0; c3d::Ring o, h; o.dim = h.dim = 3; o.p = {{0, 0, 5}, {4, 0, 5}, {4, 4, 5}, {0, 4, 5}}; h.p = {{1, 1, 5}, {1, 3, 5}, {3, 3, 5}, {3, 1, 5}}; t.rings = {o, h};
    auto P = [](int x, int y) { return std::array<double, 3>{(double)x, (double)y, 5.0}; };
    TriOut good; int T[8][3][2] = {{{0, 0}, {1, 1}, {0, 4}}, {{0, 0}, {4, 0}, {1, 1}}, {{4, 0}, {3, 1}, {1, 1}}, {{4, 0}, {4, 4}, {3, 1}}, {{4, 4}, {3, 3}, {3, 1}}, {{4, 4}, {0, 4}, {3, 3}}, {{0, 4}, {1, 3}, {3, 3}}, {{0, 4}, {1, 1}, {1, 3}}};
    for (auto& tr : T) good.t.push_back({P(tr[0][0], tr[0][1]), P(tr[1][0], tr[1][1]), P(tr[2][0], tr[2][1])});
    CHECK(checkTess(t, good, false).cls == CORRECT, "hand triangulation of the framed square is correct");
    TriOut miss = good; miss.t.pop_back(); CHECK(checkTess(t, miss, false).cls == WRONG, "missing triangle detected");
    TriOut flip = good; std::swap(flip.t[0][1], flip.t[0][2]); CHECK(checkTess(t, flip, false).cls == WRONG, "flipped triangle detected");
    TriOut dup = good; dup.t.push_back(good.t[0]); CHECK(checkTess(t, dup, false).cls == WRONG, "duplicated triangle detected");
    c3d::Case np; np.exp = 0; c3d::Ring q; q.dim = 3; q.p = {{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 3}}; np.rings = {q};     // non-planar input: the plane test must fire
    TriOut two; two.t.push_back({std::array<double, 3>{0, 0, 0}, {4, 0, 0}, {4, 4, 0}}); two.t.push_back({std::array<double, 3>{0, 0, 0}, {4, 4, 0}, {0, 4, 3}});
    CHECK(checkTess(np, two, false).cls == WRONG, "non-planar polygon rejected by the plane test");
    { c3d::Case tj; tj.exp = 0; c3d::Ring q2; q2.dim = 3; q2.p = {{0, 0, 5}, {2, 0, 5}, {4, 0, 5}, {4, 4, 5}, {0, 4, 5}}; tj.rings = {q2};   // square with a collinear vertex on its bottom edge
      TriOut tt; tt.t.push_back({P(0, 0), P(4, 0), P(4, 4)}); tt.t.push_back({P(0, 0), P(4, 4), P(0, 4)});
      Verdict vt = checkTess(tj, tt, false); CHECK(vt.cls == CORRECT && vt.msg.find("T-junction") != std::string::npos, "T-junction triangulation is a valid cover and the T-junction is reported"); }
    // Minkowski: cube (+) point(0,0,0) is the cube; a cube shrunk to 0..1 against a 0..2 summand is not
    c3d::Case m; m.exp = 0; m.mesh["A"] = box(0, 0, 0, 1, 1, 1); m.pts["B"] = {{0, 0, 0}};
    CHECK(checkMinkowski(m, dbl(box(0, 0, 0, 1, 1, 1)), false).cls == CORRECT, "cube + point = cube");
    m.pts["B"] = {{0, 0, 0}, {1, 0, 0}}; CHECK(checkMinkowski(m, dbl(box(0, 0, 0, 1, 1, 1)), false).cls == WRONG, "too small hull detected");
    CHECK(checkMinkowski(m, dbl(box(0, 0, 0, 2, 1, 1)), false).cls == CORRECT, "cube + segment = box");
    printf("%s (mutant %d): %d failed checks\n", fails ? "FAIL" : "PASS", MUTANT, fails); return fails ? 1 : 0;
}
