// MP08b shared case format (".h2d", text, integer coordinates) and result format. Header-only.
//   H2D1 / name N / family F / mode M (hm|mono|vis|alpha|repair) / rule R (repair: 0 even-odd, 1 non-zero) / alpha A B (alpha = A/B, squared radius)
//   point X Y (visibility observer) / rings K then K lines 'n x0 y0 ...' (implicitly closed; polygon: outer CCW then holes CW) / points M then M lines 'x y'
// Result: "RES1 STATUS" then lines: 'ring n x y ...' (integer), 'rrat n xn xd yn yd ...' (rational vertices, long long), 'tri a b c' index triples
//   'ftri' = full triangulation, 'btri' = selected triangles, 'bedge a b' = boundary edge.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
namespace h2d {
typedef long long i64;
typedef __int128 i128;
struct P { i64 x, y; };
typedef std::vector<P> Ring;
struct Case { std::string name, family, mode; int rule = 0; i64 an = 0, ad = 1; P pt{0, 0}; std::vector<Ring> rings; std::vector<P> points; };
inline bool readCase(const std::string& path, Case& c) {
    std::ifstream f(path); if (!f) return false; std::string tok; f >> tok; if (tok != "H2D1") return false;
    while (f >> tok) {
        if (tok == "name") f >> c.name; else if (tok == "family") f >> c.family; else if (tok == "mode") f >> c.mode; else if (tok == "rule") f >> c.rule;
        else if (tok == "alpha") f >> c.an >> c.ad; else if (tok == "point") f >> c.pt.x >> c.pt.y;
        else if (tok == "rings") { size_t k; f >> k; c.rings.resize(k); for (auto& r : c.rings) { size_t n; f >> n; r.resize(n); for (auto& p : r) f >> p.x >> p.y; } }
        else if (tok == "points") { size_t m; f >> m; c.points.resize(m); for (auto& p : c.points) f >> p.x >> p.y; }
    }
    return true;
}
struct RP { i64 xn, xd, yn, yd; };
struct Res { std::string status; std::vector<Ring> rings; std::vector<std::vector<RP>> rrat; std::vector<std::vector<int>> ftri, btri; std::vector<std::pair<int, int>> bedge; };
inline bool readRes(const std::string& path, Res& r) {
    std::ifstream f(path); if (!f) return false; std::string tok; f >> tok >> r.status; if (tok != "RES1") return false;
    while (f >> tok) {
        if (tok == "ring") { size_t n; f >> n; Ring g(n); for (auto& p : g) f >> p.x >> p.y; r.rings.push_back(g); }
        else if (tok == "rrat") { size_t n; f >> n; std::vector<RP> g(n); for (auto& p : g) f >> p.xn >> p.xd >> p.yn >> p.yd; r.rrat.push_back(g); }
        else if (tok == "ftri") { int a, b, c; f >> a >> b >> c; r.ftri.push_back({a, b, c}); }
        else if (tok == "btri") { int a, b, c; f >> a >> b >> c; r.btri.push_back({a, b, c}); }
        else if (tok == "bedge") { int a, b; f >> a >> b; r.bedge.push_back({a, b}); }
    }
    return true;
}
}  // namespace h2d
