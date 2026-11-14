// MP24b: geogram_fidelity2, pipeline v2 (PLAN.md "MP24b amended rule") in ONE process:
//   exact validation -> exact power-of-two scale 2^-s -> exact re-centring if representable, else c = 0 if |bbox centre| <= T x largest bbox side, else REFUSE (hard limit)
//   -> Geogram boolean (runner.cpp call, MP20+MP22 patched library) -> translate back (one IEEE addition) -> exact scale back (ldexp).
//   geogram_fidelity2 boolean <union|intersection|difference> <A.msh> <B.msh> <out.msh>
// Exit 0 ok, 3 REFUSED (reason on stderr; hard limits start "REFUSED: hard limit"), 4 error/usage. stderr always ends with one line "fidelity2: path=<recentred|c0|refused> ..."
// (also before a refusal), with the translate-back error in ulps and per-stage ms; GEOGRAM_FIDELITY_LOG=<file> appends "<out path>\t<line>" per successful run.
// Scale: s = smallest integer >= 0 with max|coordinate| < 2^18 (A and B together). Refused (hard limit, "underflow") if any nonzero coordinate would become subnormal or inexact.
// Re-centring (MP24 rule): c = per-axis multiple of 2^e (e = lowest set bit over all scaled coordinates) nearest the bbox centre; used only if every x - c is EXACT (TwoSum error 0).
// Otherwise c = 0 when max_k |centre_k| <= T x (largest bbox side of A and B together), T = 2^T_EXP (chosen by the development sweep, lib/geogram/mp24b/DEV_SWEEP.md); else refuse.
// Mutants (-D...): MUT_NOSCALE (s = 0), MUT_SCALE19 (bound 2^19), MUT_C0 (never re-centre, never refuse), MUT_NOBACK (scale-back skipped), MUT_TLARGE (T = 2^60).
#define main geogram_runner_main
#include "../runner.cpp"
#undef main
#include "../mp21b/validator.h"
#include <cfloat>
#include <cmath>
#include <fcntl.h>

#ifndef T_EXP
#define T_EXP 12
#endif
#ifdef MUT_TLARGE
#undef T_EXP
#define T_EXP 60
#endif
#ifdef MUT_SCALE19
static const int kBoundExp = 19;
#else
static const int kBoundExp = 18;
#endif

