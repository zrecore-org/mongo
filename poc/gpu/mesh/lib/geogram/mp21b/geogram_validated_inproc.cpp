// MP21b in-process variant: the same validator (validator.h) in front of the unchanged geogram_runner main() (runner.cpp is #included with main renamed),
// so the validated path costs no second process start. Same CLI/exit codes as geogram_validated. `validate` mode as well.
#define main geogram_runner_main
#include "../runner.cpp"
#undef main
#include "validator.h"

int main(int argc, char** argv) {
    auto check = [&](const char* path, const char* who) -> int {
        mesh::Mesh m; if (!mesh::readMesh(path, m)) { fprintf(stderr, "ERROR: cannot read %s (%s)\n", path, who); return 4; }
        std::string r = mp21b::validate(m); if (!r.empty()) { fprintf(stderr, "REFUSED: invalid input %s: %s\n", who, r.c_str()); return 3; }
        return 0;
    };
    if (argc >= 6 && std::string(argv[1]) == "boolean") { if (int c = check(argv[3], "A")) return c; if (int c = check(argv[4], "B")) return c; }
    return geogram_runner_main(argc, argv);
}
