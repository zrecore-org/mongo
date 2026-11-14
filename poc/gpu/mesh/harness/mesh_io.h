// Compact binary triangle-mesh format shared by every mesh POC.
//   file "MSH1": u32 magic 'MSH1', u32 nv, u32 nt, nv*3 float64 (little endian), nt*3 uint32 vertex indices
//   container "MSHC": u32 magic 'MSHC', u32 count, then count MSH1 records back to back
// Triangles are counter-clockwise seen from outside; a closed mesh with outward orientation has positive volume.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mesh {
struct Mesh {
    std::vector<double> v;      // xyz per vertex
    std::vector<uint32_t> t;    // 3 indices per triangle
    size_t nv() const { return v.size() / 3; }
    size_t nt() const { return t.size() / 3; }
    uint32_t addV(double x, double y, double z) { v.push_back(x); v.push_back(y); v.push_back(z); return (uint32_t)(nv() - 1); }
    void addT(uint32_t a, uint32_t b, uint32_t c) { t.push_back(a); t.push_back(b); t.push_back(c); }
};

constexpr uint32_t kMagicMesh = 0x3148534d;   // 'MSH1'
constexpr uint32_t kMagicCont = 0x4348534d;   // 'MSHC'

inline void writeRecord(FILE* f, const Mesh& m) {
    uint32_t h[3] = {kMagicMesh, (uint32_t)m.nv(), (uint32_t)m.nt()};
    fwrite(h, 4, 3, f);
    fwrite(m.v.data(), 8, m.v.size(), f);
    fwrite(m.t.data(), 4, m.t.size(), f);
}
inline bool readRecord(FILE* f, Mesh& m) {
    uint32_t h[3];
    if (fread(h, 4, 3, f) != 3 || h[0] != kMagicMesh) return false;
    m.v.resize((size_t)h[1] * 3); m.t.resize((size_t)h[2] * 3);
    if (fread(m.v.data(), 8, m.v.size(), f) != m.v.size()) return false;
    if (fread(m.t.data(), 4, m.t.size(), f) != m.t.size()) return false;
    for (uint32_t i : m.t) if (i >= h[1]) return false;   // indices validated on load
    return true;
}
inline bool writeMesh(const std::string& path, const Mesh& m) {
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return false;
    writeRecord(f, m); return fclose(f) == 0;
}
inline bool readMesh(const std::string& path, Mesh& m) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
    bool ok = readRecord(f, m); fclose(f); return ok;
}
}  // namespace mesh
