// MP21b: exact pre-validator in front of geogram_runner.
//   geogram_validated boolean <union|intersection|difference> A.msh B.msh out.msh   validate A and B, exit 3 "REFUSED: <reason>" if malformed, else exec the runner unchanged
//   geogram_validated validate A.msh [B.msh ...]                                      validation only (exit 0 ok, 3 refused)
//   geogram_validated measure A.msh                                                   passed straight to the runner
// Runner path: env GEOGRAM_VALIDATED_RUNNER (default storage/mesh/geogram/bin/geogram_runner). Exit codes as the runner: 0 ok, 3 refused, 4 error.
// Checks (exact predicates from harness/exact.h): finite coordinates; closed 2-manifold as returned (by index) and after welding exactly equal coordinates
// (every undirected edge in exactly two triangles, once in each direction); no duplicate or inverted-twin triangle (same vertex set twice); no zero-area
// triangle (exact); exact total volume > 0 (outward orientation). Compile-time planted mutants: -DMUT_NODUP -DMUT_NOORIENT -DMUT_ONEBOUNDARY.
#include "validator.h"
#include <unistd.h>
#include <string>
using namespace mesh;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage error\n"); return 4; }
    std::string cmd = argv[1];
    auto check = [&](const char* path, const char* who) -> int {
        Mesh m; if (!readMesh(path, m)) { fprintf(stderr, "ERROR: cannot read %s (%s)\n", path, who); return 4; }
        std::string r = mp21b::validate(m); if (!r.empty()) { fprintf(stderr, "REFUSED: invalid input %s: %s\n", who, r.c_str()); return 3; }
        return 0;
    };
    if (cmd == "validate") { int worst = 0; for (int i = 2; i < argc; i++) { int c = check(argv[i], argv[i]); if (c) { worst = c; } } return worst; }
    if (cmd == "boolean" && argc == 6) { if (int c = check(argv[3], "A")) return c; if (int c = check(argv[4], "B")) return c; }
    else if (!(cmd == "measure" && argc == 3)) { fprintf(stderr, "usage error\n"); return 4; }
    const char* runner = getenv("GEOGRAM_VALIDATED_RUNNER");
    if (!runner) runner = "/Volumes/CrucialX9/Projects/mongo/storage/mesh/geogram/bin/geogram_runner";
    std::vector<char*> args; args.push_back((char*)runner); for (int i = 1; i < argc; i++) args.push_back(argv[i]); args.push_back(nullptr);
    execv(runner, args.data()); fprintf(stderr, "ERROR: cannot exec %s\n", runner); return 4;
}
