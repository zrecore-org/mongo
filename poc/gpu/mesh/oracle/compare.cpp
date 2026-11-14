// Library scoring tool (MP02 canonical equivalence). One yardstick for every boolean engine; the pass bars are pre-registered in PLAN.md.
//   mesh_compare score <corpusdir> <caseId> <union|intersection|difference> <result.msh> [points]
//   mesh_compare selftest
// For each result it reports, all exact: (1) validity of the inputs as solids; (2) topology of the result (closed manifold? Euler characteristic, components);
// (3) the exact volume against the manifest's exact expectation (box cases) classified exact / close / wrong; (4) point-in-solid agreement of the result with
// the boolean of A and B: for box pairs EVERY cell of the arrangement is tested (the per-axis midpoints of the sorted box coordinates, plus a cell outside), for
// other pairs `points` exact dyadic sample points (default 5000); points that lie on a surface are skipped and counted.
#include "../harness/pip.h"
#include "../harness/topology.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

using namespace mesh;

static bool parseDyadic(const std::string& s, Dyadic& d) {
    size_t star = s.find("*2^"); if (star == std::string::npos) return false;
    d.num = BigInt(s.substr(0, star)); d.exp2 = atoi(s.c_str() + star + 3); return true;
}
static double ratio(const Dyadic& a, const Dyadic& b) {            // a / b as a double (b != 0)
    int e = std::min(a.exp2, b.exp2);
    return align(a, e).num.convert_to<double>() / align(b, e).num.convert_to<double>();
}
static bool isAxisBox(const Mesh& m, std::vector<double> xs[3]) {
    if (m.nv() != 8 || m.nt() != 12) return false;
    for (int k = 0; k < 3; k++) { for (size_t i = 0; i < 8; i++) xs[k].push_back(m.v[3*i+k]); std::sort(xs[k].begin(), xs[k].end()); xs[k].erase(std::unique(xs[k].begin(), xs[k].end()), xs[k].end()); if (xs[k].size() > 2) return false; }
    return true;
}
static bool inOp(int op, bool a, bool b) { return op == 0 ? (a || b) : op == 1 ? (a && b) : (a && !b); }
static const char* kOpName[] = {"union", "intersection", "difference"};

struct Score { int cells = 0, cellBad = 0, cellSkipped = 0, pts = 0, ptsBad = 0, ptsSkipped = 0; };

static void testPoint(const Mesh& A, const Mesh& B, const Mesh& R, int op, const double* p, bool cell, Score& s) {
    Pip a = classifyPoint(A, p), b = classifyPoint(B, p), r = classifyPoint(R, p);
    int& n = cell ? s.cells : s.pts; int& bad = cell ? s.cellBad : s.ptsBad; int& skip = cell ? s.cellSkipped : s.ptsSkipped;
    if (a.boundary || b.boundary || r.boundary) { skip++; return; }
    n++; if (r.inside != inOp(op, a.inside, b.inside)) bad++;
}

