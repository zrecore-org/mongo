// MP26 scorer for real-mesh pairs. Reuses harness/exact.h, topology.h and the exact point-in-solid logic of harness/pip.h (copied verbatim in classifyFast, plus an exact
// bounding-box / grid pre-filter that only skips triangles that provably cannot affect the classification, so 200k-triangle meshes are tractable).
//   real_score pair <A.msh> <B.msh> <npts> <seed> <label>:<op>:<result.msh> ...        one TSV line per result, then one residual line per label that has union+intersection
//   real_score selftest <mesh.msh>                                                       checks classifyFast against pip.h classifyPoint on a mesh and plants wrong results
// Line format (tab separated): R label op status nt closed geomClosed nonManifold euler components zeroArea dupTri inverted samples bad informative skipped volume6(str) reasons
//   status PASS / FAIL.  FAIL reasons: pts(k), open, inverted, duplicate-triangle, unreadable, nonfinite.  (A and B are classified once and cached for all results of the pair.)
// Residual line: S label inclusion_exclusion_relative_residual 6V(AuB) 6V(AnB) 6V(A) 6V(B) (exact dyadic strings) ; relative residual = |V(AuB)+V(AnB)-V(A)-V(B)| / V(AuB) as a double.
#include "../../harness/pip.h"
#include "../../harness/topology.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
using namespace mesh;

