// MP07 shared case format (".c3d", text, integer coordinates; real coordinate = int * 2^exp). Header only.
//   C3D1 / name N / kind K / cls C / exp E / param KEY v... / ring DIM n c0 c1 ... / mesh NAME nv nt (then nv lines "x y z", nt lines "a b c") / pts NAME n (n lines "x y z") / pt x y z
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
namespace c3d {
typedef long long i64;
typedef std::array<i64, 3> V3;
struct IMesh { std::vector<V3> v; std::vector<std::array<int, 3>> t; };
struct Ring { int dim = 2; std::vector<std::array<i64, 3>> p; };
struct Case {
    std::string name, kind, cls; int exp = 0;
    std::map<std::string, std::vector<i64>> param;
    std::vector<Ring> rings;
    std::map<std::string, IMesh> mesh;
    std::map<std::string, std::vector<V3>> pts;
    bool hasPt = false; V3 pt{0, 0, 0};
    i64 P(const std::string& k, size_t i = 0, i64 d = 0) const { auto it = param.find(k); return it == param.end() || i >= it->second.size() ? d : it->second[i]; }
};
inline void write(FILE* f, const Case& c) {
    fprintf(f, "C3D1\nname %s\nkind %s\ncls %s\nexp %d\n", c.name.c_str(), c.kind.c_str(), c.cls.c_str(), c.exp);
    for (auto& kv : c.param) { fprintf(f, "param %s", kv.first.c_str()); for (i64 x : kv.second) fprintf(f, " %lld", x); fprintf(f, "\n"); }
    for (auto& r : c.rings) { fprintf(f, "ring %d %zu", r.dim, r.p.size()); for (auto& q : r.p) for (int d = 0; d < r.dim; d++) fprintf(f, " %lld", q[d]); fprintf(f, "\n"); }
    for (auto& kv : c.mesh) { fprintf(f, "mesh %s %zu %zu\n", kv.first.c_str(), kv.second.v.size(), kv.second.t.size());
        for (auto& v : kv.second.v) fprintf(f, "%lld %lld %lld\n", v[0], v[1], v[2]); for (auto& t : kv.second.t) fprintf(f, "%d %d %d\n", t[0], t[1], t[2]); }
    for (auto& kv : c.pts) { fprintf(f, "pts %s %zu\n", kv.first.c_str(), kv.second.size()); for (auto& v : kv.second) fprintf(f, "%lld %lld %lld\n", v[0], v[1], v[2]); }
    if (c.hasPt) fprintf(f, "pt %lld %lld %lld\n", c.pt[0], c.pt[1], c.pt[2]);
}
inline bool read(const std::string& path, Case& c) {
    std::ifstream f(path); if (!f) return false; std::string tok; f >> tok; if (tok != "C3D1") return false;
    while (f >> tok) {
        if (tok == "name") f >> c.name; else if (tok == "kind") f >> c.kind; else if (tok == "cls") f >> c.cls; else if (tok == "exp") f >> c.exp;
        else if (tok == "param") { std::string line; std::getline(f, line); std::istringstream is(line); std::string k; is >> k; i64 x; while (is >> x) c.param[k].push_back(x); }
        else if (tok == "ring") { Ring r; size_t n; f >> r.dim >> n; r.p.resize(n); for (auto& q : r.p) { q = {0, 0, 0}; for (int d = 0; d < r.dim; d++) f >> q[d]; } c.rings.push_back(r); }
        else if (tok == "mesh") { std::string nm; size_t nv, nt; f >> nm >> nv >> nt; IMesh m; m.v.resize(nv); m.t.resize(nt); for (auto& v : m.v) f >> v[0] >> v[1] >> v[2]; for (auto& t : m.t) f >> t[0] >> t[1] >> t[2]; c.mesh[nm] = m; }
        else if (tok == "pts") { std::string nm; size_t n; f >> nm >> n; std::vector<V3> v(n); for (auto& q : v) f >> q[0] >> q[1] >> q[2]; c.pts[nm] = v; }
        else if (tok == "pt") { f >> c.pt[0] >> c.pt[1] >> c.pt[2]; c.hasPt = true; }
    }
    return true;
}
}  // namespace c3d