static int score(const std::string& dir, const std::string& id, int op, const std::string& resultPath, int npts, bool quiet = false, Mesh* givenR = nullptr, Mesh* givenA = nullptr, Mesh* givenB = nullptr, const std::string* expRow = nullptr) {
    std::vector<std::string> c;
    std::string line;
    if (expRow) line = *expRow; else {
        std::ifstream f(dir + "/manifest.tsv"); std::getline(f, line);
        while (std::getline(f, line)) { if (line.compare(0, id.size() + 1, id + "\t") == 0) break; line.clear(); }
        if (line.empty()) { printf("case %s not in manifest\n", id.c_str()); return 2; }
    }
    { std::stringstream ss(line); std::string x; while (std::getline(ss, x, '\t')) c.push_back(x); }
    Mesh A, B, R;
    if (givenA) A = *givenA; else if (!readMesh(dir + "/adversarial/" + id + "_A.msh", A)) { printf("cannot load A\n"); return 2; }
    if (givenB) B = *givenB; else if (!readMesh(dir + "/adversarial/" + id + "_B.msh", B)) { printf("cannot load B\n"); return 2; }
    if (givenR) R = *givenR; else if (!readMesh(resultPath, R)) { printf("%s %s: FAIL cannot load result\n", id.c_str(), kOpName[op]); return 1; }
    std::vector<std::string> fails;
    for (double x : R.v) if (!std::isfinite(x)) { fails.push_back("non-finite coordinate in result"); break; }
    Topology ta = analyzeTopology(A), tb = analyzeTopology(B), tr = analyzeTopology(R);
    bool validInputs = ta.closedManifold() && tb.closedManifold() && !ta.inverted && !tb.inverted;
    Dyadic vr = exactVolumeTimes6(R);
    // volume against the manifest's exact expectation
    std::string volStatus = "n/a";
    double rel = 0;
    if (c[9 + op] != "-") {
        Dyadic ex; parseDyadic(c[9 + op], ex);
        Dyadic va, vb; parseDyadic(c[7], va); parseDyadic(c[8], vb);
        if (equalExact(vr, ex)) volStatus = "exact";
        else {
            Dyadic diff = subExact(vr, ex);
            if (signExact(ex) != 0) { rel = ratio(diff, ex); volStatus = std::fabs(rel) <= 1e-9 ? "close" : "wrong"; }
            else { Dyadic scale = signExact(subExact(va, vb)) >= 0 ? va : vb; double r0 = std::fabs(ratio(diff, scale)); rel = r0; volStatus = r0 <= std::ldexp(1.0, -52) ? "close" : "wrong"; }
        }
        if (volStatus == "wrong") fails.push_back("exact volume differs from the expectation by relative " + std::to_string(rel));
    }
    // structure of the result
    bool empty = R.nt() == 0;
    if (!empty && !tr.closedManifoldAsReturned()) fails.push_back("result is not a closed manifold (as returned, by vertex index)");     // pre-registered bar, amended 2026-11-14: judged as returned, see PLAN.md
    if (validInputs && !empty && tr.inverted) fails.push_back("result has negative volume (inverted)");
    if (validInputs && !empty && tr.duplicateTri) fails.push_back("result has a duplicated triangle");
    // point classification
    Score s;
    if (validInputs) {
        std::vector<double> xs[3], ys[3];
        bool boxes = isAxisBox(A, xs) && isAxisBox(B, ys);
        if (boxes) {
            std::vector<double> ax[3];
            for (int k = 0; k < 3; k++) {
                ax[k] = xs[k]; ax[k].insert(ax[k].end(), ys[k].begin(), ys[k].end());
                std::sort(ax[k].begin(), ax[k].end()); ax[k].erase(std::unique(ax[k].begin(), ax[k].end()), ax[k].end());
            }
            std::vector<double> cen[3];
            for (int k = 0; k < 3; k++) {
                cen[k].push_back(ax[k].front() - 1.0);
                for (size_t i = 0; i + 1 < ax[k].size(); i++) { double m = ax[k][i] + (ax[k][i+1] - ax[k][i]) / 2; if (m > ax[k][i] && m < ax[k][i+1]) cen[k].push_back(m); }   // skip gaps with no representable midpoint
            }
            for (double x : cen[0]) for (double y : cen[1]) for (double z : cen[2]) { double p[3] = {x, y, z}; testPoint(A, B, R, op, p, true, s); }
        }
        // dyadic samples in the expanded bounding box
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        for (const Mesh* mm : {&A, &B}) for (size_t i = 0; i < mm->nv(); i++) for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], mm->v[3*i+k]); hi[k] = std::max(hi[k], mm->v[3*i+k]); }
        double mx = 1; for (int k = 0; k < 3; k++) mx = std::max(mx, std::max(std::fabs(lo[k]), std::fabs(hi[k])));
        double g = std::max(std::ldexp(1.0, -14), std::ldexp(1.0, (int)std::ceil(std::log2(mx)) - 50));
        std::mt19937_64 rng(12345);
        for (int i = 0; i < npts; i++) {
            double p[3];
            for (int k = 0; k < 3; k++) { double span = hi[k] - lo[k]; double v = lo[k] - 0.25 * span + (double)(rng() >> 11) / 9007199254740992.0 * 1.5 * span; p[k] = (std::floor(v / g) + 0.5) * g; }
            testPoint(A, B, R, op, p, false, s);
        }
        if (s.cellBad) fails.push_back(std::to_string(s.cellBad) + " arrangement cell(s) classified differently from the boolean of A and B");
        if (s.ptsBad) fails.push_back(std::to_string(s.ptsBad) + " sample point(s) classified differently from the boolean of A and B");
    }
    bool pass = fails.empty();
    if (!quiet) {
        printf("%s %s: %s | inputs=%s | result tri=%zu closed=%d geomClosed=%d euler=%ld components=%d zeroArea=%d | volume=%s", id.c_str(), kOpName[op], pass ? (validInputs ? "PASS" : "ACCEPTED-UNVERIFIED") : "FAIL",
               validInputs ? "valid" : "invalid", R.nt(), (int)tr.closedManifoldAsReturned(), (int)tr.closedManifold(), tr.euler(), tr.components, (int)tr.zeroArea, volStatus.c_str());
        if (volStatus == "close") printf("(rel %.3g)", rel);
        printf(" | cells %d bad %d skipped %d | samples %d bad %d skipped %d", s.cells, s.cellBad, s.cellSkipped, s.pts, s.ptsBad, s.ptsSkipped);
        for (auto& f : fails) printf(" | %s", f.c_str());
        printf("\n");
    }
    return pass ? 0 : 1;
}