namespace {
struct TwoSum { double s, err; };
inline TwoSum twoSum(double a, double b) { double s = a + b, bb = s - a; return {s, (a - (s - bb)) + (b - bb)}; }
inline int lowestSetBitExp(double x) {
    int ex; double f = std::frexp(std::fabs(x), &ex); long long mant = (long long)std::ldexp(f, 53);
    return ex - 53 + __builtin_ctzll((unsigned long long)mant);
}
struct Plan { double c[3] = {0, 0, 0}; int e = 0, s = 0; const char* path = "refused"; bool ok = true; std::string reason; double ratio = 0; };

// exact scale by 2^-s into ta, tb; false (reason) if some nonzero coordinate would underflow (subnormal or inexact)
bool scaleExact(const mesh::Mesh& a, const mesh::Mesh& b, int s, mesh::Mesh& ta, mesh::Mesh& tb, std::string& why) {
    ta = a; tb = b;
    for (mesh::Mesh* m : {&ta, &tb}) for (double& x : m->v) {
        if (x == 0 || s == 0) continue;
        double y = std::ldexp(x, -s);
        if (!(std::fabs(y) >= DBL_MIN) || std::ldexp(y, s) != x) { char buf[160]; snprintf(buf, sizeof buf, "hard limit (underflow): scaling by 2^-%d would underflow a nonzero coordinate (%.17g)", s, x); why = buf; return false; }
        x = y;
    }
    return true;
}

Plan plan(const mesh::Mesh& a, const mesh::Mesh& b, mesh::Mesh& ta, mesh::Mesh& tb) {
    Plan p; double mx = 0;
    for (const mesh::Mesh* m : {&a, &b}) for (double x : m->v) mx = std::max(mx, std::fabs(x));
#ifndef MUT_NOSCALE
    if (mx >= std::ldexp(1.0, kBoundExp)) { int ex; std::frexp(mx, &ex); p.s = ex - kBoundExp; }   // mx < 2^ex, so mx * 2^-s < 2^kBoundExp
#endif
    mesh::Mesh sa, sb;
    if (!scaleExact(a, b, p.s, sa, sb, p.reason)) { p.ok = false; return p; }
    double lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY}; int e = INT32_MAX;
    for (const mesh::Mesh* m : {&sa, &sb}) for (size_t i = 0; i < m->nv(); i++) for (int k = 0; k < 3; k++) {
        double x = m->v[3 * i + k]; lo[k] = std::min(lo[k], x); hi[k] = std::max(hi[k], x);
        if (x != 0) e = std::min(e, lowestSetBitExp(x));
    }
    if (e == INT32_MAX) e = 0;
    p.e = e;
    double side = 0, cen = 0, cand[3];
    for (int k = 0; k < 3; k++) { side = std::max(side, hi[k] - lo[k]); double mid = lo[k] / 2 + hi[k] / 2; cen = std::max(cen, std::fabs(mid)); cand[k] = mid; }
    p.ratio = side > 0 ? cen / side : 0;
    ta = sa; tb = sb;
    bool exactOk = true;
#ifndef MUT_C0
    for (int k = 0; k < 3 && exactOk; k++) {
        double sg = std::ldexp(cand[k], -e);
        if (!std::isfinite(sg)) { exactOk = false; break; }
        if (std::fabs(sg) < 4503599627370496.0) sg = std::nearbyint(sg);
        p.c[k] = std::ldexp(sg, e);
        if (!std::isfinite(p.c[k])) exactOk = false;
    }
    if (exactOk) {
        mesh::Mesh ra = sa, rb = sb;
        for (mesh::Mesh* m : {&ra, &rb}) for (size_t i = 0; i < m->nv() && exactOk; i++) for (int k = 0; k < 3; k++) {
            TwoSum t = twoSum(m->v[3 * i + k], -p.c[k]);
            if (!std::isfinite(t.s) || t.err != 0) { exactOk = false; break; }
            m->v[3 * i + k] = t.s;
        }
        if (exactOk) { ta = ra; tb = rb; p.path = "recentred"; return p; }
    }
#else
    exactOk = false;
#endif
    p.c[0] = p.c[1] = p.c[2] = 0;
#ifdef MUT_C0
    p.path = "c0"; return p;
#else
    if (p.ratio <= std::ldexp(1.0, T_EXP)) { p.path = "c0"; return p; }
    p.ok = false; p.path = "refused";
    char buf[260]; snprintf(buf, sizeof buf, "hard limit (far from the origin): not exactly re-centrable and |bbox centre| / largest side = %.6g > T = 2^%d", p.ratio, T_EXP);
    p.reason = buf; return p;
#endif
}
}  // namespace