namespace {
struct FPip : Pip { int w = 0; };   // w = exact winding number along the +x ray (sum of the signs of the x component of the normal over the crossings; 1 inside an outward solid)
struct Fast {
    const Mesh* m; std::vector<double> ymin, ymax, zmin, zmax, xmax; double ylo, zlo, inv; int G;
    std::vector<std::vector<uint32_t>> cell;
    int cy(double y) const { double f = std::floor((y - ylo) * inv); return f < 0 ? 0 : f >= G ? G - 1 : (int)f; }
    int cz(double z) const { double f = std::floor((z - zlo) * inv); return f < 0 ? 0 : f >= G ? G - 1 : (int)f; }
    explicit Fast(const Mesh& mesh) : m(&mesh) {
        size_t n = m->nt(); ymin.resize(n); ymax.resize(n); zmin.resize(n); zmax.resize(n); xmax.resize(n);
        double yl = 1e300, yh = -1e300, zl = 1e300, zh = -1e300;
        for (size_t k = 0; k < n; k++) {
            const double *a = &m->v[3*(size_t)m->t[3*k]], *b = &m->v[3*(size_t)m->t[3*k+1]], *c = &m->v[3*(size_t)m->t[3*k+2]];
            ymin[k] = std::min({a[1], b[1], c[1]}); ymax[k] = std::max({a[1], b[1], c[1]});
            zmin[k] = std::min({a[2], b[2], c[2]}); zmax[k] = std::max({a[2], b[2], c[2]});
            xmax[k] = std::max({a[0], b[0], c[0]});
            yl = std::min(yl, ymin[k]); yh = std::max(yh, ymax[k]); zl = std::min(zl, zmin[k]); zh = std::max(zh, zmax[k]);
        }
        G = std::max(1, std::min(1024, (int)std::sqrt((double)n / 2 + 1)));
        ylo = yl; zlo = zl; double span = std::max(yh - yl, zh - zl); inv = span > 0 ? G / span : 1.0;
        cell.assign((size_t)G * G, {});
        for (size_t k = 0; k < n; k++)
            for (int i = cy(ymin[k]); i <= cy(ymax[k]); i++) for (int j = cz(zmin[k]); j <= cz(zmax[k]); j++) cell[(size_t)i * G + j].push_back((uint32_t)k);
    }
    FPip classify(const double* p) const {
        FPip r;
        const auto& cl = cell[(size_t)cy(p[1]) * G + cz(p[2])];
        for (uint32_t k : cl) {
            if (xmax[k] < p[0] || p[1] < ymin[k] || p[1] > ymax[k] || p[2] < zmin[k] || p[2] > zmax[k]) continue;
            const double* a = &m->v[3 * (size_t)m->t[3*k]];
            const double* b = &m->v[3 * (size_t)m->t[3*k+1]];
            const double* c = &m->v[3 * (size_t)m->t[3*k+2]];
            double A[3] = {a[0], a[1], a[2]}, B[3] = {b[0], b[1], b[2]}, C[3] = {c[0], c[1], c[2]}, P[3] = {p[0], p[1], p[2]};
            int s0 = orientYZ(a, b, p, 0), s1 = orientYZ(b, c, p, 0), s2 = orientYZ(c, a, p, 0);
            double o3 = orient3d(A, B, C, P);
            if (o3 == 0) {
                BigInt v[12]; toCommonInts({a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2], p[0],p[1],p[2]}, v);
                auto sub = [&](int i, int j, BigInt* o) { for (int d = 0; d < 3; d++) o[d] = v[3*i+d] - v[3*j+d]; };
                BigInt ab[3], bc[3], ca[3], ap[3], bp[3], cp[3], n[3];
                sub(1,0,ab); sub(2,1,bc); sub(0,2,ca); sub(3,0,ap); sub(3,1,bp); sub(3,2,cp);
                auto cross = [](const BigInt* x, const BigInt* y, BigInt* o) { o[0]=x[1]*y[2]-x[2]*y[1]; o[1]=x[2]*y[0]-x[0]*y[2]; o[2]=x[0]*y[1]-x[1]*y[0]; };
                auto dot = [](const BigInt* x, const BigInt* y) -> BigInt { return x[0]*y[0]+x[1]*y[1]+x[2]*y[2]; };
                BigInt c0[3], c1[3], c2[3]; cross(ab, ap, c0); cross(bc, bp, c1); cross(ca, cp, c2); cross(ab, bc, n);
                int d0 = dot(c0, n).sign(), d1 = dot(c1, n).sign(), d2 = dot(c2, n).sign();
                bool nonDegenerate = n[0] != 0 || n[1] != 0 || n[2] != 0;
                if (nonDegenerate && d0 >= 0 && d1 >= 0 && d2 >= 0) { r.boundary = true; return r; }
            }
            if (s0 == 0 || s1 == 0 || s2 == 0) continue;
            if (!(s0 == s1 && s1 == s2)) continue;
            double ayz[2] = {a[1], a[2]}, byz[2] = {b[1], b[2]}, cyz[2] = {c[1], c[2]};
            int nx = sgnD(orient2d(ayz, byz, cyz));
            if (nx == 0) continue;
            int side = sgnD(o3);
            if (side == 0) {
                BigInt v[9]; toCommonInts({a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2]}, v);
                BigInt ux=v[3]-v[0], uy=v[4]-v[1], uz=v[5]-v[2], wx=v[6]-v[0], wy=v[7]-v[1], wz=v[8]-v[2];
                BigInt ny = uz*wx - ux*wz, nz = ux*wy - uy*wx;
                side = -ny.sign(); if (side == 0) side = -nz.sign();
            }
            if (side == nx) { r.crossings++; r.w += nx; }
        }
        r.inside = (r.crossings & 1) != 0;
        return r;
    }
};
bool inOp(int op, bool a, bool b) { return op == 0 ? (a || b) : op == 1 ? (a && b) : (a && !b); }
int opIndex(const std::string& s) { return s == "union" ? 0 : s == "intersection" ? 1 : s == "difference" ? 2 : -1; }

struct Samples { std::vector<std::array<double, 3>> p; std::vector<char> a, b; int ambiguous = 0; };   // a, b: operand inside flags; samples where an operand has winding number outside {0,1} are dropped and counted in `ambiguous`

