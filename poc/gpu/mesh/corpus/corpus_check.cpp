// MP01 check: the manifest is true. For each adversarial case: A and B load, indices are valid, the exact volume of every box operand equals the
// analytic expectation, inclusion-exclusion holds for the expected volumes, and every tag that can be tested objectively (open, non-manifold-edge,
// inverted, unwelded-vertices, zero-area-triangle, duplicate-triangle, valid) is actually true of the meshes. Run: corpus_check <corpusdir>
#include "../harness/topology.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace mesh;
using Topo = Topology;
static Topo analyze(const Mesh& m) { return analyzeTopology(m); }
static bool parseDyadic(const std::string& s, Dyadic& d) {
    size_t star = s.find("*2^"); if (star == std::string::npos) return false;
    d.num = BigInt(s.substr(0, star)); d.exp2 = atoi(s.c_str() + star + 3); return true;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string dir = argv[1];
    std::ifstream f(dir + "/manifest.tsv"); std::string line; std::getline(f, line);
    int cases = 0, bad = 0, boxChecked = 0, tagChecks = 0;
    while (std::getline(f, line)) {
        std::vector<std::string> c; std::stringstream ss(line); std::string x; while (std::getline(ss, x, '\t')) c.push_back(x);
        if (c.size() < 12) { printf("bad manifest line: %s\n", line.c_str()); bad++; continue; }
        cases++;
        Mesh A, B;
        if (!readMesh(dir + "/adversarial/" + c[0] + "_A.msh", A) || !readMesh(dir + "/adversarial/" + c[0] + "_B.msh", B)) { printf("%s: cannot load\n", c[0].c_str()); bad++; continue; }
        if (A.nv() != std::stoul(c[3]) || A.nt() != std::stoul(c[4]) || B.nv() != std::stoul(c[5]) || B.nt() != std::stoul(c[6])) { printf("%s: counts differ from manifest\n", c[0].c_str()); bad++; }
        if (c[7] != "-") {
            Dyadic ea, eb, eu, ei, ed;
            if (!parseDyadic(c[7], ea) || !parseDyadic(c[8], eb) || !parseDyadic(c[9], eu) || !parseDyadic(c[10], ei) || !parseDyadic(c[11], ed)) { printf("%s: unparsable expectation\n", c[0].c_str()); bad++; continue; }
            // independent path: tetrahedron sums over the triangles versus the analytic interval product
            if (!equalExact(exactVolumeTimes6(A), ea)) { printf("%s: exact volume of A differs from expectation\n", c[0].c_str()); bad++; }
            if (!equalExact(exactVolumeTimes6(B), eb)) { printf("%s: exact volume of B differs from expectation\n", c[0].c_str()); bad++; }
            // 6V(union) = 6V(A) + 6V(B) - 6V(inter); 6V(diff) = 6V(A) - 6V(inter)
            if (!equalExact(eu, subExact(addExact(ea, eb), ei)) || !equalExact(ed, subExact(ea, ei))) { printf("%s: inclusion-exclusion violated\n", c[0].c_str()); bad++; }
            boxChecked++;
        }
        const std::string& tags = c[2];
        auto has = [&](const char* t) { return tags.find(t) != std::string::npos; };
        Topo ta = analyze(A);
        auto expectTag = [&](const char* t, bool actual) { if (has(t)) { tagChecks++; if (!actual) { printf("%s: tag '%s' is not true of mesh A\n", c[0].c_str(), t); bad++; } } };
        expectTag("open", ta.anyOpen); expectTag("non-manifold-edge", ta.nonManifold); expectTag("inverted", ta.inverted);
        expectTag("unwelded-vertices", ta.unwelded); expectTag("zero-area-triangle", ta.zeroArea); expectTag("duplicate-triangle", ta.duplicateTri);
        if (has("valid")) { tagChecks++; Topo tb = analyze(B); if (ta.anyOpen || ta.nonManifold || ta.inverted || ta.zeroArea || ta.duplicateTri || tb.anyOpen || tb.nonManifold || tb.inverted || tb.zeroArea || tb.duplicateTri) { printf("%s: tagged valid but a mesh is open, non-manifold, inverted, degenerate or duplicated\n", c[0].c_str()); bad++; } }
        if (c[1].empty() || tags.empty()) { printf("%s: missing class or tag\n", c[0].c_str()); bad++; }
    }
    // the primitives must be closed, consistently oriented and have positive exact volume
    for (const char* n : {"box", "icosphere_0", "icosphere_3", "icosphere_5", "star_2", "torus_8x4", "torus_32x16"}) {
        Mesh m; if (!readMesh(dir + "/primitives/" + n + ".msh", m)) { printf("primitive %s: cannot load\n", n); bad++; continue; }
        Topo t = analyze(m);
        if (t.anyOpen || t.nonManifold || t.inverted || signExact(exactVolumeTimes6(m)) <= 0) { printf("primitive %s: not a closed outward-oriented manifold (open %d nonmanifold %d inverted %d)\n", n, t.anyOpen, t.nonManifold, t.inverted); bad++; }
    }
    printf("corpus_check: %d cases, %d with exact expectations verified, %d tag checks, %d problems -> %s\n", cases, boxChecked, tagChecks, bad, bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
