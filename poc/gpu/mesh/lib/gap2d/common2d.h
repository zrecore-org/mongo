// MP08a shared 2D case format (".p2d", text, integer coordinates) and result format (".res"). Header-only, no library dependencies.
//   P2D1 / name N / family F / valid 0|1 / offset D / then 'set NP' blocks: each 'poly NR', each ring 'n x0 y0 x1 y1 ...' (implicitly closed).
//   A multi-polygon is a list of polygons; a polygon is its outer ring (counter-clockwise) then its holes (clockwise).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
namespace p2d {
typedef long long i64;
struct P { i64 x, y; };
typedef std::vector<P> Ring;
typedef std::vector<Ring> Poly;      // outer first, then holes
typedef std::vector<Poly> Set;       // multi-polygon
struct Case { std::string name, family; int valid = 1; i64 offset = 0; std::vector<Set> sets; };

inline bool readCase(const std::string& path, Case& c) {
    std::ifstream f(path); if (!f) return false;
    std::string tok; f >> tok; if (tok != "P2D1") return false;
    for (;;) {
        if (!(f >> tok)) break;
        if (tok == "name") f >> c.name; else if (tok == "family") f >> c.family; else if (tok == "valid") f >> c.valid;
        else if (tok == "offset") f >> c.offset;
        else if (tok == "set") {
            size_t np; f >> np; Set s(np);
            for (auto& poly : s) { f >> tok; size_t nr; f >> nr; poly.resize(nr);
                for (auto& r : poly) { size_t n; f >> n; r.resize(n); for (auto& p : r) f >> p.x >> p.y; } }
            c.sets.push_back(s);
        }
    }
    return true;
}
inline std::vector<Ring> flatten(const Set& s) { std::vector<Ring> r; for (auto& p : s) for (auto& q : p) r.push_back(q); return r; }

// Result file: "RES1 <STATUS> [message]" then "T <ns>" lines (one per repetition), then payload lines:
//   "ring n x0 y0 ..." (doubles, %.17g, implicitly closed), "NUM v", "BOOL 0|1".
struct Res {
    std::string status, msg; std::vector<double> ns; std::vector<std::vector<std::pair<double,double>>> rings; bool hasNum = false; double num = 0; bool hasBool = false; int b = 0;
};
inline void writeRes(const std::string& path, const std::string& status, const std::string& msg, const std::vector<double>& ns,
                     const std::vector<std::vector<std::pair<double,double>>>& rings, const double* num = nullptr, const int* b = nullptr) {
    FILE* f = fopen(path.c_str(), "w"); if (!f) { perror("open res"); exit(4); }
    fprintf(f, "RES1 %s %s\n", status.c_str(), msg.c_str());
    for (double t : ns) fprintf(f, "T %.0f\n", t);
    if (num) fprintf(f, "NUM %.17g\n", *num);
    if (b) fprintf(f, "BOOL %d\n", *b);
    for (auto& r : rings) { fprintf(f, "ring %zu", r.size()); for (auto& p : r) fprintf(f, " %.17g %.17g", p.first, p.second); fprintf(f, "\n"); }
    fclose(f);
}
inline bool readRes(const std::string& path, Res& r) {
    std::ifstream f(path); if (!f) return false;
    std::string line; if (!std::getline(f, line)) return false;
    std::istringstream h(line); std::string m; h >> m >> r.status; std::getline(h, r.msg);
    if (m != "RES1") return false;
    while (std::getline(f, line)) {
        std::istringstream is(line); std::string k; is >> k;
        if (k == "T") { double t; is >> t; r.ns.push_back(t); }
        else if (k == "NUM") { is >> r.num; r.hasNum = true; }
        else if (k == "BOOL") { is >> r.b; r.hasBool = true; }
        else if (k == "ring") { size_t n; is >> n; std::vector<std::pair<double,double>> v(n); for (auto& p : v) { std::string a, b2; is >> a >> b2; p.first = strtod(a.c_str(), nullptr); p.second = strtod(b2.c_str(), nullptr); } r.rings.push_back(v); }
    }
    return true;
}
}  // namespace p2d
