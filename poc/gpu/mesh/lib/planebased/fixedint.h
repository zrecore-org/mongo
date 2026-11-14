// MP17 step 1: signed two's-complement fixed-width integer, N limbs of 32 bits (little-endian). 32-bit limbs on purpose: this is the word a GPU
// shader has (no 64-bit integer multiply-high in portable Vulkan/Metal), so limb multiply-add counts here carry over to a shader.
// Clean-room: written from the arithmetic only.
//
// Operations: add, sub, neg, mul (truncating to the width; schoolbook, i+j<N limb products only, N(N+1)/2 limb multiply-adds), mulMag
// (sign-magnitude multiply that skips leading zero limbs of both operands: same result as mul, fewer limb multiply-adds when operands are
// small), cmp, sign, shl, shr (arithmetic).
// Counting: g_fi.macs accumulates limb multiply-adds (one 32x32->64 multiply plus accumulate) in mul and mulMag.
// Debug mode (-DFIXEDINT_CHECK): every operation is recomputed with Boost cpp_int; the true (unwrapped) result must fit the width, else
// g_fi.overflows++; a wrong in-range result counts g_fi.mismatches; g_fi.maxbits is the largest true bit length of any result.
// Planted mutants: compile with -DMUTANT=n; MUT(n) selects them (same pattern as poc12_f64soft.cpp):
//   1 mul drops a carry   2 mul flips the sign of one limb product   3 shl off by one   4 add drops a carry   5 shr logical instead of arithmetic
//   6 cmp compares the top limb unsigned   7 vertex sign flipped (predicates.h)   8 cofactor sign wrong (predicates.h)
//   9 sub loses a borrow   10 plane d sign wrong (predicates.h)
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#ifndef MUTANT
#define MUTANT 0
#endif
#define MUT(n) (MUTANT == (n))

#if defined(FIXEDINT_CHECK) || defined(FIXEDINT_WITH_BIG)
#include <boost/multiprecision/cpp_int.hpp>
#endif

