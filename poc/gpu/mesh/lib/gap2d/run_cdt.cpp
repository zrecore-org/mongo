// MP08a runner: CDT 2.0.2 (MPL-2.0; comparison only), header-only, constrained Delaunay with auto hole/outer erasure, default IntersectingConstraintEdges::NotAllowed.
// usage: run_cdt triangulate <case.p2d> <out.res> [reps]
#include "common2d.h"
#include <CDT.h>
#include <chrono>
using namespace p2d; typedef std::vector<std::pair<double, double>> OR;
int main(int argc, char** argv) {
    if (argc < 4) return 2; std::string op = argv[1]; Case c; if (!readCase(argv[2], c)) return 3; int reps = argc > 4 ? atoi(argv[4]) : 1;
    if (op != "triangulate") { writeRes(argv[3], "REFUSED", "CDT only triangulates", {}, {}); return 0; }
    std::vector<double> ns; std::vector<OR> rings;
    try {
        for (int rep = 0; rep < reps; rep++) {
            std::vector<CDT::V2d<double>> pts; std::vector<CDT::Edge> edges;
            for (auto& poly : c.sets[0]) for (auto& ring : poly) { size_t base = pts.size(); for (auto& q : ring) pts.push_back(CDT::V2d<double>(static_cast<double>(q.x), static_cast<double>(q.y)));
                for (size_t i = 0; i < ring.size(); i++) edges.push_back(CDT::Edge((CDT::VertInd)(base + i), (CDT::VertInd)(base + (i + 1) % ring.size()))); }
            auto t0 = std::chrono::steady_clock::now();
            CDT::Triangulation<double> t;
            auto dups = CDT::RemoveDuplicatesAndRemapEdges(pts, edges);
            (void)dups;
            t.insertVertices(pts); t.insertEdges(edges); t.eraseOuterTrianglesAndHoles();
            auto t1 = std::chrono::steady_clock::now(); ns.push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            if (rep == reps - 1) for (auto& tr : t.triangles) { OR o; for (int k = 0; k < 3; k++) { const auto& v = t.vertices[tr.vertices[(size_t)k]]; o.push_back({v.x, v.y}); } rings.push_back(o); }
        }
    } catch (const std::exception& e) { writeRes(argv[3], "REFUSED", std::string("exception: ") + e.what(), {}, {}); return 0; }
    writeRes(argv[3], "OK", "", ns, rings); return 0;
}
