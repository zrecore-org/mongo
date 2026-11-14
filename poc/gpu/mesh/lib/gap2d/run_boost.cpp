// MP08a runner: Boost.Geometry 1.92.0 (double points, counter-clockwise closed polygons). usage: run_boost <op> <case.p2d> <out.res> [reps]
#include "common2d.h"
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/geometries.hpp>
#include <chrono>
namespace bg = boost::geometry; using namespace p2d;
typedef bg::model::d2::point_xy<double> Pt;
typedef bg::model::polygon<Pt, false, true> BPoly;   // counter-clockwise, closed
typedef bg::model::multi_polygon<BPoly> MP;
typedef std::vector<std::pair<double, double>> OR;
static void fill(std::vector<Pt>& dst, const Ring& r) { for (auto& q : r) dst.push_back(Pt((double)q.x, (double)q.y)); dst.push_back(Pt((double)r[0].x, (double)r[0].y)); }
static MP toMP(const Set& s) { MP m; for (auto& poly : s) { BPoly p; if (poly.empty()) continue; fill(p.outer(), poly[0]); for (size_t i = 1; i < poly.size(); i++) { p.inners().push_back({}); fill(p.inners().back(), poly[i]); } m.push_back(p); } return m; }
static void collect(const MP& m, std::vector<OR>& rings) {
    for (auto& p : m) { OR o; for (size_t i = 0; i + 1 < p.outer().size(); i++) o.push_back({p.outer()[i].x(), p.outer()[i].y()}); rings.push_back(o);
        for (auto& h : p.inners()) { OR q; for (size_t i = 0; i + 1 < h.size(); i++) q.push_back({h[i].x(), h[i].y()}); rings.push_back(q); } }
}
int main(int argc, char** argv) {
    if (argc < 4) return 2; std::string op = argv[1]; Case c; if (!readCase(argv[2], c)) return 3; int reps = argc > 4 ? atoi(argv[4]) : 1;
    MP A = toMP(c.sets[0]), B = c.sets.size() > 1 ? toMP(c.sets[1]) : MP();
    std::vector<double> ns; std::vector<OR> rings; double num = 0; int bval = 0; bool hasNum = false, hasBool = false;
    try {
        for (int rep = 0; rep < reps; rep++) {
            MP res; auto t0 = std::chrono::steady_clock::now(); BPoly hull;
            if (op == "union") bg::union_(A, B, res);
            else if (op == "intersection") bg::intersection(A, B, res);
            else if (op == "difference") bg::difference(A, B, res);
            else if (op == "offset") { bg::strategy::buffer::distance_symmetric<double> ds((double)c.offset); bg::strategy::buffer::side_straight ss; bg::strategy::buffer::join_miter jm(3.0); bg::strategy::buffer::end_flat es; bg::strategy::buffer::point_circle pc(8); bg::buffer(A, res, ds, ss, jm, es, pc); }
            else if (op == "hull") bg::convex_hull(A, hull);
            else if (op == "area") { num = bg::area(A); hasNum = true; }
            else if (op == "intersects") { bval = bg::intersects(A, B) ? 1 : 0; hasBool = true; }
            else if (op == "isvalid") { bval = bg::is_valid(A) ? 1 : 0; hasBool = true; }
            else { writeRes(argv[3], "REFUSED", "no such operation", {}, {}); return 0; }
            auto t1 = std::chrono::steady_clock::now(); ns.push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            if (rep == reps - 1) { collect(res, rings); if (op == "hull") { OR o; for (size_t i = 0; i + 1 < hull.outer().size(); i++) o.push_back({hull.outer()[i].x(), hull.outer()[i].y()}); rings.push_back(o); } }
        }
    } catch (const std::exception& e) { writeRes(argv[3], "REFUSED", std::string("exception: ") + e.what(), {}, {}); return 0; }
    writeRes(argv[3], "OK", "", ns, rings, hasNum ? &num : nullptr, hasBool ? &bval : nullptr); return 0;
}
