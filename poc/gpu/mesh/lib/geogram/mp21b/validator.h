#pragma once
#include "../../../harness/exact.h"
#include <cstring>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
namespace mp21b {
using namespace mesh;

bool zeroAreaExact(const double* a, const double* b, const double* c) {
    // static filter: if some cross-product component is larger than a conservative rounding bound the area is certainly non-zero
    double ux = b[0]-a[0], uy = b[1]-a[1], uz = b[2]-a[2], wx = c[0]-a[0], wy = c[1]-a[1], wz = c[2]-a[2];
    double p1 = uy*wz, p2 = uz*wy, p3 = uz*wx, p4 = ux*wz, p5 = ux*wy, p6 = uy*wx;
    double s = std::fabs(p1)+std::fabs(p2)+std::fabs(p3)+std::fabs(p4)+std::fabs(p5)+std::fabs(p6);
    if (s > 1e-250 && s < 1e250) {
        double eb = 16 * 1.1102230246251565e-16 * s;
        if (std::fabs(p1-p2) > eb || std::fabs(p3-p4) > eb || std::fabs(p5-p6) > eb) return false;
    }
    BigInt v[9]; toCommonInts({a[0],a[1],a[2],b[0],b[1],b[2],c[0],c[1],c[2]}, v);
    BigInt Ux = v[3]-v[0], Uy = v[4]-v[1], Uz = v[5]-v[2], Wx = v[6]-v[0], Wy = v[7]-v[1], Wz = v[8]-v[2];
    return (Uy*Wz-Uz*Wy) == 0 && (Uz*Wx-Ux*Wz) == 0 && (Ux*Wy-Uy*Wx) == 0;
}

// every undirected edge exactly once in each direction. Returns a reason or "".
std::string edgeCheck(const std::vector<uint32_t>& id, size_t nt, const char* what) {
    std::unordered_map<uint64_t, std::pair<int,int>> e; e.reserve(nt * 2);
    for (size_t k = 0; k < nt; k++) for (int j = 0; j < 3; j++) {
        uint32_t p = id[3*k+j], q = id[3*k+(j+1)%3];
        auto& v = e[((uint64_t)std::min(p,q) << 32) | std::max(p,q)]; if (p < q) v.first++; else v.second++;
    }
    size_t open = 0, nonman = 0;
    for (auto& x : e) { int n = x.second.first + x.second.second; if (n == 1) open++; else if (n > 2 || x.second.first != 1) nonman++; }
#ifdef MUT_ONEBOUNDARY
    if (open == 1 && nonman == 0) return "";
#endif
    if (nonman) return std::string("non-manifold edge (") + what + ", " + std::to_string(nonman) + " edges shared by more than two triangles or with equal direction)";
    if (open) return std::string("open surface (") + what + ", " + std::to_string(open) + " boundary edges)";
    return "";
}

// static filter for the volume sign: double sum of the triple products with a conservative error bound; inconclusive -> exact path
bool volumeCertainlyPositive(const Mesh& m) {
    double sum = 0, abss = 0;
    for (size_t k = 0; k < m.nt(); k++) {
        const double *a = &m.v[3*m.t[3*k]], *b = &m.v[3*m.t[3*k+1]], *c = &m.v[3*m.t[3*k+2]];
        double t1 = b[1]*c[2], t2 = b[2]*c[1], t3 = b[2]*c[0], t4 = b[0]*c[2], t5 = b[0]*c[1], t6 = b[1]*c[0];
        double x = a[0]*(t1-t2), y = a[1]*(t3-t4), z = a[2]*(t5-t6);
        sum += x + y + z;
        abss += std::fabs(a[0])*(std::fabs(t1)+std::fabs(t2)) + std::fabs(a[1])*(std::fabs(t3)+std::fabs(t4)) + std::fabs(a[2])*(std::fabs(t5)+std::fabs(t6));
    }
    if (!(abss > 1e-250 && abss < 1e250)) return false;
    double eb = ((double)m.nt() * 4 + 64) * 1.1102230246251565e-16 * abss;
    return sum > eb;
}

struct H3 { size_t operator()(const std::array<uint32_t,3>& a) const { return ((size_t)a[0]*1000003u ^ a[1]) * 1000003u ^ a[2]; } };

std::string validate(const Mesh& m) {
    if (m.nt() == 0) return "empty mesh";
    for (double x : m.v) if (!std::isfinite(x)) return "non-finite coordinate";
    std::vector<uint32_t> idx(m.t.begin(), m.t.end());
    if (auto r = edgeCheck(idx, m.nt(), "as returned"); !r.empty()) return r;
    // weld by exactly equal coordinates (-0 == +0)
    struct K { double x, y, z; bool operator==(const K& o) const { return x == o.x && y == o.y && z == o.z; } };
    struct KH { size_t operator()(const K& k) const { uint64_t a, b, c; double x = k.x + 0.0, y = k.y + 0.0, z = k.z + 0.0; memcpy(&a, &x, 8); memcpy(&b, &y, 8); memcpy(&c, &z, 8); return (size_t)((a * 0x9E3779B97F4A7C15ull) ^ (b * 0xC2B2AE3D27D4EB4Full) ^ (c * 0x165667B19E3779F9ull)); } };
    std::unordered_map<K, uint32_t, KH> w; w.reserve(m.nv() * 2); std::vector<uint32_t> id(m.nv());
    for (size_t i = 0; i < m.nv(); i++) { auto it = w.emplace(K{m.v[3*i], m.v[3*i+1], m.v[3*i+2]}, (uint32_t)w.size()); id[i] = it.first->second; }
    std::vector<uint32_t> wt(m.t.size()); for (size_t i = 0; i < wt.size(); i++) wt[i] = id[m.t[i]];
    if (auto r = edgeCheck(wt, m.nt(), "after welding equal vertices"); !r.empty()) return r;
#ifndef MUT_NODUP
    { std::unordered_set<std::array<uint32_t,3>, H3> s; s.reserve(m.nt() * 2);
      for (size_t k = 0; k < m.nt(); k++) { std::array<uint32_t,3> a = {wt[3*k], wt[3*k+1], wt[3*k+2]}; std::sort(a.begin(), a.end()); if (!s.insert(a).second) return "duplicate or inverted-twin triangle"; } }
#endif
    for (size_t k = 0; k < m.nt(); k++) if (zeroAreaExact(&m.v[3*m.t[3*k]], &m.v[3*m.t[3*k+1]], &m.v[3*m.t[3*k+2]])) return "zero-area triangle (exact test)";
#ifndef MUT_NOORIENT
    if (!volumeCertainlyPositive(m) && signExact(exactVolumeTimes6(m)) <= 0) return "inverted orientation (exact volume not positive)";
#endif
    return "";
}
}  // namespace mp21b

