// For each mink / mink_nc case: is the origin inside or on the boundary of B (exact, harness/pip.h)? Used to test the claim that Manifold::MinkowskiSum equals A (+) B only when 0 is in B.   mink_origin <cases-dir>
#include "c3d.h"
#include "../../harness/pip.h"
#include <dirent.h>
int main(int, char** argv) {
    DIR* d = opendir(argv[1]); struct dirent* e;
    while ((e = readdir(d))) { std::string f = e->d_name; if (f.rfind("mink", 0) != 0 || f.size() < 5) continue; c3d::Case c; c3d::read(std::string(argv[1]) + "/" + f, c); if (!c.mesh.count("B")) continue;
        mesh::Mesh m; for (auto& v : c.mesh["B"].v) for (int k = 0; k < 3; k++) m.v.push_back((double)v[k]); for (auto& t : c.mesh["B"].t) m.addT(t[0], t[1], t[2]);
        double p[3] = {0, 0, 0}; mesh::Pip r = mesh::classifyPoint(m, p); printf("%s\t%d\n", c.name.c_str(), (r.inside || r.boundary) ? 1 : 0); }
    return 0; }
