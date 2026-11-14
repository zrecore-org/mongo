// MP08b 3D Delaunay oracle (cpp_int / int128 exact, independent of Geogram). Checks: every tet non-degenerate; exact in-sphere: no point strictly inside any circumsphere;
// every point is a vertex; tiling: for sampled points (exact, scaled x2) strictly inside the convex hull (hull = all supporting planes by brute force over point triples)
// exactly one tet strictly contains it, strictly outside none; each triangular face is used by <= 2 tets, on opposite sides. usage: oracle3d <case.h3d> <result>
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
typedef boost::multiprecision::cpp_int Z; typedef long long i64; typedef __int128 i128; typedef std::array<i64, 3> P3;
static int sg(i128 v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static int o3(const P3& a, const P3& b, const P3& c, const P3& d) { i128 bx = b[0] - a[0], by = b[1] - a[1], bz = b[2] - a[2], cx = c[0] - a[0], cy = c[1] - a[1], cz = c[2] - a[2], dx = d[0] - a[0], dy = d[1] - a[1], dz = d[2] - a[2];
    return sg(bx * (cy * dz - cz * dy) - by * (cx * dz - cz * dx) + bz * (cx * dy - cy * dx)); }
static int fail(const std::string& m) { printf("WRONG %s\n", m.c_str()); return 0; }
// insphere sign relative to tet orientation: >0 strictly inside circumsphere (for positively oriented tet by o3)
static int insphere(const P3& a, const P3& b, const P3& c, const P3& d, const P3& e) { Z m[4][4]; const P3* t[4] = {&a, &b, &c, &d};
    for (int i = 0; i < 4; i++) { Z x = (*t[i])[0] - e[0], y = (*t[i])[1] - e[1], z = (*t[i])[2] - e[2]; m[i][0] = x; m[i][1] = y; m[i][2] = z; m[i][3] = x * x + y * y + z * z; }
    auto det3 = [&](int r0, int r1, int r2, int c0, int c1, int c2) { return m[r0][c0] * (m[r1][c1] * m[r2][c2] - m[r1][c2] * m[r2][c1]) - m[r0][c1] * (m[r1][c0] * m[r2][c2] - m[r1][c2] * m[r2][c0]) + m[r0][c2] * (m[r1][c0] * m[r2][c1] - m[r1][c1] * m[r2][c0]); };
    Z det = 0; for (int j = 0; j < 4; j++) { int cs[3], k = 0; for (int q = 0; q < 4; q++) if (q != j) cs[k++] = q; Z mn = det3(1, 2, 3, cs[0], cs[1], cs[2]); Z term = m[0][j] * mn; if ((j & 1)) det -= term; else det += term; }
    // det sign convention: for a positively oriented (o3 > 0) tet, a point inside the sphere gives det < 0 with this expansion -> normalise by orientation below
    return det > 0 ? 1 : det < 0 ? -1 : 0; }
int main(int argc, char** argv) { if (argc < 3) return 2; std::ifstream in(argv[1]); std::string tok; in >> tok; std::vector<P3> pts; size_t n = 0; std::string fam;
    while (in >> tok) { if (tok == "points") { in >> n; pts.resize(n); for (auto& p : pts) in >> p[0] >> p[1] >> p[2]; } else if (tok == "name") in >> tok; else if (tok == "family") in >> fam; }
    std::ifstream rf(argv[2]); rf >> tok; std::string st; rf >> st; if (st != "OK") { printf("REFUSED %s\n", st.c_str()); return 0; } std::vector<std::array<int, 4>> tets; while (rf >> tok) { if (tok == "tet") { std::array<int, 4> t; rf >> t[0] >> t[1] >> t[2] >> t[3]; tets.push_back(t); } }
    if (tets.empty()) return fail("no tetrahedra");
    std::set<int> used; std::map<std::array<int, 3>, std::vector<std::pair<int, int>>> faces;   // sorted face -> (tet, opposite vertex)
    for (size_t k = 0; k < tets.size(); k++) { auto& t = tets[k]; for (int v : t) if (v < 0 || (size_t)v >= n) return fail("bad index"); for (int a = 0; a < 4; a++) for (int b = a + 1; b < 4; b++) if (t[a] == t[b]) return fail("repeated vertex");
        int o = o3(pts[t[0]], pts[t[1]], pts[t[2]], pts[t[3]]); if (o == 0) return fail("degenerate (flat) tetrahedron"); if (o < 0) std::swap(t[2], t[3]);
        for (int v : t) used.insert(v);
        for (size_t q = 0; q < n; q++) { if ((int)q == t[0] || (int)q == t[1] || (int)q == t[2] || (int)q == t[3]) continue; int s = insphere(pts[t[0]], pts[t[1]], pts[t[2]], pts[t[3]], pts[q]); // sign convention verified: for a tet with o3 > 0, a point strictly inside the circumsphere gives det < 0 (checked by hand on (0,0,0),(4,0,0),(0,4,0),(0,0,4) with (1,1,1))
            if (s < 0) return fail(std::string("point strictly inside a circumsphere (tet ") + std::to_string(k) + ")"); }
        for (int j = 0; j < 4; j++) { std::array<int, 3> f; int c = 0; for (int q = 0; q < 4; q++) if (q != j) f[c++] = t[q]; std::sort(f.begin(), f.end()); faces[f].push_back({(int)k, t[j]}); } }
    if (used.size() != n) return fail("a point is not a vertex of any tetrahedron");
    for (auto& fc : faces) { if (fc.second.size() > 2) return fail("face used by more than 2 tets"); if (fc.second.size() == 2) { int s0 = o3(pts[fc.first[0]], pts[fc.first[1]], pts[fc.first[2]], pts[fc.second[0].second]), s1 = o3(pts[fc.first[0]], pts[fc.first[1]], pts[fc.first[2]], pts[fc.second[1].second]); if (s0 * s1 >= 0) return fail("two tets on the same side of a shared face (overlap)"); } }
    for (auto& fc : faces) if (fc.second.size() == 1) { int pos = 0, neg = 0; for (size_t q = 0; q < n; q++) { int o = o3(pts[fc.first[0]], pts[fc.first[1]], pts[fc.first[2]], pts[q]); if (o > 0) pos++; else if (o < 0) neg++; } if (pos && neg) return fail("unmatched face is not on the convex hull (hole in the tetrahedralisation)"); }
    // supporting planes by brute force
    struct Pl { int i, j, k, s; }; std::vector<Pl> pl; for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) for (size_t k = j + 1; k < n; k++) { int pos = 0, neg = 0; for (size_t q = 0; q < n; q++) { int o = o3(pts[i], pts[j], pts[k], pts[q]); if (o > 0) pos++; else if (o < 0) neg++; } if (pos == 0 && neg == 0) continue; if (pos == 0) pl.push_back({(int)i, (int)j, (int)k, -1}); else if (neg == 0) pl.push_back({(int)i, (int)j, (int)k, 1}); }
    if (pl.empty()) return fail("points coplanar");
    i64 lo[3] = {1 << 30, 1 << 30, 1 << 30}, hi[3] = {-(1 << 30), -(1 << 30), -(1 << 30)}; for (auto& p : pts) for (int a = 0; a < 3; a++) { lo[a] = std::min(lo[a], p[a]); hi[a] = std::max(hi[a], p[a]); }
    uint64_t s = 0x33445566ULL; auto rnd = [&](i64 a, i64 b) { uint64_t z = (s += 0x9E3779B97F4A7C15ULL); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; z = z ^ (z >> 31); return a + (i64)(z % (uint64_t)(b - a + 1)); };
    std::vector<P3> sp; for (int it = 0; it < 1500; it++) { i64 x = rnd(2 * lo[0] - 2, 2 * hi[0] + 2); i64 y = rnd(2 * lo[1] - 2, 2 * hi[1] + 2); i64 z = rnd(2 * lo[2] - 2, 2 * hi[2] + 2); sp.push_back({x, y, z}); }
    std::vector<P3> p2 = pts; for (auto& p : p2) for (int a = 0; a < 3; a++) p[a] *= 2; int nin = 0, nout = 0, amb = 0;
    for (auto& q : sp) { int side = 1; bool any0 = false; for (auto& p : pl) { int o = o3(p2[p.i], p2[p.j], p2[p.k], q) * p.s; if (o < 0) { side = -1; break; } if (o == 0) any0 = true; }
        if (side == 1 && any0) { amb++; continue; }
        int cnt = 0; bool tch = false; for (auto& t : tets) { int a = o3(p2[t[1]], p2[t[2]], p2[t[3]], q) * o3(p2[t[1]], p2[t[2]], p2[t[3]], p2[t[0]]), b = o3(p2[t[0]], p2[t[2]], p2[t[3]], q) * o3(p2[t[0]], p2[t[2]], p2[t[3]], p2[t[1]]), c = o3(p2[t[0]], p2[t[1]], p2[t[3]], q) * o3(p2[t[0]], p2[t[1]], p2[t[3]], p2[t[2]]), d = o3(p2[t[0]], p2[t[1]], p2[t[2]], q) * o3(p2[t[0]], p2[t[1]], p2[t[2]], p2[t[3]]);
            if (a > 0 && b > 0 && c > 0 && d > 0) cnt++; else if (a >= 0 && b >= 0 && c >= 0 && d >= 0) tch = true; }
        if (side == -1) { nout++; if (cnt != 0 || tch) { if (cnt != 0) return fail("sample outside the hull lies inside a tet"); } }
        else { nin++; if (tch && cnt == 0) { amb++; nin--; continue; } if (cnt != 1) return fail("sample strictly inside the hull is strictly inside " + std::to_string(cnt) + " tets (gap or overlap)"); } }
    printf("CORRECT tets=%zu inside=%d outside=%d skipped=%d\n", tets.size(), nin, nout, amb); return 0; }
