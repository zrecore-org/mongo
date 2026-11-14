// Exact IEEE-754 binary64 addition in 64-bit integer arithmetic (round to nearest, ties to even), and MongoDB's
// DoubleDoubleSummation::addDouble built on it. One text, compiled as C++ (F64SOFT_C, checked on the host against the
// hardware double) and as GLSL (needs GL_EXT_shader_explicit_arithmetic_types_int64). No float instruction is used, so fast-math,
// FMA contraction and flush-to-zero cannot change a result.
//
// Written from the IEEE 754 semantics for this project. The idea of exact binary64 in integers (rather than float pairs) comes from the
// vulkan-gpu fork of pg-strom, as described in its plan (.claude/hw-acceleration.md, A10). Its source file kernels_vm/f64_soft.inc was NOT read, and no code was copied.
//
// Literals use the `ul` suffix in both languages (64-bit in C++ on LP64 targets: macOS and Linux).
// Domain: finite inputs, no subnormals, results that stay normal (the caller checks the exponent window; see softsum.comp).
// Zero of either sign is allowed. Every shift count is below 64 on purpose (a shift by >= the width is undefined in SPIR-V).
#define F64SOFT_INC_INCLUDED 1
#ifdef F64SOFT_C
#include <cstdint>
typedef unsigned uint;
extern int g_mut;   // mutation switch for the self-check: 0 = correct code
#define MUT(k) (g_mut == (k))
#define FN static inline
#define IO(T) T&
static inline int msb64(uint64_t x) { return 63 - __builtin_clzll(x); }   // x != 0
#else
#define MUT(k) false
#define FN
#define IO(T) inout T
int msb64(uint64_t x) { uvec2 h = unpackUint2x32(x); return h.y != 0u ? 32 + findMSB(h.y) : findMSB(h.x); }
#endif

FN uint64_t f64_add(uint64_t a, uint64_t b) {
    const uint64_t kMag = 0x7FFFFFFFFFFFFFFFul, kMant = 0x000FFFFFFFFFFFFFul, kHidden = 0x0010000000000000ul;
    if ((b & kMag) > (a & kMag)) { uint64_t t = a; a = b; b = t; }   // now |a| >= |b|
    uint sa = uint(a >> 63), sb = uint(b >> 63);
    int ea = int((a >> 52) & 0x7FFul), eb = int((b >> 52) & 0x7FFul);
    if (eb == 0) {                       // b is zero (subnormals are excluded)
        if (ea == 0) return uint64_t(sa & sb) << 63;   // zero + zero: -0 only if both are -0
        return a;
    }
    uint64_t fa = ((a & kMant) | kHidden) << 10;    // 53 bits at [62:10], ten guard bits below
    uint64_t fb = ((b & kMant) | kHidden) << 10;
    int d = ea - eb;                      // >= 0
    if (d > 0) {
        if (d >= 64) fb = 1ul;         // only the sticky bit survives
        else {
            uint64_t lost = fb & ((1ul << d) - 1ul);
            fb = (fb >> d) | (MUT(1) ? 0ul : (lost != 0ul ? 1ul : 0ul));   // MUT 1: drop the sticky bit
        }
    }
    uint64_t r = (sa == sb) ? fa + fb : fa - fb;
    if (r == 0ul) return 0ul;       // exact cancellation gives +0
    int p = msb64(r);                     // 62 normally, 63 after a carry, lower after cancellation
    int e = ea + (p - 62);
    if (p > 62) r = (r >> 1) | (r & 1ul);
    else if (p < 62) r = r << (62 - p);
    uint64_t m = r >> 10;
    uint rem = uint(r & 0x3FFul);
    bool up = MUT(2) ? (rem >= 0x200u)                                 // MUT 2: round half up
            : MUT(3) ? (rem > 0x200u)                                  // MUT 3: ties always round down
            : (rem > 0x200u || (rem == 0x200u && (m & 1ul) != 0ul));
    if (up) m += 1ul;
    if ((m >> 53) != 0ul) { m >>= 1; e += 1; }   // the mantissa rounded up to 2^53
    return (uint64_t(sa) << 63) | (uint64_t(e) << 52) | (m & kMant);
}
FN uint64_t f64_sub(uint64_t a, uint64_t b) { return f64_add(a, b ^ 0x8000000000000000ul); }

