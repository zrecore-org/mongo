// MP26: STL (binary or ASCII; also OBJ with only 'v' and 'f' lines) -> MSH1 with exact doubles (float32 -> double is exact), vertices welded by exactly equal coordinates.
//   stl_to_msh <in.stl|in.obj> <out.msh>      prints "nv nt degenerate_index_tris" on stdout; exit 0 ok, 2 unreadable.
#include "../../harness/mesh_io.h"
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
using namespace mesh;
struct Key { double x, y, z; bool operator==(const Key& o) const { return memcmp(this, &o, sizeof *this) == 0; } };
struct KH { size_t operator()(const Key& k) const { uint64_t b[3]; memcpy(b, &k, 24); uint64_t h = 1469598103934665603ull; for (auto v : b) { h ^= v; h *= 1099511628211ull; } return h; } };
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::ifstream f(argv[1], std::ios::binary); if (!f) return 2;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::array<double, 3>> tri;   // 3 points per triangle in file order
    bool bin = false;
    if (data.size() >= 84) { uint32_t n; memcpy(&n, data.data() + 80, 4); if (84 + (size_t)n * 50 == data.size()) { bin = true;
        for (uint32_t i = 0; i < n; i++) for (int v = 0; v < 3; v++) { float c[3]; memcpy(c, data.data() + 84 + (size_t)i * 50 + 12 + v * 12, 12); tri.push_back({c[0], c[1], c[2]}); } } }
    std::vector<std::array<double, 3>> objv; std::vector<uint32_t> objf; bool isObj = false;
    if (!bin) {
        std::istringstream ss(data); std::string line;
        while (std::getline(ss, line)) {
            std::istringstream ls(line); std::string w; ls >> w;
            if (w == "vertex") { float x, y, z; ls >> x >> y >> z; tri.push_back({x, y, z}); }       // ASCII STL: parsed as float32 (the format's precision) then widened exactly
            else if (w == "v") { double x, y, z; ls >> x >> y >> z; objv.push_back({x, y, z}); isObj = true; }
            else if (w == "f") { std::vector<uint32_t> idx; std::string t; while (ls >> t) idx.push_back((uint32_t)atoi(t.c_str()) - 1);
                for (size_t k = 1; k + 1 < idx.size(); k++) { objf.push_back(idx[0]); objf.push_back(idx[k]); objf.push_back(idx[k + 1]); } }
        }
    }
    Mesh m; std::unordered_map<Key, uint32_t, KH> w;
    auto add = [&](const std::array<double, 3>& p) { Key k{p[0] + 0.0, p[1] + 0.0, p[2] + 0.0}; auto it = w.find(k); if (it == w.end()) it = w.emplace(k, m.addV(k.x, k.y, k.z)).first; return it->second; };   // +0.0 folds -0 into +0
    if (isObj && tri.empty()) { std::vector<uint32_t> id; for (auto& p : objv) id.push_back(add(p)); for (uint32_t i : objf) { if (i >= id.size()) return 2; m.t.push_back(id[i]); } }
    else for (size_t i = 0; i + 2 < tri.size(); i += 3) { uint32_t a = add(tri[i]), b = add(tri[i + 1]), c = add(tri[i + 2]); m.addT(a, b, c); }
    size_t degen = 0; for (size_t k = 0; k < m.nt(); k++) if (m.t[3*k] == m.t[3*k+1] || m.t[3*k+1] == m.t[3*k+2] || m.t[3*k] == m.t[3*k+2]) degen++;
    if (m.nt() == 0) return 2;
    if (!writeMesh(argv[2], m)) return 2;
    printf("%zu %zu %zu\n", m.nv(), m.nt(), degen);
    return 0;
}