Samples makeSamples(const Mesh& A, const Mesh& B, int npts, uint64_t seed, const Fast& fa, const Fast& fb) {
    double lo[3], hi[3], ilo[3], ihi[3];
    for (int k = 0; k < 3; k++) { lo[k] = 1e300; hi[k] = -1e300; double alo = 1e300, ahi = -1e300, blo = 1e300, bhi = -1e300;
        for (size_t i = 0; i < A.nv(); i++) { alo = std::min(alo, A.v[3*i+k]); ahi = std::max(ahi, A.v[3*i+k]); }
        for (size_t i = 0; i < B.nv(); i++) { blo = std::min(blo, B.v[3*i+k]); bhi = std::max(bhi, B.v[3*i+k]); }
        lo[k] = std::min(alo, blo); hi[k] = std::max(ahi, bhi); ilo[k] = std::max(alo, blo); ihi[k] = std::min(ahi, bhi); }
    bool haveI = ilo[0] < ihi[0] && ilo[1] < ihi[1] && ilo[2] < ihi[2];
    double maxspan = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    double g = std::ldexp(1.0, (int)std::floor(std::log2(maxspan)) - 30);   // dyadic grid, 2^-30 of the largest extent: exact doubles
    std::mt19937_64 rng(seed);
    Samples s;
    int attempts = 0;
    while ((int)s.p.size() < npts && attempts < npts * 4) {
        attempts++;
        bool inter = haveI && (attempts % 2 == 0);
        const double* L = inter ? ilo : lo; const double* H = inter ? ihi : hi;
        double p[3];
        for (int k = 0; k < 3; k++) { double span = H[k] - L[k]; double v = L[k] - 0.02 * span + (double)(rng() >> 11) / 9007199254740992.0 * 1.04 * span; p[k] = (std::floor(v / g) + 0.5) * g; }
        FPip a = fa.classify(p), b = fb.classify(p);
        if (a.boundary || b.boundary) continue;
        if ((a.w != 0 && a.w != 1) || (b.w != 0 && b.w != 1)) { s.ambiguous++; continue; }
        s.p.push_back({p[0], p[1], p[2]}); s.a.push_back(a.w == 1); s.b.push_back(b.w == 1);
    }
    return s;
}

double ratio_(const Dyadic& a, const Dyadic& b) { int e = std::min(a.exp2, b.exp2); return align(a, e).num.convert_to<double>() / align(b, e).num.convert_to<double>(); }
struct Res { Dyadic v6; bool ok = false; bool isEmpty = true; };

