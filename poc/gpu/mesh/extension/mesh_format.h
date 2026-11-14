// POC mesh payload carried in a BSON BinData (subtype 0), little-endian, packed, no padding:
//   u32 nv | u32 nt | float64 xyz[nv*3] | u32 tri[nt*3]      (total 8 + 24*nv + 12*nt bytes)
// Triangles index vertices 0..nv-1; outward-facing counter-clockwise gives positive volume.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace meshfmt {

inline bool volume(const uint8_t* p, size_t n, double& out, std::string& err) {
    if (n < 8) { err = "mesh payload shorter than 8-byte header"; return false; }
    uint32_t nv, nt; memcpy(&nv, p, 4); memcpy(&nt, p + 4, 4);
    uint64_t need = 8ull + 24ull * nv + 12ull * nt;
    if (need != n) { err = "mesh payload size " + std::to_string(n) + " != expected " + std::to_string(need); return false; }
    const uint8_t* xyz = p + 8; const uint8_t* tri = xyz + 24ull * nv;
    auto vert = [&](uint32_t i, double* o) { memcpy(o, xyz + 24ull * i, 24); };
    double sum = 0;  // plain double: this POC is about packaging, not exactness
    for (uint32_t t = 0; t < nt; ++t) {
        uint32_t ix[3]; memcpy(ix, tri + 12ull * t, 12);
        if (ix[0] >= nv || ix[1] >= nv || ix[2] >= nv) { err = "triangle index out of range"; return false; }
        double a[3], b[3], c[3]; vert(ix[0], a); vert(ix[1], b); vert(ix[2], c);
        sum += a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
    }
    out = sum / 6.0;
    return true;
}

}  // namespace meshfmt
