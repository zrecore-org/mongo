// MP07 runner: run3d <route> <case.c3d>  -> one TSV row per sub-result on stdout:
//   name  kind  cls  route  sub  verdict  ulps  ns  message
// Routes: extrude: manifold own | revolve: manifold | tess: earcut cdt | mink: msum hull | dist_pm: own | dist_mm: mingap own | measure: manifold own
#include "oracle3d.h"
#include "own3d.h"
#include <CDT.h>
#include "manifold/manifold.h"
#include <chrono>
using namespace c3d;
using manifold::Manifold; using manifold::MeshGL64;
static const char* vname(int c) { static const char* n[] = {"CORRECT", "NEAR", "WRONG", "REFUSED", "NOAPI"}; return n[c]; }
static std::string g_name, g_kind, g_cls, g_route;
static void row(const char* sub, const o3::Verdict& v, double ns) {
    std::string m = v.msg; for (auto& ch : m) if (ch == '\t' || ch == '\n') ch = ' ';
    printf("%s\t%s\t%s\t%s\t%s\t%s\t%.3g\t%.0f\t%s\n", g_name.c_str(), g_kind.c_str(), g_cls.c_str(), g_route.c_str(), sub, vname(v.cls), v.ulps, ns, m.c_str()); }
typedef std::chrono::steady_clock Clk;
static double since(Clk::time_point t0) { return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(Clk::now() - t0).count(); }

static Manifold fromIM(const c3d::IMesh& m, int exp) {
    double sc = std::ldexp(1.0, exp); MeshGL64 g; g.numProp = 3; for (auto& v : m.v) for (int d = 0; d < 3; d++) g.vertProperties.push_back((double)v[d] * sc);
    for (auto& t : m.t) for (int k = 0; k < 3; k++) g.triVerts.push_back((uint64_t)t[k]); return Manifold(g); }
static void toMesh(const Manifold& M, mesh::Mesh& out) { MeshGL64 g = M.GetMeshGL64(); out.v.assign(g.vertProperties.begin(), g.vertProperties.end()); out.t.clear(); for (auto x : g.triVerts) out.t.push_back((uint32_t)x); }
static manifold::Polygons polys(const Case& c) { double sc = std::ldexp(1.0, c.exp); manifold::Polygons P; for (auto& r : c.rings) { manifold::SimplePolygon s; for (auto& p : r.p) s.push_back(manifold::vec2((double)p[0] * sc, (double)p[1] * sc)); P.push_back(s); } return P; }
static o3::Verdict worst(const std::vector<o3::Verdict>& vs) { o3::Verdict w; for (auto& v : vs) { if (v.cls > w.cls) w = v; else if (v.cls == w.cls && v.ulps > w.ulps) { w.ulps = v.ulps; if (!v.msg.empty()) w.msg = v.msg; } } return w; }

