// MP26: exact topology/validity summary of MSH1 files (reuses harness/topology.h). topo_info <file.msh>...   one line per file:
//   file nv nt closedManifold(welded) anyOpen nonManifold unwelded zeroArea duplicateTri inverted components euler volume6_sign
#include "../../harness/topology.h"
int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        mesh::Mesh m; if (!mesh::readMesh(argv[i], m)) { printf("%s\tunreadable\n", argv[i]); continue; }
        mesh::Topology t = mesh::analyzeTopology(m);
        printf("%s\t%zu\t%zu\tclosedManifold=%d\topen=%d\tnonManifold=%d\tunwelded=%d\tzeroArea=%d\tdupTri=%d\tinverted=%d\tcomponents=%d\teuler=%ld\n", argv[i], m.nv(), m.nt(), (int)t.closedManifold(), (int)t.anyOpen, (int)t.nonManifold, (int)t.unwelded, (int)t.zeroArea, (int)t.duplicateTri, (int)t.inverted, t.components, t.euler());
    }
}
