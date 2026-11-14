// MP08a runner: earcut.hpp v3.2.4. Triangulates each polygon (outer + holes) of set A separately. usage: run_earcut triangulate <case.p2d> <out.res> [reps]
#include "common2d.h"
#include <mapbox/earcut.hpp>
#include <array>
#include <chrono>
using namespace p2d; typedef std::vector<std::pair<double, double>> OR;
int main(int argc, char** argv) {
    if (argc < 4) return 2; std::string op = argv[1]; Case c; if (!readCase(argv[2], c)) return 3; int reps = argc > 4 ? atoi(argv[4]) : 1;
    if (op != "triangulate") { writeRes(argv[3], "REFUSED", "earcut only triangulates", {}, {}); return 0; }
    std::vector<double> ns; std::vector<OR> rings;
    try {
        for (int rep = 0; rep < reps; rep++) {
            std::vector<OR> out; auto t0 = std::chrono::steady_clock::now();
            for (auto& poly : c.sets[0]) {
                std::vector<std::vector<std::array<double, 2>>> pg; std::vector<std::array<double, 2>> flat;
                for (auto& ring : poly) { std::vector<std::array<double, 2>> r; for (auto& q : ring) r.push_back({(double)q.x, (double)q.y}); pg.push_back(r); for (auto& a : r) flat.push_back(a); }
                std::vector<uint32_t> idx = mapbox::earcut<uint32_t>(pg);
                for (size_t i = 0; i + 2 < idx.size(); i += 3) out.push_back(OR{{flat[idx[i]][0], flat[idx[i]][1]}, {flat[idx[i + 1]][0], flat[idx[i + 1]][1]}, {flat[idx[i + 2]][0], flat[idx[i + 2]][1]}});
            }
            auto t1 = std::chrono::steady_clock::now(); ns.push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            if (rep == reps - 1) rings = out;
        }
    } catch (const std::exception& e) { writeRes(argv[3], "REFUSED", std::string("exception: ") + e.what(), {}, {}); return 0; }
    writeRes(argv[3], "OK", "", ns, rings); return 0;
}