int main(int argc, char** argv) {
    if (argc < 3) return 2; g_route = argv[1]; Case c; if (!read(argv[2], c)) return 3; g_name = c.name; g_kind = c.kind; g_cls = c.cls;
    const std::string& k = c.kind; const std::string& r = g_route; int e = c.exp;
    try {
    if (k == "extrude") {
        mesh::Mesh out; bool refused = false; auto t0 = Clk::now();
        if (r == "manifold") {
            double sc = std::ldexp(1.0, e), h = (double)c.P("vec", 2) * sc, dx = (double)c.P("vec", 0) * sc, dy = (double)c.P("vec", 1) * sc;
            Manifold M = Manifold::Extrude(polys(c), h);
            if (dx != 0 || dy != 0) M = M.Transform(manifold::mat3x4(manifold::vec3(1, 0, 0), manifold::vec3(0, 1, 0), manifold::vec3(dx / h, dy / h, 1), manifold::vec3(0, 0, 0)));
            if (M.Status() != Manifold::Error::NoError || M.IsEmpty()) refused = true; else toMesh(M, out);
            if (dx != 0 || dy != 0) g_cls += "+shear";
        } else if (r == "own_cdt") {   // same walls, caps by constrained Delaunay (conforming: no edge passes through a vertex)
            std::vector<CDT::V2d<double>> pts; std::vector<CDT::Edge> edges; double sc = std::ldexp(1.0, e);
            for (auto& ring : c.rings) { size_t base = pts.size(); for (auto& q : ring.p) pts.push_back(CDT::V2d<double>((double)q[0] * sc, (double)q[1] * sc)); for (size_t i = 0; i < ring.p.size(); i++) edges.push_back(CDT::Edge((CDT::VertInd)(base + i), (CDT::VertInd)(base + (i + 1) % ring.p.size()))); }
            std::vector<uint32_t> idx; bool bad = false;
            try { CDT::Triangulation<double> t; t.insertVertices(pts); t.insertEdges(edges); t.eraseOuterTrianglesAndHoles(); for (auto& tr : t.triangles) for (int a = 0; a < 3; a++) idx.push_back((uint32_t)tr.vertices[(size_t)a]); } catch (const std::exception&) { bad = true; }
            refused = bad || !own::extrude(c, out, &idx); if (c.P("vec", 0) != 0 || c.P("vec", 1) != 0) g_cls += "+shear";
        } else { refused = !own::extrude(c, out); if (c.P("vec", 0) != 0 || c.P("vec", 1) != 0) g_cls += "+shear"; }
        double ns = since(t0); row("solid", o3::checkExtrude(c, out, refused), ns);
    } else if (k == "revolve") {
        mesh::Mesh out; bool refused = false; auto t0 = Clk::now(); double sc = std::ldexp(1.0, e); (void)sc;
        int seg = (int)c.P("seg"); if (r == "neg_seg") seg += 5;   // negative control: the oracle must call these WRONG
        Manifold M = Manifold::Revolve(polys(c), seg, (double)c.P("deg"));
        if (M.Status() != Manifold::Error::NoError || M.IsEmpty()) refused = true; else { toMesh(M, out); if (r == "neg_nudge") out.v[0] = std::nextafter(out.v[0], 1e300) + (out.v[0] == 0 ? 1e-9 : 0); }
        double ns = since(t0); row("solid", o3::checkRevolve(c, out, refused), ns);
    } else if (k == "tess") {
        o3::TriOut to; bool refused = false; auto t0 = Clk::now();
        try {
            if (r == "earcut") { refused = !own::tessEarcut(c, to.t); }
            else {   // cdt
                own::Proj p = own::makeProj(c); if (!p.ok) refused = true; else {
                    std::vector<CDT::V2d<double>> pts; std::vector<CDT::Edge> edges; std::map<std::pair<double, double>, std::array<i64, 3>> back; double sc = std::ldexp(1.0, e);
                    for (auto& ring : c.rings) { size_t base = pts.size(); for (auto& q : ring.p) { auto a = own::proj2(p, q); pts.push_back(CDT::V2d<double>(a[0], a[1])); back[{a[0], a[1]}] = q; }
                        for (size_t i = 0; i < ring.p.size(); i++) edges.push_back(CDT::Edge((CDT::VertInd)(base + i), (CDT::VertInd)(base + (i + 1) % ring.p.size()))); }
                    CDT::RemoveDuplicatesAndRemapEdges(pts, edges); CDT::Triangulation<double> t; t.insertVertices(pts); t.insertEdges(edges); t.eraseOuterTrianglesAndHoles();
                    for (auto& tr : t.triangles) { std::array<std::array<double, 3>, 3> o; for (int a = 0; a < 3; a++) { const auto& v = t.vertices[tr.vertices[(size_t)a]]; auto it = back.find({v.x, v.y}); if (it == back.end()) { o3::Verdict w = o3::wrong("CDT vertex not an input vertex (Steiner)"); row("tess", w, since(t0)); return 0; } for (int d = 0; d < 3; d++) o[a][d] = (double)it->second[d] * sc; } to.t.push_back(o); } } }
        } catch (const std::exception& ex) { o3::Verdict v; v.cls = o3::REFUSED; v.msg = std::string("exception: ") + ex.what(); row("tess", v, since(t0)); return 0; }
        double ns = since(t0); row("tess", o3::checkTess(c, to, refused), ns);
    } else if (k == "mink" || k == "mink_nc") {
        mesh::Mesh out; bool refused = false; auto t0 = Clk::now();
        if (r == "msum" || r == "msumT") { if (!c.mesh.count("B")) { o3::Verdict v; v.cls = o3::NOAPI; v.msg = "n/a: second operand is a flat point set, not a solid"; row("solid", v, 0); return 0; }
            Manifold A = fromIM(c.mesh.at("A"), e), B = fromIM(c.mesh.at("B"), e); if (A.Status() != Manifold::Error::NoError || B.Status() != Manifold::Error::NoError) refused = true;
            else {
                // Manifold's convex/convex branch returns Union(A, hull of pairwise sums): it is the Minkowski sum only if the origin lies in B. msumT translates B by -b0 (b0 a vertex of B, so 0 is in the shifted B) and the result by +b0.
                c3d::V3 b0 = {0, 0, 0}; if (r == "msumT") { b0 = c.mesh.at("B").v[0]; c3d::IMesh sh = c.mesh.at("B"); for (auto& v : sh.v) for (int d = 0; d < 3; d++) v[d] -= b0[d]; B = fromIM(sh, e); }
                Manifold M = A.MinkowskiSum(B); if (M.Status() != Manifold::Error::NoError || M.IsEmpty()) refused = true; else { toMesh(M, out); double sc = std::ldexp(1.0, e); for (size_t i = 0; i < out.nv(); i++) for (int d = 0; d < 3; d++) out.v[3 * i + d] += (double)b0[d] * sc; } } }
        else { double sc = std::ldexp(1.0, e); std::vector<manifold::vec3> pts; const auto& B = c.mesh.count("B") ? c.mesh.at("B").v : c.pts.at("B"); for (auto& a : c.mesh.at("A").v) for (auto& b : B) pts.push_back(manifold::vec3((double)(a[0] + b[0]) * sc, (double)(a[1] + b[1]) * sc, (double)(a[2] + b[2]) * sc));
            Manifold M = Manifold::Hull(pts); if (M.Status() != Manifold::Error::NoError || M.IsEmpty()) refused = true; else toMesh(M, out); }
        double ns = since(t0); row("solid", k == "mink" ? o3::checkMinkowski(c, out, refused) : o3::checkMinkowskiNC(c, out, refused), ns);
    } else if (k == "dist_pm") {
        o3::IMesh A = o3::fromCase(c.mesh.at("A"), e); o3::IV p = {o3::BigInt(c.pt[0]), o3::BigInt(c.pt[1]), o3::BigInt(c.pt[2])}; o3::PM pm = o3::pointMesh(p, A);
        own::DMesh d = own::fromCase(c.mesh.at("A"), e); d.bvh.build(d.v, d.t); double sc = std::ldexp(1.0, e); auto t0 = Clk::now(); double got = own::pointMeshD(d, {(double)c.pt[0] * sc, (double)c.pt[1] * sc, (double)c.pt[2] * sc}); double ns = since(t0);
        row("distance", o3::cmpDist(got, pm.d2, e), ns);
    } else if (k == "dist_mm") {
        o3::IMesh A = o3::fromCase(c.mesh.at("A"), e), B = o3::fromCase(c.mesh.at("B"), e); o3::Q D = o3::solidDist2(A, B); double got; auto t0 = Clk::now();
        if (r == "mingap") { Manifold MA = fromIM(c.mesh.at("A"), e), MB = fromIM(c.mesh.at("B"), e);
            if (MA.Status() != Manifold::Error::NoError || MB.Status() != Manifold::Error::NoError) { o3::Verdict v; v.cls = o3::REFUSED; v.msg = "input refused"; row("distance", v, 0); return 0; }
            auto bb = MA.BoundingBox(); auto bb2 = MB.BoundingBox(); double dg = 0; for (int i = 0; i < 3; i++) { double lo = std::min(bb.min[i], bb2.min[i]), hi = std::max(bb.max[i], bb2.max[i]); dg += (hi - lo) * (hi - lo); }
            t0 = Clk::now(); got = MA.MinGap(MB, 4 * std::sqrt(dg) + 1); }
        else { own::DMesh da = own::fromCase(c.mesh.at("A"), e), db = own::fromCase(c.mesh.at("B"), e); da.bvh.build(da.v, da.t); db.bvh.build(db.v, db.t); t0 = Clk::now(); got = own::meshMeshD(da, db, true); }
        double ns = since(t0); row("distance", o3::cmpDist(got, D, e), ns);
    } else if (k == "measure") {
        o3::IMesh A = o3::fromCase(c.mesh.at("A"), e); o3::Q v6 = o3::Q(o3::vol6(A)); o3::QV cen; bool hasC = o3::centroid(A, cen); o3::Area ar = o3::area(A); o3::IV lo, hi; o3::bbox(A, lo, hi);
        auto bboxV = [&](const double* mn, const double* mx) { std::vector<o3::Verdict> vs; for (int d = 0; d < 3; d++) { vs.push_back(o3::cmpScalar(mn[d], o3::Q(lo[d]), e)); vs.push_back(o3::cmpScalar(mx[d], o3::Q(hi[d]), e)); } return worst(vs); };
        if (r == "manifold") {
            Manifold M = fromIM(c.mesh.at("A"), e); if (M.Status() != Manifold::Error::NoError) { o3::Verdict v; v.cls = o3::REFUSED; v.msg = "input refused"; row("volume", v, 0); return 0; }
            auto t0 = Clk::now(); double vol = M.Volume(); double ns = since(t0); row("volume", o3::cmpScalar(vol, v6 / 6, 3L * e), ns);
            t0 = Clk::now(); double a = M.SurfaceArea(); ns = since(t0); o3::Verdict va = o3::cmpArea(a, ar, e); if (ar.exact) va.msg = "exact-area case"; row("area", va, ns);
            t0 = Clk::now(); auto bb = M.BoundingBox(); ns = since(t0); double mn[3] = {bb.min[0], bb.min[1], bb.min[2]}, mx[3] = {bb.max[0], bb.max[1], bb.max[2]}; row("bbox", bboxV(mn, mx), ns);
            o3::Verdict na; na.cls = o3::NOAPI; na.msg = "no centroid query in Manifold 3.5.5 headers"; row("centroid", na, 0); na.msg = "no closest-point query in Manifold 3.5.5 headers"; row("closest", na, 0);
        } else {
            own::DMesh d = own::fromCase(c.mesh.at("A"), e); d.bvh.build(d.v, d.t);
            auto t0 = Clk::now(); double vol = own::volumeD(d); double ns = since(t0); row("volume", o3::cmpScalar(vol, v6 / 6, 3L * e), ns);
            t0 = Clk::now(); double a = own::areaD(d); ns = since(t0); o3::Verdict va = o3::cmpArea(a, ar, e); if (ar.exact) va.msg = "exact-area case"; row("area", va, ns);
            t0 = Clk::now(); own::D3 ce = own::centroidD(d); ns = since(t0);
            if (hasC) { std::vector<o3::Verdict> vs; for (int i = 0; i < 3; i++) vs.push_back(o3::cmpScalar(ce[i], cen[i], e)); row("centroid", worst(vs), ns); } else { o3::Verdict w; w.cls = o3::REFUSED; w.msg = "zero volume"; row("centroid", w, ns); }
            double mn[3] = {1e300, 1e300, 1e300}, mx[3] = {-1e300, -1e300, -1e300}; t0 = Clk::now(); for (auto& v : d.v) for (int i = 0; i < 3; i++) { mn[i] = std::min(mn[i], v[i]); mx[i] = std::max(mx[i], v[i]); } ns = since(t0); row("bbox", bboxV(mn, mx), ns);
            double sc = std::ldexp(1.0, e); own::D3 q; t0 = Clk::now(); own::pointMeshD(d, {(double)c.pt[0] * sc, (double)c.pt[1] * sc, (double)c.pt[2] * sc}, &q); ns = since(t0);
            o3::PM pm = o3::pointMesh({o3::BigInt(c.pt[0]), o3::BigInt(c.pt[1]), o3::BigInt(c.pt[2])}, A); o3::Verdict bestV; bool first = true;
            for (auto& cand : pm.pts) { std::vector<o3::Verdict> vs; for (int i = 0; i < 3; i++) vs.push_back(o3::cmpScalar(q[i], cand[i], e)); o3::Verdict w = worst(vs); if (first || w.cls < bestV.cls || (w.cls == bestV.cls && w.ulps < bestV.ulps)) { bestV = w; first = false; } }
            row("closest", bestV, ns);
        }
    } else return 3;
    } catch (const std::exception& ex) { o3::Verdict v; v.cls = o3::REFUSED; v.msg = std::string("exception: ") + ex.what(); row("exception", v, 0); }
    return 0;
}
