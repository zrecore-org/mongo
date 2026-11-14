// MP08a runner: Manifold v3.5.5 CrossSection (and manifold::Triangulate). Fill rule NonZero. usage: run_manifold <op> <case.p2d> <out.res> [reps]
#include "common2d.h"
#include "manifold/cross_section.h"
#include "manifold/polygon.h"
#include <chrono>
using namespace p2d; using namespace manifold; typedef std::vector<std::pair<double, double>> OR;
static Polygons toPolys(const Set& s) { Polygons r; for (auto& poly : s) for (auto& ring : poly) { SimplePolygon p; for (auto& q : ring) p.push_back(vec2((double)q.x, (double)q.y)); r.push_back(p); } return r; }
int main(int argc, char** argv) {
    if (argc < 4) return 2; std::string op = argv[1]; Case c; if (!readCase(argv[2], c)) return 3; int reps = argc > 4 ? atoi(argv[4]) : 1;
    Polygons PA = toPolys(c.sets[0]), PB = c.sets.size() > 1 ? toPolys(c.sets[1]) : Polygons();
    std::vector<double> ns; std::vector<OR> rings; double num = 0; bool hasNum = false;
    try {
        CrossSection A(PA, CrossSection::FillRule::NonZero), B(PB, CrossSection::FillRule::NonZero);
        for (int rep = 0; rep < reps; rep++) {
            Polygons res; auto t0 = std::chrono::steady_clock::now();
            if (op == "union") res = A.Boolean(B, OpType::Add).ToPolygons();
            else if (op == "intersection") res = A.Boolean(B, OpType::Intersect).ToPolygons();
            else if (op == "difference") res = A.Boolean(B, OpType::Subtract).ToPolygons();
            else if (op == "offset") res = A.Offset((double)c.offset, CrossSection::JoinType::Miter, 3.0, 8).ToPolygons();
            else if (op == "hull") res = A.Hull().ToPolygons();
            else if (op == "area") { num = A.Area(); hasNum = true; }
            else if (op == "triangulate") { std::vector<ivec3> tr = Triangulate(PA); std::vector<vec2> flat; for (auto& p : PA) for (auto& q : p) flat.push_back(q); for (auto& t : tr) res.push_back(SimplePolygon{flat[(size_t)t[0]], flat[(size_t)t[1]], flat[(size_t)t[2]]}); }
            else { writeRes(argv[3], "REFUSED", "no such operation", {}, {}); return 0; }
            auto t1 = std::chrono::steady_clock::now(); ns.push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            if (rep == reps - 1) for (auto& p : res) { OR r; for (auto& q : p) r.push_back({q.x, q.y}); rings.push_back(r); }
        }
    } catch (const std::exception& e) { writeRes(argv[3], "REFUSED", std::string("exception: ") + e.what(), {}, {}); return 0; }
    writeRes(argv[3], "OK", "", ns, rings, hasNum ? &num : nullptr, nullptr); return 0;
}
