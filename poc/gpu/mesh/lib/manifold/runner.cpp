// MP03: command-line runner around Manifold (MeshGL64, double precision). Contract in REPORT.md / task text:
//   manifold_runner boolean <union|intersection|difference> <A.msh> <B.msh> <out.msh>
//   manifold_runner measure <A.msh>
// exit 0 ok (empty result still writes a 0-triangle mesh), 3 library/inputs refused (stderr "REFUSED: <name>"), 4 error ("ERROR: <what>").
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include "../../harness/mesh_io.h"
#include "manifold/manifold.h"

#ifndef MANIFOLD_VERSION_STR
#define MANIFOLD_VERSION_STR "unknown"
#endif

using manifold::Manifold;
using manifold::MeshGL64;

static const char* statusName(Manifold::Error e) {
    switch (e) {
#define C(x) case Manifold::Error::x: return #x;
        C(NoError) C(NonFiniteVertex) C(NotManifold) C(VertexOutOfBounds) C(PropertiesWrongLength)
        C(MissingPositionProperties) C(MergeVectorsDifferentLengths) C(MergeIndexOutOfBounds)
        C(TransformWrongLength) C(RunIndexWrongLength) C(FaceIDWrongLength) C(InvalidConstruction)
        C(ResultTooLarge) C(InvalidTangents) C(Cancelled)
#undef C
    }
    return "Unknown";
}

struct Refused { std::string what; };

// Loads a mesh; throws Refused for non-finite coordinates / bad indices / bad MSH1 content of an existing file.
static Manifold load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    fclose(f);
    mesh::Mesh m;
    if (!mesh::readMesh(path, m)) throw Refused{"InvalidMeshFile"};
    for (double d : m.v) if (!std::isfinite(d)) throw Refused{"NonFiniteVertex"};
    MeshGL64 g;
    g.numProp = 3;
    g.vertProperties = m.v;
    g.triVerts.assign(m.t.begin(), m.t.end());
    Manifold M(g);
    if (M.Status() != Manifold::Error::NoError) throw Refused{statusName(M.Status())};
    return M;
}

static int run(int argc, char** argv) {
    fprintf(stderr, "manifold version %s (pinned commit 32271a1a999ce721ffaf5ab10e9746ceeb696547)\n", MANIFOLD_VERSION_STR);
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "boolean" && argc == 6) {
        std::string op = argv[2];
        manifold::OpType ot;
        if (op == "union") ot = manifold::OpType::Add;
        else if (op == "intersection") ot = manifold::OpType::Intersect;
        else if (op == "difference") ot = manifold::OpType::Subtract;
        else { fprintf(stderr, "ERROR: bad op %s\n", op.c_str()); return 4; }
        Manifold A = load(argv[3]);
        Manifold B = load(argv[4]);
        Manifold R = A.Boolean(B, ot);
        if (R.Status() != Manifold::Error::NoError) {
            // result status error (not an input refusal): reported as internal error, exit 4
            fprintf(stderr, "ERROR: result status %s\n", statusName(R.Status()));
            return 4;
        }
        MeshGL64 g = R.GetMeshGL64();
        mesh::Mesh out;
        size_t np = g.numProp;
        size_t nv = np ? g.vertProperties.size() / np : 0;
        out.v.reserve(nv * 3);
        for (size_t i = 0; i < nv; i++)
            for (int k = 0; k < 3; k++) out.v.push_back(g.vertProperties[i * np + k]);
        for (auto i : g.triVerts) out.t.push_back((uint32_t)i);
        if (!mesh::writeMesh(argv[5], out)) { fprintf(stderr, "ERROR: cannot write %s\n", argv[5]); return 4; }
        return 0;
    }
    if (cmd == "measure" && argc == 3) {
        Manifold A = load(argv[2]);
        printf("volume=%.17g area=%.17g genus=%d status=%s\n", A.Volume(), A.SurfaceArea(), A.Genus(), statusName(A.Status()));
        return 0;
    }
    fprintf(stderr, "ERROR: usage: manifold_runner boolean <union|intersection|difference> A B out | measure A\n");
    return 4;
}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const Refused& r) {
        fprintf(stderr, "REFUSED: %s\n", r.what.c_str());
        return 3;
    } catch (const std::exception& e) {
        fprintf(stderr, "ERROR: %s\n", e.what());
        return 4;
    } catch (...) {
        fprintf(stderr, "ERROR: unknown exception\n");
        return 4;
    }
}
