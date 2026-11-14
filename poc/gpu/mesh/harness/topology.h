// Combinatorial and exact-geometric properties of a triangle mesh, shared by corpus_check and the library scoring tool (oracle/compare.cpp).
#pragma once
#include "exact.h"
#include <array>
#include <map>
#include <set>

namespace mesh {
struct Topology {
    bool anyOpenIdx = false;     // as returned (by vertex index, no welding): some edge belongs to exactly one triangle
    bool nonManifoldIdx = false; // as returned (by index): some edge belongs to more than two triangles, or two with the same direction
    bool anyOpen = false;        // after welding vertices with exactly equal coordinates: some edge belongs to exactly one triangle
    bool nonManifold = false;    // some edge belongs to more than two triangles, or two triangles with the same direction
    bool duplicateTri = false;   // the same three vertex indices appear twice
    bool zeroArea = false;       // some triangle has exactly zero area (exact test)
    bool unwelded = false;       // two vertices have exactly equal coordinates
    bool inverted = false;       // negative exact volume
    size_t vertices = 0;         // distinct coordinates (welded by exact equality)
    size_t edges = 0;            // distinct undirected edges after welding
    size_t faces = 0;
    int components = 0;          // edge-connected components of triangles (after welding)
    long euler() const { return (long)vertices - (long)edges + (long)faces; }
    bool closedManifold() const { return !anyOpen && !nonManifold && faces > 0; }            // geometric: coincident vertices are identified
    bool closedManifoldAsReturned() const { return !anyOpenIdx && !nonManifoldIdx && faces > 0; }   // combinatorial: exactly the indices the library returned
};

inline Topology analyzeTopology(const Mesh& m) {
    Topology t;
    // weld by exact coordinates so that an "unwelded" mesh can still be judged by its geometry
    std::map<std::array<double, 3>, uint32_t> weld; std::vector<uint32_t> id(m.nv());
    for (size_t i = 0; i < m.nv(); i++) {
        std::array<double, 3> k = {m.v[3*i], m.v[3*i+1], m.v[3*i+2]};
        auto it = weld.find(k);
        if (it == weld.end()) { it = weld.emplace(k, (uint32_t)weld.size()).first; } else t.unwelded = true;
        id[i] = it->second;
    }
    t.vertices = weld.size(); t.faces = m.nt();
    std::map<std::pair<uint32_t, uint32_t>, std::pair<int, int>> edges;
    std::set<std::array<uint32_t, 3>> tris;
    std::vector<int> parent(m.nt()); for (size_t i = 0; i < parent.size(); i++) parent[i] = (int)i;
    auto find = [&](int x) { while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
    std::map<std::pair<uint32_t, uint32_t>, int> firstTri;
    for (size_t k = 0; k < m.nt(); k++) {
        uint32_t idx[3] = {id[m.t[3*k]], id[m.t[3*k+1]], id[m.t[3*k+2]]};
        for (int e = 0; e < 3; e++) {
            uint32_t p = idx[e], q = idx[(e + 1) % 3];
            auto key = std::make_pair(std::min(p, q), std::max(p, q));
            auto& v = edges[key]; if (p < q) v.first++; else v.second++;
            auto ft = firstTri.find(key); if (ft == firstTri.end()) firstTri[key] = (int)k; else parent[find((int)k)] = find(ft->second);
        }
        std::array<uint32_t, 3> s = {idx[0], idx[1], idx[2]}; std::sort(s.begin(), s.end()); if (!tris.insert(s).second) t.duplicateTri = true;
        const double *pa = &m.v[3*m.t[3*k]], *pb = &m.v[3*m.t[3*k+1]], *pc = &m.v[3*m.t[3*k+2]];
        BigInt v[9]; toCommonInts({pa[0], pa[1], pa[2], pb[0], pb[1], pb[2], pc[0], pc[1], pc[2]}, v);
        BigInt ux = v[3]-v[0], uy = v[4]-v[1], uz = v[5]-v[2], wx = v[6]-v[0], wy = v[7]-v[1], wz = v[8]-v[2];
        if ((uy*wz-uz*wy) == 0 && (uz*wx-ux*wz) == 0 && (ux*wy-uy*wx) == 0) t.zeroArea = true;
    }
    {   // the same test on the mesh exactly as returned: no welding by coordinates
        std::map<std::pair<uint32_t, uint32_t>, std::pair<int, int>> ie;
        for (size_t k = 0; k < m.nt(); k++) for (int e = 0; e < 3; e++) {
            uint32_t p = m.t[3*k+e], q = m.t[3*k+(e+1)%3]; auto key = std::make_pair(std::min(p, q), std::max(p, q));
            auto& v = ie[key]; if (p < q) v.first++; else v.second++;
        }
        for (auto& e : ie) { int n = e.second.first + e.second.second; if (n == 1) t.anyOpenIdx = true; if (n > 2 || (n == 2 && e.second.first != 1)) t.nonManifoldIdx = true; }
    }
    t.edges = edges.size();
    for (auto& e : edges) {
        int n = e.second.first + e.second.second;
        if (n == 1) t.anyOpen = true;
        if (n > 2 || (n == 2 && e.second.first != 1)) t.nonManifold = true;
    }
    std::set<int> roots; for (size_t k = 0; k < m.nt(); k++) roots.insert(find((int)k));
    t.components = (int)roots.size();
    t.inverted = m.nt() > 0 && signExact(exactVolumeTimes6(m)) < 0;
    return t;
}
}  // namespace mesh
