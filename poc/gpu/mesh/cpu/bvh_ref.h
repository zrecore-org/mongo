// Deterministic CPU reference BVH and box-overlap pair finding (MP05). No external dependencies besides mesh_io.h.
//
// WHAT THIS COMPUTES: all pairs (i,j) of triangles of mesh A and mesh B whose binary64 axis-aligned bounding boxes overlap
// (closed boxes: touching counts). That is a CANDIDATE SET by box overlap, not exact triangle-triangle intersection.
// Box corners are the exact min/max of the binary64 vertex coordinates (no rounding is introduced, no inflation).
//
// Design choices (documented as required):
//  * BVH build: triangles are sorted by a 63-bit Morton code of the box centroid (21 bits per axis), ties broken by triangle index; the sorted array is
//    split at its midpoint (index (lo+hi)/2) recursively until a range has <= kLeaf triangles. A median split on a sorted order is simple, balanced
//    (depth ceil(log2(n/kLeaf))) and independent of floating-point comparisons beyond the quantisation below. It is not SAH-quality.
//  * The Morton quantisation takes the centroid (lo+hi)*0.5 of the box, maps it onto [0, 2^21-1] with one multiply by a scale computed from the scene
//    bounds, and truncates to an integer; everything after that (bit interleave, sort, split) is integer arithmetic. The floating-point steps are single
//    IEEE + - * / operations; build with -ffp-contract=off so results are identical across compilers/platforms. The Morton code only affects the TREE SHAPE
//    and therefore speed: the returned pair set depends only on the exact box overlap predicate (comparisons of binary64 values, which are exact).
//  * Determinism of output: pairs are sorted by (i,j). The multi-threaded variant splits A's triangles into fixed chunks of kChunk consecutive indices,
//    threads claim chunks dynamically, but every chunk's result is stored in its own slot and the slots are concatenated in chunk order, so the output is
//    identical for every thread count.
//  * NaN coordinates are not supported (comparisons with NaN are false, so such triangles never overlap anything).
#pragma once
#include "mesh_io.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace bvhref {

struct Box { double lo[3], hi[3]; };
struct Pair { uint32_t i, j; };
inline bool operator==(const Pair& a, const Pair& b) { return a.i == b.i && a.j == b.j; }
inline bool operator<(const Pair& a, const Pair& b) { return a.i != b.i ? a.i < b.i : a.j < b.j; }

inline bool overlap(const Box& a, const Box& b) {   // closed boxes
    return a.lo[0] <= b.hi[0] && b.lo[0] <= a.hi[0] && a.lo[1] <= b.hi[1] && b.lo[1] <= a.hi[1] && a.lo[2] <= b.hi[2] && b.lo[2] <= a.hi[2];
}

inline std::vector<Box> triangleBoxes(const mesh::Mesh& m) {
    std::vector<Box> out(m.nt());
    for (size_t t = 0; t < m.nt(); t++) {
        Box& b = out[t];
        for (int k = 0; k < 3; k++) { b.lo[k] = b.hi[k] = m.v[3 * (size_t)m.t[3 * t] + k]; }
        for (int c = 1; c < 3; c++) for (int k = 0; k < 3; k++) {
            double x = m.v[3 * (size_t)m.t[3 * t + c] + k];
            if (x < b.lo[k]) b.lo[k] = x;
            if (x > b.hi[k]) b.hi[k] = x;
        }
    }
    return out;
}

inline uint64_t spread21(uint64_t x) {   // 21 bits -> every third bit
    x &= 0x1fffff;
    x = (x | x << 32) & 0x1f00000000ffffULL;
    x = (x | x << 16) & 0x1f0000ff0000ffULL;
    x = (x | x << 8) & 0x100f00f00f00f00fULL;
    x = (x | x << 4) & 0x10c30c30c30c30c3ULL;
    x = (x | x << 2) & 0x1249249249249249ULL;
    return x;
}
inline uint64_t morton63(uint32_t x, uint32_t y, uint32_t z) { return spread21(x) | spread21(y) << 1 | spread21(z) << 2; }

constexpr int kLeaf = 4;
constexpr size_t kChunk = 256;

