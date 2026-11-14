// MP08b comparison runner: ivanfratric/polypartition (MIT) at b000a4a2a72b46e1305fb6e95b080448d7c12049. mode hm -> ConvexPartition_HM, mono -> MonotonePartition.
// Same case/result format and the same oracle as proto.cpp. usage: run_polypartition <case.h2d> <result.res>
// -DMUT=1 drops the last part; -DMUT=2 returns the unpartitioned outer ring as the single part.
#ifndef MUT
#define MUT 0
#endif
#include "hard2d.h"
#include "polypartition.h"
using namespace h2d;
int main(int argc, char** argv) { if (argc < 3) return 2; Case c; if (!readCase(argv[1], c)) return 3; FILE* f = fopen(argv[2], "w"); if (!f) return 4;
    TPPLPartition pp; TPPLPolyList in, outp; for (size_t k = 0; k < c.rings.size(); k++) { TPPLPoly p; p.Init((long)c.rings[k].size()); for (size_t i = 0; i < c.rings[k].size(); i++) { p[(int)i].x = (double)c.rings[k][i].x; p[(int)i].y = (double)c.rings[k][i].y; }
        if (k == 0) { p.SetHole(false); p.SetOrientation(TPPL_ORIENTATION_CCW); } else { p.SetHole(true); p.SetOrientation(TPPL_ORIENTATION_CW); } in.push_back(p); }
    int ok = c.mode == "mono" ? pp.MonotonePartition(&in, &outp) : pp.ConvexPartition_HM(&in, &outp);
    if (!ok) { fprintf(f, "RES1 REFUSED failed\n"); fclose(f); return 0; }
    fprintf(f, "RES1 OK\n"); if (MUT == 1 && !outp.empty()) outp.pop_back(); if (MUT == 2) { outp.clear(); outp.push_back(in.front()); }
    for (auto& p : outp) { fprintf(f, "ring %ld", (long)p.GetNumPoints()); for (long i = 0; i < p.GetNumPoints(); i++) fprintf(f, " %lld %lld", (long long)p[(int)i].x, (long long)p[(int)i].y); fprintf(f, "\n"); } fclose(f); return 0; }
