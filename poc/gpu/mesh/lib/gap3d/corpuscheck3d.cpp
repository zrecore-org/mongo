// Corpus self-check: every solid mesh in the corpus (and the speed meshes) must be closed, 2-manifold, outward (exact), without exactly-zero-area triangles.
#include "c3d.h"
#include "../../harness/topology.h"
#include <dirent.h>
using namespace c3d;
int main(int argc, char** argv) {
    int bad = 0, n = 0; DIR* d = opendir(argv[1]); struct dirent* e;
    while ((e = readdir(d))) { std::string f = e->d_name; if (f.size() < 5 || f.substr(f.size() - 4) != ".c3d") continue; Case c; read(std::string(argv[1]) + "/" + f, c);
        for (auto& kv : c.mesh) { mesh::Mesh m; for (auto& v : kv.second.v) for (int k = 0; k < 3; k++) m.v.push_back((double)v[k]); for (auto& t : kv.second.t) m.addT(t[0], t[1], t[2]);
            mesh::Topology t = mesh::analyzeTopology(m); n++; bool ok = t.closedManifold() && !t.inverted && !t.zeroArea && !t.duplicateTri && t.components == 1; if (!ok) { bad++; printf("BAD %s mesh %s closed=%d inverted=%d zero=%d comps=%d\n", f.c_str(), kv.first.c_str(), t.closedManifold(), t.inverted, t.zeroArea, t.components); } } }
    for (int i = 2; i < argc; i++) { mesh::Mesh m; if (!mesh::readMesh(argv[i], m)) { printf("cannot read %s\n", argv[i]); bad++; continue; } mesh::Topology t = mesh::analyzeTopology(m); n++; bool ok = t.closedManifold() && !t.inverted && !t.zeroArea && !t.duplicateTri; printf("%s: tris=%zu closed=%d inverted=%d zero=%d dup=%d comps=%d\n", argv[i], m.nt(), t.closedManifold(), t.inverted, t.zeroArea, t.duplicateTri, t.components); if (!ok) bad++; }
    printf("%d meshes checked, %d bad\n", n, bad); return bad ? 1 : 0; }