int main(int argc, char** argv) {
    int realOut = dup(1); dup2(2, 1); (void)realOut;
    if (argc != 6 || std::string(argv[1]) != "boolean") { fprintf(stderr, "usage: geogram_fidelity2 boolean <union|intersection|difference> A.msh B.msh out.msh\n"); return 4; }
    try {
        GEO::initialize(GEO::GEOGRAM_INSTALL_ALL);
        GEO::set_assert_mode(GEO::ASSERT_THROW);
        if (const char* th = getenv("GEOGRAM_RUNNER_THREADS")) GEO::Process::set_max_threads((GEO::index_t)atoi(th));
        if (!getenv("GEOGRAM_RUNNER_LOG")) GEO::Logger::instance()->set_quiet(true);
        std::string op = argv[2];
        const char* opName = op == "union" ? "A+B" : op == "intersection" ? "A*B" : op == "difference" ? "A-B" : nullptr;
        if (!opName) { fprintf(stderr, "bad operation '%s'\n", op.c_str()); return 4; }
        mesh::Mesh a, b;
        if (!mesh::readMesh(argv[3], a)) { fprintf(stderr, "cannot read %s\n", argv[3]); return 4; }
        if (!mesh::readMesh(argv[4], b)) { fprintf(stderr, "cannot read %s\n", argv[4]); return 4; }

        auto t0 = Clock::now();
        if (std::string r = mp21b::validate(a); !r.empty()) { fprintf(stderr, "REFUSED: invalid input A: %s\n", r.c_str()); return 3; }
        if (std::string r = mp21b::validate(b); !r.empty()) { fprintf(stderr, "REFUSED: invalid input B: %s\n", r.c_str()); return 3; }
        double validateMs = msSince(t0);

        auto t1 = Clock::now();
        mesh::Mesh ta, tb; Plan pl = plan(a, b, ta, tb);
        double planMs = msSince(t1);
        if (!pl.ok) { fprintf(stderr, "fidelity2: path=refused s=%d ratio=%.6g T_exp=%d\nREFUSED: %s\n", pl.s, pl.ratio, T_EXP, pl.reason.c_str()); return 3; }

        auto t2 = Clock::now();
        GEO::Mesh ga, gb, gr; toGeo(ta, ga); toGeo(tb, gb);
        double convertIn = msSince(t2);
        auto t3 = Clock::now();
        try { GEO::mesh_boolean_operation(gr, ga, gb, opName, GEO::MESH_BOOL_OPS_DEFAULT); }
        catch (const std::exception& e) { fprintf(stderr, "REFUSED: geogram threw: %s\n", e.what()); return 3; }
        double boolMs = msSince(t3);
        auto t4 = Clock::now();
        mesh::Mesh r; fromGeo(gr, r);
        double convertOut = msSince(t4);

        auto t5 = Clock::now();
        double maxUlp = 0; size_t rounded = 0, ncoord = r.v.size();
        for (size_t i = 0; i < r.nv(); i++) for (int k = 0; k < 3; k++) {
            TwoSum t = twoSum(r.v[3 * i + k], pl.c[k]);
            if (!std::isfinite(t.s)) { fprintf(stderr, "INTERNAL: overflow translating back\n"); return 4; }
            if (t.err != 0) {
                rounded++;
                double m = std::fabs(t.s), u = std::nextafter(m, INFINITY) - m; if (!std::isfinite(u)) u = m - std::nextafter(m, 0.0);
                maxUlp = std::max(maxUlp, std::fabs(t.err) / u);
            }
            double y = t.s;
#ifndef MUT_NOBACK
            if (pl.s) { y = std::ldexp(y, pl.s); if (!std::isfinite(y)) { fprintf(stderr, "INTERNAL: overflow scaling back\n"); return 4; } }
#endif
            r.v[3 * i + k] = y;
        }
        double backMs = msSince(t5);
        char line[700];
        snprintf(line, sizeof line, "fidelity2: path=%s s=%d ratio=%.6g T_exp=%d e=%d c=(%.17g,%.17g,%.17g) translate_back_max_err_ulps=%.6g rounded_coords=%zu/%zu validate_ms=%.3f recentre_ms=%.3f back_ms=%.3f convert_ms=%.3f boolean_ms=%.3f out_nv=%zu out_nt=%zu",
                 pl.path, pl.s, pl.ratio, T_EXP, pl.e, pl.c[0], pl.c[1], pl.c[2], maxUlp, rounded, ncoord, validateMs, planMs, backMs, convertIn + convertOut, boolMs, r.nv(), r.nt());
        fprintf(stderr, "%s\n", line);
        if (const char* lf = getenv("GEOGRAM_FIDELITY_LOG")) {
            std::string s = std::string(argv[5]) + "\t" + line + "\n";
            int fd = open(lf, O_WRONLY | O_APPEND | O_CREAT, 0644); if (fd >= 0) { if (write(fd, s.data(), s.size()) < 0) {} close(fd); }
        }
        if (!mesh::writeMesh(argv[5], r)) { fprintf(stderr, "cannot write %s\n", argv[5]); return 4; }
        return 0;
    } catch (const std::exception& e) { fprintf(stderr, "INTERNAL: exception: %s\n", e.what()); return 4; }
    catch (...) { fprintf(stderr, "INTERNAL: unknown exception\n"); return 4; }
}
