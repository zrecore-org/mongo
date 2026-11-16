// MP14a POC. The extension's own exact mesh-volume code (independent of oracle/exact.h by design: edge-cross-product formulation,
// bit-level binary64 decoding, per-triangle arbitrary-precision accumulation, correctly rounded conversion proven in the tests).
// Pack format (defined in this POC, identical to exactmesh's 'MSH1' record): little-endian u32 magic 'MSH1' (0x3148534d), u32 nv, u32 nt,
// nv*3 binary64 vertices, nt*3 u32 indices. Carried in a BSON BinData (subtype 0).
// Mutants for the bar-6 tests are selected at compile time: -DMESHVOL_MUTANT=1 plain double sum, 2 wrong sign, 3 drop last triangle.
#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "pack format is little-endian; add byte swaps for big-endian hosts");

namespace meshvol {
using BigInt = boost::multiprecision::cpp_int;

enum Err : int {
    kOk = 0, kMissingField = 1, kNotBinData = 2, kBadSubtype = 3, kBadHeader = 4, kSizeMismatch = 5, kIndexRange = 6, kNonFinite = 7,
    kOpenMesh = 8, kNonManifold = 9, kTooLarge = 10, kInternal = 11, kInterrupted = 12, kBadDocument = 13, kEmpty = 14, kBadPoints = 15,
};
inline const char* errName(int c) {
    static const char* n[] = {"ok", "missing field", "field is not BinData", "BinData subtype is not 0", "bad mesh header", "payload size does not match header",
                              "triangle index out of range", "non-finite coordinate", "open mesh (a directed edge has no reverse)",
                              "non-manifold or degenerate triangle (directed edge used twice or repeated vertex)", "mesh too large",
                              "internal error", "interrupted or past deadline", "malformed BSON document", "empty mesh", "bad points field"};
    return (c >= 0 && c <= 15) ? n[c] : "?";
}

constexpr uint32_t kMagic = 0x3148534d;
constexpr uint64_t kMaxTriangles = 1u << 24;

struct Result {
    int code = kOk;
    std::string exact;   // exact volume as a reduced rational "p/q" (or "p" when q == 1); V = 6V/6 so the denominator has a factor 3 and no finite decimal exists
    double approx = 0;   // correctly rounded (nearest, ties to even) binary64 of the exact value
    double rawDouble = 0;
};

// --- exact decoding of a binary64 into (signed 53-bit mantissa, exponent) with value = m * 2^e, by bit manipulation ---
inline void decode(double x, int64_t* m, int* e) {
    uint64_t u;
    std::memcpy(&u, &x, 8);
    const bool neg = u >> 63;
    const int be = (int)((u >> 52) & 0x7ff);
    uint64_t frac = u & ((1ull << 52) - 1);
    int64_t mant;
    int ex;
    if (be == 0) { mant = (int64_t)frac; ex = -1074; }
    else { mant = (int64_t)(frac | (1ull << 52)); ex = be - 1075; }
    *m = neg ? -mant : mant;
    *e = ex;
}

// Parses and validates a packed mesh. Returns kOk and fills v/t, or an error code.
struct Packed { std::vector<double> v; std::vector<uint32_t> t; };
inline int parsePacked(const uint8_t* p, size_t len, Packed* out) {
    if (len < 12) return kBadHeader;
    uint32_t h[3];
    std::memcpy(h, p, 12);
    if (h[0] != kMagic) return kBadHeader;
    const uint64_t nv = h[1], nt = h[2];
    if (nt == 0) return kEmpty;
    if (nt > kMaxTriangles || nv > kMaxTriangles * 3) return kTooLarge;
    const uint64_t want = 12 + nv * 24 + nt * 12;  // nv, nt < 2^26: no overflow in uint64
    if (want != (uint64_t)len) return kSizeMismatch;
    out->v.resize(nv * 3);
    out->t.resize(nt * 3);
    std::memcpy(out->v.data(), p + 12, nv * 24);
    std::memcpy(out->t.data(), p + 12 + nv * 24, nt * 12);
    for (double d : out->v) if (!std::isfinite(d)) return kNonFinite;
    for (uint32_t i : out->t) if (i >= nv) return kIndexRange;
    return kOk;
}

// Every directed edge must appear exactly once and its reverse exactly once (oriented closed 2-manifold edge condition).
inline int checkClosed(const Packed& m) {
    const size_t nt = m.t.size() / 3;
    std::vector<uint64_t> e;
    e.reserve(nt * 3);
    for (size_t k = 0; k < nt; k++) {
        const uint32_t a = m.t[3 * k], b = m.t[3 * k + 1], c = m.t[3 * k + 2];
        if (a == b || b == c || a == c) return kNonManifold;
        e.push_back((uint64_t)a << 32 | b); e.push_back((uint64_t)b << 32 | c); e.push_back((uint64_t)c << 32 | a);
    }
    std::sort(e.begin(), e.end());
    for (size_t i = 1; i < e.size(); i++) if (e[i] == e[i - 1]) return kNonManifold;
    for (uint64_t k : e) {
        const uint64_t rev = (k & 0xffffffffu) << 32 | (k >> 32);
        if (!std::binary_search(e.begin(), e.end(), rev)) return kOpenMesh;
    }
    return kOk;
}

inline size_t msb(const BigInt& x) { return boost::multiprecision::msb(x); }

// Round the positive rational p/q (q > 0) to the nearest binary64, ties to even, including subnormals and overflow to infinity.
inline double roundRational(BigInt p, const BigInt& q) {
    using boost::multiprecision::abs;
    if (p == 0) return 0.0;
    const bool neg = p < 0;
    BigInt a = abs(p), den = q;
    const long la = (long)msb(a), lq = (long)msb(den);
    long s = 64 - (la - lq);
    BigInt num = a;
    if (s >= 0) num <<= s; else den <<= -s;
    BigInt Q, rem;
    boost::multiprecision::divide_qr(num, den, Q, rem);
    const bool sticky = rem != 0;
    const long L = (long)msb(Q) + 1;     // bit length of Q
    const long E = L - 1 - s;            // exponent of the leading bit of the value
    long keep = std::min<long>(53, E + 1075);
    if (E > 1023) return neg ? -INFINITY : INFINITY;
    long drop = L - keep;
    BigInt M = drop >= 0 ? BigInt(Q >> drop) : BigInt(Q << -drop);
    if (drop > 0) {
        BigInt remBits = Q - (M << drop), half = BigInt(1) << (drop - 1);
        const bool up = remBits > half || (remBits == half && (sticky || (M & 1) != 0));
        if (up) M += 1;
    }
    const double d = std::ldexp((double)(uint64_t)M, (int)(drop - s));
    return neg ? -d : d;
}

inline std::string ratString(BigInt num, int exp2, int divisor) {
    using boost::multiprecision::gcd;
    BigInt p = num, q = divisor;
    if (exp2 >= 0) p <<= exp2; else q <<= -exp2;
    if (p == 0) return "0";
    const BigInt g = gcd(boost::multiprecision::abs(p), q);
    p /= g; q /= g;
    return q == 1 ? p.str() : p.str() + "/" + q.str();
}

inline double finalApprox(const BigInt& num, int exp2, int divisor) {
    BigInt p = num, q = divisor;
    if (exp2 >= 0) p <<= exp2; else q <<= -exp2;
    return roundRational(p, q);
}

// The exact volume of an already validated closed mesh: V = (1/6) sum_k a . ((b-a) x (c-a)) over triangles (a, b, c).
inline void exactVolume(const Packed& m, Result* r) {
    const size_t nv = m.v.size() / 3;
    std::vector<int64_t> mant(m.v.size());
    std::vector<int> ex(m.v.size());
    int emin = 1 << 20;
    for (size_t i = 0; i < m.v.size(); i++) {
        decode(m.v[i], &mant[i], &ex[i]);
        if (mant[i] != 0) emin = std::min(emin, ex[i]);
    }
    if (emin == 1 << 20) emin = 0;
    std::vector<BigInt> iv(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) { iv[i] = mant[i]; if (mant[i] != 0) iv[i] <<= (ex[i] - emin); }
    (void)nv;
    size_t nt = m.t.size() / 3;
#if defined(MESHVOL_MUTANT) && MESHVOL_MUTANT == 3
    if (nt > 0) nt -= 1;  // mutant: drops the last triangle
#endif
#if defined(MESHVOL_MUTANT) && MESHVOL_MUTANT == 1
    // mutant: plain binary64 accumulation, no exactness; the result is then pushed through the same formatting.
    double acc = 0;
    for (size_t k = 0; k < nt; k++) {
        const double *a = &m.v[3 * (size_t)m.t[3 * k]], *b = &m.v[3 * (size_t)m.t[3 * k + 1]], *c = &m.v[3 * (size_t)m.t[3 * k + 2]];
        const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        acc += a[0] * (uy * vz - uz * vy) + a[1] * (uz * vx - ux * vz) + a[2] * (ux * vy - uy * vx);
    }
    r->rawDouble = acc;
    int64_t mm; int ee; decode(acc, &mm, &ee);
    BigInt num = mm;
    r->exact = ratString(num, ee, 6);
    r->approx = std::isfinite(acc) ? finalApprox(num, ee, 6) : acc;
    return;
#else
    BigInt sum = 0;
    for (size_t k = 0; k < nt; k++) {
        const BigInt *a = &iv[3 * (size_t)m.t[3 * k]], *b = &iv[3 * (size_t)m.t[3 * k + 1]], *c = &iv[3 * (size_t)m.t[3 * k + 2]];
        const BigInt ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        sum += a[0] * (uy * vz - uz * vy) + a[1] * (uz * vx - ux * vz) + a[2] * (ux * vy - uy * vx);
    }
#if defined(MESHVOL_MUTANT) && MESHVOL_MUTANT == 2
    sum = -sum;  // mutant: wrong sign
#endif
    const int e3 = 3 * emin;
    r->exact = ratString(sum, e3, 6);
    r->approx = finalApprox(sum, e3, 6);
#endif
}

// Whole pipeline for one packed mesh.
inline Result volumeOfPacked(const uint8_t* p, size_t len) {
    Result r;
    Packed m;
    if ((r.code = parsePacked(p, len, &m)) != kOk) return r;
    if ((r.code = checkClosed(m)) != kOk) return r;
    exactVolume(m, &r);
    return r;
}
}  // namespace meshvol
