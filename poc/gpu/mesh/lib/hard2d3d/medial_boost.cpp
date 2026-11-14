// MP08b medial-axis sanity prototype: Boost.Polygon voronoi (BSL-1.0, boost 1.92.0, exact for int input) on the polygon boundary segments; keeps primary finite edges whose
// midpoint lies inside the polygon. Checks (double tolerance, NOT an exact oracle): (a) each kept vertex has equal clearance to the sites of its edge's two cells and no boundary
// segment is closer; (b) topology: #degree>=3 vertices == (convex vertices) - 2 + 2*holes for generic input (reported, deviations counted as 'topology differs').
// usage: medial_boost <case.h2d...>
#include "hard2d.h"
#include <boost/polygon/voronoi.hpp>
#include <cmath>
#include <map>
using namespace h2d; using namespace boost::polygon;
struct Seg { int x0, y0, x1, y1; };
namespace boost { namespace polygon { template <> struct geometry_concept<Seg> { typedef segment_concept type; };
 template <> struct segment_traits<Seg> { typedef int coordinate_type; typedef point_data<int> point_type; static point_type get(const Seg& s, direction_1d d) { return d.to_int() ? point_type(s.x1, s.y1) : point_type(s.x0, s.y0); } }; } }
static double dseg(double px, double py, const Seg& s) { double dx = s.x1 - s.x0, dy = s.y1 - s.y0; double t = ((px - s.x0) * dx + (py - s.y0) * dy) / (dx * dx + dy * dy); t = std::max(0.0, std::min(1.0, t)); return std::hypot(px - (s.x0 + t * dx), py - (s.y0 + t * dy)); }
static int pip(const std::vector<Ring>& rs, double x, double y) { int c = 0; for (auto& r : rs) for (size_t i = 0; i < r.size(); i++) { P a = r[i], b = r[(i + 1) % r.size()]; if ((a.y > y) != (b.y > y) && x < a.x + (double)(b.x - a.x) * (y - a.y) / (double)(b.y - a.y)) c ^= 1; } return c; }
int main(int argc, char** argv) { int okAll = 0, topo = 0, bad = 0, total = 0;
    for (int ai = 1; ai < argc; ai++) { Case c; if (!readCase(argv[ai], c)) continue; std::vector<Seg> segs; for (auto& r : c.rings) for (size_t i = 0; i < r.size(); i++) segs.push_back({(int)r[i].x, (int)r[i].y, (int)r[(i + 1) % r.size()].x, (int)r[(i + 1) % r.size()].y});
        voronoi_diagram<double> vd; construct_voronoi(segs.begin(), segs.end(), &vd); total++;
        auto site = [&](const voronoi_diagram<double>::cell_type& cell, double px, double py) { const Seg& s = segs[cell.source_index()]; if (cell.source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT) return std::hypot(px - s.x0, py - s.y0); if (cell.source_category() == SOURCE_CATEGORY_SEGMENT_END_POINT) return std::hypot(px - s.x1, py - s.y1); return dseg(px, py, s); };
        std::map<const voronoi_diagram<double>::vertex_type*, int> deg; bool good = true; int nedge = 0;
        for (auto& e : vd.edges()) { if (e.is_secondary() || e.is_infinite() || e.vertex0() > e.vertex1()) continue; auto *v0 = e.vertex0(), *v1 = e.vertex1(); double mx = (v0->x() + v1->x()) / 2, my = (v0->y() + v1->y()) / 2; if (!pip(c.rings, mx, my)) continue;
            nedge++; deg[v0]++; deg[v1]++; for (auto* v : {v0, v1}) { double d1 = site(*e.cell(), v->x(), v->y()), d2 = site(*e.twin()->cell(), v->x(), v->y()), dm = 1e300; for (auto& s : segs) dm = std::min(dm, dseg(v->x(), v->y(), s));
                if (std::fabs(d1 - d2) > 1e-6 * (1 + d1) || dm < d1 - 1e-6 * (1 + d1)) good = false; } }
        int d3 = 0; for (auto& kv : deg) if (kv.second >= 3) d3++; int convex = 0; for (auto& r : c.rings) for (size_t i = 0; i < r.size(); i++) { P a = r[(i + r.size() - 1) % r.size()], b = r[i], d = r[(i + 1) % r.size()]; i128 cr = (i128)(b.x - a.x) * (d.y - b.y) - (i128)(b.y - a.y) * (d.x - b.x); if (cr > 0) convex++; }
        int expect = convex - 2 + 2 * ((int)c.rings.size() - 1); bool topoOk = d3 == expect; if (good) okAll++; else bad++; if (good && !topoOk) topo++;
        printf("%s clearance_ok=%d deg3=%d expected=%d edges=%d\n", c.name.c_str(), good, d3, expect, nedge); }
    printf("SUMMARY cases=%d clearance_ok=%d clearance_bad=%d topology_differs=%d\n", total, okAll, bad, topo); return 0; }
