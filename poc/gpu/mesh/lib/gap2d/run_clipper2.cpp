// MP08a runner: Clipper2 2.0.1 (Paths64, integer grid at scale 1, FillRule::NonZero). usage: run_clipper2 <op> <case.p2d> <out.res> [reps]
#include "common2d.h"
#include "clipper2/clipper.h"
#include "clipper2/clipper.triangulation.h"
#include <chrono>
using namespace Clipper2Lib; using namespace p2d;
typedef std::vector<std::pair<double, double>> OR;
static Paths64 toPaths(const Set& s) { Paths64 r; for (auto& poly : s) for (auto& ring : poly) { Path64 p; for (auto& q : ring) p.push_back(Point64(q.x, q.y)); r.push_back(p); } return r; }
int main(int argc, char** argv) {
    if (argc < 4) return 2; std::string op = argv[1]; Case c; if (!readCase(argv[2], c)) return 3; int reps = argc > 4 ? atoi(argv[4]) : 1;
    Paths64 A = toPaths(c.sets[0]), B = c.sets.size() > 1 ? toPaths(c.sets[1]) : Paths64();
    std::vector<double> ns; std::vector<OR> rings; double num = 0; int bval = 0; bool hasNum = false, hasBool = false;
    try {
        for (int rep = 0; rep < reps; rep++) {
            Paths64 res; auto t0 = std::chrono::steady_clock::now();
            if (op == "union") res = Union(A, B, FillRule::NonZero);
            else if (op == "intersection") res = Intersect(A, B, FillRule::NonZero);
            else if (op == "difference") res = Difference(A, B, FillRule::NonZero);
            else if (op == "offset") res = InflatePaths(A, (double)c.offset, JoinType::Miter, EndType::Polygon, 3.0);
            else if (op == "area") { num = Area(A); hasNum = true; }
            else if (op == "intersects") { Paths64 x = Intersect(A, B, FillRule::NonZero); bval = x.empty() ? 0 : 1; hasBool = true; }
            else if (op == "triangulate") { Paths64 sol; TriangulateResult tr = Triangulate(A, sol, true); if (tr != TriangulateResult::success) { writeRes(argv[3], "REFUSED", std::string("TriangulateResult=") + std::to_string((int)tr), {}, {}); return 0; } res = sol; }
            else { writeRes(argv[3], "REFUSED", "no such operation in Clipper2", {}, {}); return 0; }
            auto t1 = std::chrono::steady_clock::now(); ns.push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            if (rep == reps - 1) for (auto& p : res) { OR r; for (auto& q : p) r.push_back({(double)q.x, (double)q.y}); rings.push_back(r); }
        }
    } catch (const std::exception& e) { writeRes(argv[3], "REFUSED", std::string("exception: ") + e.what(), {}, {}); return 0; }
    catch (...) { writeRes(argv[3], "REFUSED", "unknown exception", {}, {}); return 0; }
    writeRes(argv[3], "OK", "", ns, rings, hasNum ? &num : nullptr, hasBool ? &bval : nullptr); return 0;
}