namespace pb {

struct FIStats {
    uint64_t macs = 0;        // limb multiply-adds
    uint64_t overflows = 0;   // FIXEDINT_CHECK: true result did not fit the width
    uint64_t mismatches = 0;  // FIXEDINT_CHECK: in-range result differs from cpp_int
    int maxbits = 0;          // FIXEDINT_CHECK: max bit length of |true result| over every operation
};
#ifdef FIXEDINT_THREADS
inline thread_local FIStats g_fi;
#else
inline FIStats g_fi;
#endif

template <int N> struct FixedInt {
    static_assert(N >= 2, "need at least 2 limbs");
    static constexpr int LIMBS = N;
    static constexpr int BITS = 32 * N;
    uint32_t w[N];

    FixedInt() { for (int i = 0; i < N; i++) w[i] = 0; }
    static FixedInt fromI64(int64_t v) {
        FixedInt r;
        uint32_t fill = v < 0 ? 0xFFFFFFFFu : 0u;
        r.w[0] = (uint32_t)(uint64_t)v;
        r.w[1] = (uint32_t)((uint64_t)v >> 32);
        for (int i = 2; i < N; i++) r.w[i] = fill;
        return r;
    }
    bool isNeg() const { return (w[N - 1] >> 31) != 0; }
    int sign() const {
        if (isNeg()) return -1;
        for (int i = 0; i < N; i++) if (w[i]) return 1;
        return 0;
    }
    bool isZero() const { for (int i = 0; i < N; i++) if (w[i]) return false; return true; }
};

#if defined(FIXEDINT_CHECK) || defined(FIXEDINT_WITH_BIG)
using Big = boost::multiprecision::cpp_int;
template <int N> Big toBig(const FixedInt<N>& a) {
    Big r = 0;
    for (int i = N - 1; i >= 0; i--) { r <<= 32; r |= Big(a.w[i]); }
    if (a.isNeg()) r -= (Big(1) << (32 * N));
    return r;
}
inline int bitlen(const Big& x) { return x == 0 ? 0 : (int)boost::multiprecision::msb(boost::multiprecision::abs(x)) + 1; }
inline Big floorShr(const Big& x, int n) {
    Big d = Big(1) << n, q = x / d, r = x % d;
    if (r < 0) q -= 1;
    return q;
}
// wraps x into the signed range of an N-limb integer
template <int N> Big wrapN(const Big& x) {
    Big M = Big(1) << (32 * N), r = x % M;
    if (r < 0) r += M;
    if (r >= (M >> 1)) r -= M;
    return r;
}
#endif

#ifdef FIXEDINT_CHECK
template <int N> void fiChk(const FixedInt<N>& r, const Big& expect) {
    int bl = bitlen(expect);
    if (bl > g_fi.maxbits) g_fi.maxbits = bl;
    Big lim = Big(1) << (32 * N - 1);
    if (expect >= lim || expect < -lim) g_fi.overflows++;
    else if (toBig(r) != expect) g_fi.mismatches++;
}
#define FI_CHECK(r, expr) fiChk(r, expr)
#else
#define FI_CHECK(r, expr) ((void)0)
#endif

template <int N> inline FixedInt<N> add(const FixedInt<N>& a, const FixedInt<N>& b) {
    FixedInt<N> r;
    uint64_t carry = 0;
    for (int i = 0; i < N; i++) {
        uint64_t t = (uint64_t)a.w[i] + b.w[i] + carry;
        r.w[i] = (uint32_t)t;
        carry = t >> 32;
        if (MUT(4) && i == 3) carry = 0;
    }
    FI_CHECK(r, toBig(a) + toBig(b));
    return r;
}

template <int N> inline FixedInt<N> sub(const FixedInt<N>& a, const FixedInt<N>& b) {
    FixedInt<N> r;
    uint64_t borrow = 0;
    for (int i = 0; i < N; i++) {
        uint64_t t = (uint64_t)a.w[i] - b.w[i] - borrow;
        r.w[i] = (uint32_t)t;
        borrow = (t >> 32) & 1;
        if (MUT(9) && i == 2) borrow = 0;
    }
    FI_CHECK(r, toBig(a) - toBig(b));
    return r;
}

template <int N> inline FixedInt<N> neg(const FixedInt<N>& a) {
    FixedInt<N> r;
    uint64_t carry = 1;
    for (int i = 0; i < N; i++) {
        uint64_t t = (uint64_t)(uint32_t)~a.w[i] + carry;
        r.w[i] = (uint32_t)t;
        carry = t >> 32;
    }
    FI_CHECK(r, -toBig(a));
    return r;
}

// Truncating multiply: the low N limbs of the product (correct for two's complement, since the product mod 2^(32N) does not depend on signs).
template <int N> inline FixedInt<N> mul(const FixedInt<N>& a, const FixedInt<N>& b) {
    FixedInt<N> r;
    for (int i = 0; i < N; i++) {
        uint32_t carry = 0;
        for (int j = 0; i + j < N; j++) {
            uint64_t p = (uint64_t)a.w[i] * b.w[j];
            if (MUT(2) && i == 1 && j == 2) p = (uint64_t)0 - p;
            uint64_t t = p + r.w[i + j] + carry;   // at most (2^32-1)^2 + 2(2^32-1) = 2^64 - 1
            r.w[i + j] = (uint32_t)t;
            carry = (uint32_t)(t >> 32);
            if (MUT(1) && i == 1 && j == 0) carry = 0;
        }
    }
    g_fi.macs += (uint64_t)N * (N + 1) / 2;
    FI_CHECK(r, toBig(a) * toBig(b));
    return r;
}

namespace detail {
template <int N> inline FixedInt<N> absRaw(const FixedInt<N>& a, bool* wasNeg) {
    *wasNeg = a.isNeg();
    if (!*wasNeg) return a;
    FixedInt<N> r;
    uint64_t carry = 1;
    for (int i = 0; i < N; i++) { uint64_t t = (uint64_t)(uint32_t)~a.w[i] + carry; r.w[i] = (uint32_t)t; carry = t >> 32; }
    return r;
}
template <int N> inline int usedLimbs(const FixedInt<N>& a) { int n = N; while (n > 0 && a.w[n - 1] == 0) n--; return n; }
}  // namespace detail

// Sign-magnitude multiply that skips leading zero limbs. Same truncated result as mul(); counts only the limb products it performs.
template <int N> inline FixedInt<N> mulMag(const FixedInt<N>& a, const FixedInt<N>& b) {
    bool na, nb;
    FixedInt<N> x = detail::absRaw(a, &na), y = detail::absRaw(b, &nb);
    int la = detail::usedLimbs(x), lb = detail::usedLimbs(y);
    FixedInt<N> r;
    uint64_t macs = 0;
    for (int i = 0; i < la; i++) {
        uint32_t carry = 0;
        int j = 0;
        for (; j < lb && i + j < N; j++) {
            uint64_t t = (uint64_t)x.w[i] * y.w[j] + r.w[i + j] + carry;
            r.w[i + j] = (uint32_t)t;
            carry = (uint32_t)(t >> 32);
            macs++;
        }
        if (i + lb < N) r.w[i + lb] = carry;   // that limb has not been written by an earlier row
    }
    g_fi.macs += macs;
    if (na != nb) {
        uint64_t carry = 1;
        for (int i = 0; i < N; i++) { uint64_t t = (uint64_t)(uint32_t)~r.w[i] + carry; r.w[i] = (uint32_t)t; carry = t >> 32; }
    }
    FI_CHECK(r, toBig(a) * toBig(b));
    return r;
}

// Signed comparison: -1, 0, +1.
template <int N> inline int cmp(const FixedInt<N>& a, const FixedInt<N>& b) {
    if (MUT(6)) {
        if (a.w[N - 1] != b.w[N - 1]) return a.w[N - 1] < b.w[N - 1] ? -1 : 1;
    } else {
        int32_t x = (int32_t)a.w[N - 1], y = (int32_t)b.w[N - 1];
        if (x != y) return x < y ? -1 : 1;
    }
    for (int i = N - 2; i >= 0; i--) if (a.w[i] != b.w[i]) return a.w[i] < b.w[i] ? -1 : 1;
    return 0;
}

// Shifts by n in [0, 32N). shl truncates; shr is arithmetic (rounds toward minus infinity).
template <int N> inline FixedInt<N> shl(const FixedInt<N>& a, int n) {
    if (MUT(3) && n >= 32) n -= 1;
    int limbs = n / 32, bits = n % 32;
    FixedInt<N> r;
    for (int i = N - 1; i >= 0; i--) {
        int s = i - limbs;
        uint32_t v = 0;
        if (s >= 0) {
            v = a.w[s] << bits;
            if (bits && s - 1 >= 0) v |= a.w[s - 1] >> (32 - bits);
        }
        r.w[i] = v;
    }
    FI_CHECK(r, toBig(a) << n);
    return r;
}
template <int N> inline FixedInt<N> shr(const FixedInt<N>& a, int n) {
    int limbs = n / 32, bits = n % 32;
    uint32_t fill = a.isNeg() ? 0xFFFFFFFFu : 0u;
    if (MUT(5)) fill = 0;
    FixedInt<N> r;
    for (int i = 0; i < N; i++) {
        int s = i + limbs;
        uint32_t v = s < N ? a.w[s] : fill;
        uint32_t hi = s + 1 < N ? a.w[s + 1] : fill;
        r.w[i] = bits ? (v >> bits) | (hi << (32 - bits)) : v;
    }
    FI_CHECK(r, floorShr(toBig(a), n));
    return r;
}

}  // namespace pb