struct Bvh {
    struct Node { Box box; uint32_t left = 0, right = 0; uint32_t first = 0, count = 0; };   // leaf iff count > 0
    std::vector<Node> nodes;
    std::vector<uint32_t> order;       // triangle indices in Morton order
    const std::vector<Box>* boxes = nullptr;

    uint32_t buildRange(uint32_t lo, uint32_t hi) {
        uint32_t id = (uint32_t)nodes.size(); nodes.emplace_back();
        Box b = (*boxes)[order[lo]];
        for (uint32_t k = lo + 1; k < hi; k++) {
            const Box& o = (*boxes)[order[k]];
            for (int a = 0; a < 3; a++) { if (o.lo[a] < b.lo[a]) b.lo[a] = o.lo[a]; if (o.hi[a] > b.hi[a]) b.hi[a] = o.hi[a]; }
        }
        nodes[id].box = b;
        if (hi - lo <= (uint32_t)kLeaf) { nodes[id].first = lo; nodes[id].count = hi - lo; return id; }
        uint32_t mid = lo + (hi - lo) / 2;      // split the sorted array at its midpoint
        uint32_t l = buildRange(lo, mid), r = buildRange(mid, hi);
        nodes[id].left = l; nodes[id].right = r;
        return id;
    }
    void build(const std::vector<Box>& bx) {
        boxes = &bx; nodes.clear(); order.assign(bx.size(), 0);
        size_t n = bx.size(); if (!n) return;
        double lo[3], hi[3];
        for (int a = 0; a < 3; a++) { lo[a] = 0.5 * (bx[0].lo[a] + bx[0].hi[a]); hi[a] = lo[a]; }
        for (size_t i = 1; i < n; i++) for (int a = 0; a < 3; a++) {
            double c = 0.5 * (bx[i].lo[a] + bx[i].hi[a]);
            if (c < lo[a]) lo[a] = c; if (c > hi[a]) hi[a] = c;
        }
        double scale[3];
        for (int a = 0; a < 3; a++) { double e = hi[a] - lo[a]; scale[a] = e > 0 ? 2097151.0 / e : 0.0; }
        std::vector<std::pair<uint64_t, uint32_t>> keys(n);
        for (size_t i = 0; i < n; i++) {
            uint32_t q[3];
            for (int a = 0; a < 3; a++) {
                double c = 0.5 * (bx[i].lo[a] + bx[i].hi[a]);
                double s = (c - lo[a]) * scale[a];
                q[a] = s <= 0 ? 0u : (s >= 2097151.0 ? 2097151u : (uint32_t)s);
            }
            keys[i] = {morton63(q[0], q[1], q[2]), (uint32_t)i};
        }
        std::sort(keys.begin(), keys.end());     // pair comparison: ties broken by triangle index
        for (size_t i = 0; i < n; i++) order[i] = keys[i].second;
        nodes.reserve(2 * n / kLeaf * 2 + 4);
        buildRange(0, (uint32_t)n);
    }
    // appends all j with box(j) overlapping q, in ascending j order
    void query(const Box& q, std::vector<uint32_t>& out) const {
        size_t start = out.size();
        if (nodes.empty()) return;
        uint32_t stack[128]; int sp = 0; stack[sp++] = 0;
        while (sp) {
            const Node& nd = nodes[stack[--sp]];
            if (!overlap(q, nd.box)) continue;
            if (nd.count) { for (uint32_t k = nd.first; k < nd.first + nd.count; k++) { uint32_t j = order[k]; if (overlap(q, (*boxes)[j])) out.push_back(j); } }
            else { stack[sp++] = nd.right; stack[sp++] = nd.left; }
        }
        std::sort(out.begin() + start, out.end());
    }
};

// All pairs (i in A, j in B) with overlapping binary64 boxes, sorted by (i,j). Identical output for every thread count.
std::vector<Pair> overlapPairs(const std::vector<Box>& a, const std::vector<Box>& b, unsigned threads, double* buildSeconds = nullptr, double* querySeconds = nullptr);
std::vector<Pair> overlapPairs(const mesh::Mesh& meshA, const mesh::Mesh& meshB, unsigned threads = 1);
std::vector<Pair> bruteForcePairs(const std::vector<Box>& a, const std::vector<Box>& b);
std::vector<Pair> bruteForcePairs(const mesh::Mesh& meshA, const mesh::Mesh& meshB);
}  // namespace bvhref