// Exact IEEE-754 binary64 multiplication, round to nearest even, same domain as f64_add (finite, no subnormal inputs, a normal or zero result; zero of either
// sign allowed). The 106-bit product of the two 53-bit significands is built from 32-bit limbs in uint64_t (no 128-bit type, so the same text compiles as GLSL).
// Every shift count is below 64. Mutants for the self-check: MUT(4) rounds half up, MUT(5) truncates, MUT(6) ignores the low 20 bits of the remainder
// (a "dropped sticky bits" bug: ties and near-ties are misjudged).
FN uint64_t f64_mul(uint64_t a, uint64_t b) {
    const uint64_t kMant = 0x000FFFFFFFFFFFFFul, kHidden = 0x0010000000000000ul;
    uint s = uint(a >> 63) ^ uint(b >> 63);
    int ea = int((a >> 52) & 0x7FFul), eb = int((b >> 52) & 0x7FFul);
    if (ea == 0 || eb == 0) return uint64_t(s) << 63;                  // a zero factor (subnormals are excluded)
    uint64_t ma = (a & kMant) | kHidden, mb = (b & kMant) | kHidden;   // 53 bits each, in [2^52, 2^53)
    uint64_t a0 = ma & 0xFFFFFFFFul, a1 = ma >> 32;                     // a1 < 2^21
    uint64_t b0 = mb & 0xFFFFFFFFul, b1 = mb >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = p01 + p10;                                           // < 2^54
    uint64_t lo = p00 + ((mid & 0xFFFFFFFFul) << 32);
    uint64_t hi = p11 + (mid >> 32) + (lo < p00 ? 1ul : 0ul);           // product = hi * 2^64 + lo, in [2^104, 2^106)
    int sh = (hi >= 0x20000000000ul) ? 53 : 52;                         // 2^105 = 2^41 * 2^64: keep a 53-bit significand
    uint64_t m = (hi << (64 - sh)) | (lo >> sh);
    uint64_t rem = lo & ((1ul << sh) - 1ul);
    if (MUT(6)) rem &= ~0xFFFFFul;
    uint64_t halfway = 1ul << (sh - 1);
    bool up = MUT(4) ? (rem >= halfway)
            : MUT(5) ? false
            : (rem > halfway || (rem == halfway && (m & 1ul) != 0ul));
    if (up) m += 1ul;
    int e = ea + eb - 1023 + (sh == 53 ? 1 : 0);
    if ((m >> 53) != 0ul) { m >>= 1; e += 1; }
    return (uint64_t(s) << 63) | (uint64_t(e) << 52) | (m & kMant);
}
FN uint64_t f64_abs(uint64_t a) { return a & 0x7FFFFFFFFFFFFFFFul; }
FN uint64_t f64_neg(uint64_t a) { return a ^ 0x8000000000000000ul; }
// a > b for finite, non-NaN operands (zeros of either sign are equal), by sign-magnitude comparison on the bit patterns
FN bool f64_gt(uint64_t a, uint64_t b) {
    uint64_t ma = a & 0x7FFFFFFFFFFFFFFFul, mb = b & 0x7FFFFFFFFFFFFFFFul;
    if (ma == 0ul && mb == 0ul) return false;
    bool na = (a >> 63) != 0ul, nb = (b >> 63) != 0ul;
    if (na != nb) return !na;
    return na ? (ma < mb) : (ma > mb);
}

// mongo::DoubleDoubleSummation::addDouble (src/mongo/util/summation.h), without the `_special` NaN tracker (the caller falls
// back to the CPU for non-finite input). sum and addend are binary64 bit patterns.
FN void ddsAdd(IO(uint64_t) sum, IO(uint64_t) addend, uint64_t x) {
    uint64_t s = f64_add(x, addend);          // _fast2Sum(x, _addend)
    uint64_t z = f64_sub(s, x);
    uint64_t t = f64_sub(addend, z);
    x = s; addend = t;
    s = f64_add(sum, x);                      // _2Sum(_sum, x)
    uint64_t ap = f64_sub(s, x), bp = f64_sub(s, ap), da = f64_sub(sum, ap), db = f64_sub(x, bp);
    sum = s; x = f64_add(da, db);
    addend = f64_add(addend, x);              // _addend += x
}