// ---------------- self-test ----------------
static Mesh box(double x0, double y0, double z0, double x1, double y1, double z1) {
    Mesh m; double lo[3] = {x0, y0, z0}, hi[3] = {x1, y1, z1};
    for (int z = 0; z < 2; z++) { double Z = z ? hi[2] : lo[2]; m.addV(lo[0], lo[1], Z); m.addV(hi[0], lo[1], Z); m.addV(hi[0], hi[1], Z); m.addV(lo[0], hi[1], Z); }
    static const uint32_t T[12][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{0,4,7},{0,7,3},{1,2,6},{1,6,5}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    return m;
}
static std::string boxRow(const char* id, const Mesh& A, const Mesh& B, const Dyadic& u, const Dyadic& i, const Dyadic& d) {
    return std::string(id) + "\tx\tvalid\t8\t12\t8\t12\t" + toString(exactVolumeTimes6(A)) + "\t" + toString(exactVolumeTimes6(B)) + "\t" + toString(u) + "\t" + toString(i) + "\t" + toString(d);
}

static int selftest() {
    int bad = 0;
    auto expect = [&](const char* what, int got, int want) { bool ok = got == want; printf("  %-72s %s\n", what, ok ? "ok" : "WRONG"); if (!ok) bad++; };
    Mesh A = box(0, 0, 0, 1, 1, 1), B = box(0.5, 0.25, -0.5, 1.5, 1.25, 0.5);
    Mesh I = box(0.5, 0.25, 0, 1, 1, 0.5);              // A intersect B
    Dyadic vA = exactVolumeTimes6(A), vB = exactVolumeTimes6(B), vI = exactVolumeTimes6(I);
    Dyadic vU = subExact(addExact(vA, vB), vI), vD = subExact(vA, vI);
    std::string row = boxRow("t", A, B, vU, vI, vD);
    std::string d = "";
    printf("scoring known-good and known-bad results (every wrong one must FAIL):\n");
    expect("correct intersection result passes", score(d, "t", 1, "", 100, true, &I, &A, &B, &row), 0);
    expect("intersection result replaced by A fails", score(d, "t", 1, "", 100, true, &A, &A, &B, &row), 1);
    Mesh Rm = I; Rm.t.resize(Rm.t.size() - 3);
    expect("intersection result with one triangle missing (open) fails", score(d, "t", 1, "", 100, true, &Rm, &A, &B, &row), 1);
    Mesh Ri = I; for (size_t k = 0; k < Ri.nt(); k++) std::swap(Ri.t[3*k+1], Ri.t[3*k+2]);
    expect("intersection result with inverted orientation fails", score(d, "t", 1, "", 100, true, &Ri, &A, &B, &row), 1);
    Mesh Rs = I; Rs.v[3 * 6] += std::ldexp(1.0, -20);
    expect("intersection result with a vertex moved by 2^-20 fails", score(d, "t", 1, "", 100, true, &Rs, &A, &B, &row), 1);
    Mesh Rt = I; Rt.v[3 * 6] += std::ldexp(1.0, -45);
    expect("intersection result with a vertex moved by 2^-45 is within the stated 1e-9 volume tolerance (passes)", score(d, "t", 1, "", 100, true, &Rt, &A, &B, &row), 0);
    Mesh E;   // empty
    expect("empty result for a non-empty intersection fails", score(d, "t", 1, "", 100, true, &E, &A, &B, &row), 1);
    std::string rowSame = boxRow("s", A, A, vA, vA, Dyadic{0, 0});
    expect("A minus A = empty passes", score(d, "s", 2, "", 100, true, &E, &A, &A, &rowSame), 0);
    expect("A union A = A passes", score(d, "s", 0, "", 100, true, &A, &A, &A, &rowSame), 0);
    Mesh A2 = box(0, 0, 0, 1, 1, 2);
    expect("A union A = a different box fails", score(d, "s", 0, "", 100, true, &A2, &A, &A, &rowSame), 1);
    printf("planted bugs in the exact point-in-solid test (each must be caught on the unit cube):\n");
    // points aligned with the face diagonal (0.25, 0.25) and with an edge, where a ray hits an edge or vertex exactly
    struct P { double p[3]; bool inside; };
    std::vector<P> pts; for (double y : {0.25, 0.5, 0.75, 0.0, -0.25, 1.0}) for (double z : {0.25, 0.5, 0.75, 0.0, -0.25, 1.0}) for (double x : {-1.0, 0.125, 0.5, 2.0}) {
        bool in = x > 0 && x < 1 && y > 0 && y < 1 && z > 0 && z < 1; pts.push_back({{x, y, z}, in}); }
    int wrong0 = 0, wrong1 = 0, wrong3 = 0, bnd = 0;
    for (auto& q : pts) {
        Pip r0 = classifyPoint(A, q.p, 0), r1 = classifyPoint(A, q.p, 1), r3 = classifyPoint(A, q.p, 3);
        bool onB = (q.p[1] == 0 || q.p[1] == 1 || q.p[2] == 0 || q.p[2] == 1) && q.p[0] >= 0 && q.p[0] <= 1 && q.p[1] >= 0 && q.p[1] <= 1 && q.p[2] >= 0 && q.p[2] <= 1;
        if (r0.boundary) { bnd++; if (!onB && !(q.p[0] == 0 || q.p[0] == 1)) wrong0++; continue; }
        if (onB) { wrong0++; continue; }
        wrong0 += r0.inside != q.inside; wrong1 += r1.inside != q.inside; wrong3 += r3.inside != q.inside;
    }
    printf("  %zu points (rays through faces, edges, vertices and diagonals), %d on the surface skipped\n", pts.size(), bnd);
    expect("correct classifier agrees with the analytic answer everywhere", wrong0, 0);
    expect("mutant 1 (no symbolic tie-break) is caught", wrong1 > 0, 1);
    expect("mutant 3 (ignore the crossing direction) is caught", wrong3 > 0, 1);
    // consistency: swapping x and y changes the ray direction relative to the mesh; the classification must not change
    // consistency: permuting the coordinate axes of mesh AND point changes the ray direction relative to the mesh; the classification must not change
    {
        auto readM = [](const char* path, Mesh& m) { return readMesh(path, m); };
        Mesh sph; bool have = readM("build/corpus/primitives/icosphere_3.msh", sph) || readM("poc/gpu/build/corpus/primitives/icosphere_3.msh", sph) || readM("../build/corpus/primitives/icosphere_3.msh", sph);
        if (have) {
            Mesh sw = sph; for (size_t i = 0; i < sw.nv(); i++) std::swap(sw.v[3*i], sw.v[3*i+1]);
            std::mt19937_64 rng(5); int disagree = 0, n = 0, skip = 0;
            for (int i = 0; i < 600; i++) {
                double p[3]; for (int k = 0; k < 3; k++) p[k] = (std::floor(((double)(rng() >> 11) / 9007199254740992.0 * 2.4 - 1.2) * 4096) + 0.5) / 4096;
                double q[3] = {p[1], p[0], p[2]};
                Pip a = classifyPoint(sph, p), b = classifyPoint(sw, q);
                if (a.boundary || b.boundary) { skip++; continue; }
                n++; disagree += a.inside != b.inside;
            }
            printf("  icosphere_3: %d points, %d skipped on the surface\n", n, skip);
            expect("classification is identical after swapping x and y (a different ray direction)", disagree, 0);
        } else printf("  (icosphere_3 not found; run mesh/corpus/gen_corpus.sh first; swap test skipped)\n");
    }
    printf("%s\n", bad ? "SELFTEST FAIL" : "SELFTEST PASS");
    return bad ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "selftest") return selftest();
    if (argc >= 6 && std::string(argv[1]) == "score") {
        std::string op = argv[4]; int o = op == "union" ? 0 : op == "intersection" ? 1 : op == "difference" ? 2 : -1;
        if (o < 0) { fprintf(stderr, "bad op\n"); return 2; }
        return score(argv[2], argv[3], o, argv[5], argc > 6 ? atoi(argv[6]) : 5000);
    }
    fprintf(stderr, "usage: mesh_compare score <corpusdir> <caseId> <union|intersection|difference> <result.msh> [points] | selftest\n");
    return 2;
}