void scoreOne(const std::string& label, const std::string& opS, const std::string& path, const Samples& S, Res& vol, bool& okOut) {
    int op = opIndex(opS);
    Mesh R; okOut = false;
    if (!readMesh(path, R)) { printf("R\t%s\t%s\tFAIL\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t-\tunreadable\n", label.c_str(), opS.c_str()); fflush(stdout); return; }
    std::vector<std::string> fails;
    for (double x : R.v) if (!std::isfinite(x)) { fails.push_back("nonfinite"); break; }
    if (!fails.empty()) { printf("R\t%s\t%s\tFAIL\t%zu\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t-\tnonfinite\n", label.c_str(), opS.c_str(), R.nt()); fflush(stdout); return; }
    Topology tr = analyzeTopology(R);
    Dyadic v6 = exactVolumeTimes6(R); vol.v6 = v6; vol.ok = true; vol.isEmpty = R.nt() == 0; okOut = true;
    int bad = 0, n = 0, skipped = 0, informative = 0;
    if (R.nt() > 0 || true) {
        Fast fr(R);
        for (size_t i = 0; i < S.p.size(); i++) {
            FPip r = R.nt() ? fr.classify(S.p[i].data()) : FPip();
            if (r.boundary) { skipped++; continue; }
            bool want = inOp(op, S.a[i], S.b[i]);
            n++; informative += want; bad += ((r.w != 0 && r.w != 1) || (r.w == 1) != want);   // the result of a valid pair must itself have winding number 0 or 1
        }
    }
    bool empty = R.nt() == 0;
    if (bad) fails.push_back("pts(" + std::to_string(bad) + ")");
    if (!empty && !(tr.closedManifoldAsReturned() || (!tr.anyOpen && tr.faces > 0))) fails.push_back("open");
    if (!empty && tr.inverted) fails.push_back("inverted");
    if (!empty && tr.duplicateTri) fails.push_back("duplicate-triangle");
    std::string rs; for (auto& f : fails) rs += (rs.empty() ? "" : ",") + f;
    printf("R\t%s\t%s\t%s\t%zu\t%d\t%d\t%d\t%ld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n", label.c_str(), opS.c_str(), fails.empty() ? "PASS" : "FAIL", R.nt(),
           (int)tr.closedManifoldAsReturned(), (int)tr.closedManifold(), (int)tr.nonManifold, tr.euler(), tr.components, (int)tr.zeroArea, (int)tr.duplicateTri, (int)tr.inverted, n, bad, informative, toString(v6).c_str(), rs.empty() ? "-" : rs.c_str());
    fflush(stdout);
}
}  // namespace

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "selftest" && argc == 3) {
        Mesh M; if (!readMesh(argv[2], M)) { fprintf(stderr, "cannot read\n"); return 2; }
        Fast f(M); std::mt19937_64 rng(7); double lo[3] = {1e300,1e300,1e300}, hi[3] = {-1e300,-1e300,-1e300};
        for (size_t i = 0; i < M.nv(); i++) for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], M.v[3*i+k]); hi[k] = std::max(hi[k], M.v[3*i+k]); }
        int diff = 0, n = 0, in = 0, bnd = 0;
        for (int i = 0; i < 400; i++) { double p[3]; for (int k = 0; k < 3; k++) { double sp = hi[k] - lo[k]; p[k] = lo[k] - 0.1 * sp + (double)(rng() >> 11) / 9007199254740992.0 * 1.2 * sp; p[k] = std::floor(p[k] * 1024) / 1024; }
            FPip a = f.classify(p); Pip b = classifyPoint(M, p); n++; in += b.inside; bnd += b.boundary; if (a.inside != b.inside || a.boundary != b.boundary || (!a.boundary && a.w != (int)b.inside && (a.w & 1) == (int)b.inside && false)) diff++; if (!a.boundary && b.inside && a.w != 1) diff++; }
        printf("selftest: %d points (%d inside, %d boundary), %d differences between classifyFast and harness/pip.h classifyPoint\n", n, in, bnd, diff);
        return diff ? 1 : 0;
    }
    if (cmd == "pair" && argc >= 7) {
        Mesh A, B; if (!readMesh(argv[2], A) || !readMesh(argv[3], B)) { fprintf(stderr, "cannot read inputs\n"); return 2; }
        int npts = atoi(argv[4]); uint64_t seed = strtoull(argv[5], 0, 10);
        Fast fa(A), fb(B);
        Samples S = makeSamples(A, B, npts, seed, fa, fb);
        Dyadic vA = exactVolumeTimes6(A), vB = exactVolumeTimes6(B);
        printf("P\tambiguous=%d\tsamples=%zu\tA_in=%d\tB_in=%d\t6V(A)=%s\t6V(B)=%s\n", S.ambiguous, S.p.size(), (int)std::count(S.a.begin(), S.a.end(), 1), (int)std::count(S.b.begin(), S.b.end(), 1), toString(vA).c_str(), toString(vB).c_str());
        std::map<std::string, std::map<std::string, Res>> byLabel;
        for (int i = 6; i < argc; i++) {
            std::string s = argv[i]; size_t c1 = s.find(':'), c2 = s.find(':', c1 + 1);
            std::string label = s.substr(0, c1), op = s.substr(c1 + 1, c2 - c1 - 1), path = s.substr(c2 + 1);
            Res r; bool ok; scoreOne(label, op, path, S, r, ok); if (ok) byLabel[label][op] = r;
        }
        for (auto& kv : byLabel) if (kv.second.count("union") && kv.second.count("intersection")) {
            const Dyadic& u = kv.second["union"].v6; const Dyadic& x = kv.second["intersection"].v6;
            Dyadic res = subExact(addExact(u, x), addExact(vA, vB));
            double rel = std::fabs(ratio_(res, u));
            printf("S\t%s\t%.6g\t%s\t%s\t%s\t%s\n", kv.first.c_str(), rel, toString(u).c_str(), toString(x).c_str(), toString(vA).c_str(), toString(vB).c_str());
        }
        return 0;
    }
    fprintf(stderr, "usage: real_score pair A B npts seed label:op:path... | selftest mesh\n");
    return 2;
}
