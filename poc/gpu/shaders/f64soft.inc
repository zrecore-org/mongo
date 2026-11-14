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
