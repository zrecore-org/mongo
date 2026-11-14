// MP24: geogram_fidelity, the assembled high-fidelity boolean pipeline in ONE process:
//   exact validation (mp21b/validator.h)  ->  exact re-centring  ->  Geogram boolean (the runner.cpp boolean call; MP20+MP22 patched library)  ->  translate back.
//   geogram_fidelity boolean <union|intersection|difference> <A.msh> <B.msh> <out.msh>
// Same contract as geogram_runner: exit 0 ok, 3 REFUSED (reason on stderr: invalid input, translated span needs more than 53 bits, or Geogram threw), 4 error/usage.
// Re-centring rule (PLAN.md MP24): c = (c_x, c_y, c_z), each a multiple of 2^e, e = the smallest exponent of any SET BIT among all coordinates of A and B,
// nearest such multiple to the bounding-box centre of A and B together. Every coordinate minus c must be EXACTLY representable (checked per coordinate with
// TwoSum: error term 0); if one is not, the span after translation needs more than 53 bits and the pipeline refuses; it never rounds.
// Translate back: one IEEE addition y + c per result coordinate. Where it rounds (constructed points) the error is exact (TwoSum) and is measured and reported
// as the maximum over all coordinates in ulps of the original-frame value. It is the representation limit and is printed, never hidden.
// Planted mutants (compile with -DMUT_...): WRONGC (c = the plain bounding-box midpoint, not aligned to 2^e), NOBACK (translate-back skipped),
// ROUND (rounds the translation instead of refusing). Optional, outside the CLI contract: GEOGRAM_FIDELITY_LOG=<file> appends one line per run
// ("<out path>\t<fidelity line>") for the scoring tools; GEOGRAM_RUNNER_THREADS as in geogram_runner.
#define main geogram_runner_main
#include "../runner.cpp"
#undef main
#include "../mp21b/validator.h"
#include <cfloat>
#include <cmath>
#include <fcntl.h>

namespace {
struct TwoSum { double s, err; };
inline TwoSum twoSum(double a, double b) {   // Knuth: s + err == a + b exactly (no overflow; -ffp-contract=off, no fast-math)
    double s = a + b, bb = s - a; return {s, (a - (s - bb)) + (b - bb)};
}
// exponent of the lowest set bit of a nonzero finite double (value = odd * 2^e)
inline int lowestSetBitExp(double x) {
    int ex; double f = std::frexp(std::fabs(x), &ex); long long mant = (long long)std::ldexp(f, 53);
    return ex - 53 + __builtin_ctzll((unsigned long long)mant);
}
struct Plan { double c[3] = {0, 0, 0}; int e = 0; bool ok = true; std::string reason; size_t roundedIn = 0; double halfSpanUnits = 0; };

Plan planRecentre(const mesh::Mesh& a, const mesh::Mesh& b, mesh::Mesh& ta, mesh::Mesh& tb) {
    Plan p; double lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY}; int e = INT32_MAX;
    for (const mesh::Mesh* m : {&a, &b}) for (size_t i = 0; i < m->nv(); i++) for (int k = 0; k < 3; k++) {
        double x = m->v[3 * i + k]; lo[k] = std::min(lo[k], x); hi[k] = std::max(hi[k], x);
        if (x != 0) e = std::min(e, lowestSetBitExp(x));
    }
    if (e == INT32_MAX) e = 0;
    p.e = e;
    for (int k = 0; k < 3; k++) {
        double mid = lo[k] / 2 + hi[k] / 2;
#ifdef MUT_WRONGC
        p.c[k] = mid;                                          // planted bug: not aligned to 2^e
#else
        double s = std::ldexp(mid, -e);                         // exact unless it overflows
        if (!std::isfinite(s)) { p.ok = false; p.reason = "bounding box centre not representable on the 2^e grid"; return p; }
        if (std::fabs(s) < 4503599627370496.0) s = std::nearbyint(s);   // |s| >= 2^52 is already an integer
        p.c[k] = std::ldexp(s, e);                              // exact: s is an integer double
#endif
        if (!std::isfinite(p.c[k])) { p.ok = false; p.reason = "centre overflow"; return p; }
    }
    ta = a; tb = b;
    for (mesh::Mesh* m : {&ta, &tb}) for (size_t i = 0; i < m->nv(); i++) for (int k = 0; k < 3; k++) {
        double x = m->v[3 * i + k];
        TwoSum t = twoSum(x, -p.c[k]);
        if (!std::isfinite(t.s)) { p.ok = false; p.reason = "overflow in translation"; return p; }
        if (t.err != 0) {
            p.roundedIn++;
#ifndef MUT_ROUND
            if (p.ok) { p.ok = false; char buf[200]; snprintf(buf, sizeof buf, "re-centring needs more than 53 bits (axis %d: coordinate minus c is not exactly representable; lowest set bit 2^%d, half span %.4g units)", k, e, 0.5 * (hi[k] - lo[k]) / std::ldexp(1.0, e)); p.reason = buf; }
#endif
        }
        m->v[3 * i + k] = t.s;
    }
    return p;
}
}  // namespace

int main(int argc, char** argv) {
    int realOut = dup(1); dup2(2, 1); (void)realOut;
    if (argc != 6 || std::string(argv[1]) != "boolean") { fprintf(stderr, "usage: geogram_fidelity boolean <union|intersection|difference> A.msh B.msh out.msh\n"); return 4; }
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
        mesh::Mesh ta, tb; Plan plan = planRecentre(a, b, ta, tb);
        double recentreMs = msSince(t1);
        if (!plan.ok) { fprintf(stderr, "REFUSED: %s\n", plan.reason.c_str()); return 3; }

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
#ifndef MUT_NOBACK
        for (size_t i = 0; i < r.nv(); i++) for (int k = 0; k < 3; k++) {
            TwoSum t = twoSum(r.v[3 * i + k], plan.c[k]);
            if (!std::isfinite(t.s)) { fprintf(stderr, "INTERNAL: overflow translating back\n"); return 4; }
            if (t.err != 0) {
                rounded++;
                double m = std::fabs(t.s), u = std::nextafter(m, INFINITY) - m; if (!std::isfinite(u)) u = m - std::nextafter(m, 0.0);
                maxUlp = std::max(maxUlp, std::fabs(t.err) / u);
            }
            r.v[3 * i + k] = t.s;
        }
#endif
        double backMs = msSince(t5);
        char line[600];
        snprintf(line, sizeof line, "fidelity: e=%d c=(%.17g,%.17g,%.17g) translate_back_max_err_ulps=%.6g rounded_coords=%zu/%zu validate_ms=%.3f recentre_ms=%.3f back_ms=%.3f convert_ms=%.3f boolean_ms=%.3f out_nv=%zu out_nt=%zu",
                 plan.e, plan.c[0], plan.c[1], plan.c[2], maxUlp, rounded, ncoord, validateMs, recentreMs, backMs, convertIn + convertOut, boolMs, r.nv(), r.nt());
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
