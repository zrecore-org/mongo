// POC 12 part 1: exact binary64 addition in integer arithmetic (shaders/f64soft.inc) checked on the host against the hardware double.
//   ./run.sh poc12_f64soft host      targeted + random pairs, then DoubleDoubleSummation sequences, then planted mutants (must be caught)
//   ./run.sh poc12_f64soft device    the same pairs through the Vulkan device (build/shaders/softadd_probe.spv)
#include "vkctx.h"
#include <cmath>
#include <random>
int g_mut = 0;
#define F64SOFT_C 1
#include "shaders/f64soft.inc"

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static double dbl(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

// Operand pairs aimed at the corners of the algorithm, plus uniform random bit patterns restricted to normal results.
static std::vector<std::pair<uint64_t, uint64_t>> makePairs(size_t perCase, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<std::pair<uint64_t, uint64_t>> v;
    auto mant = [&] { return rng() & 0x000FFFFFFFFFFFFFull; };
    auto mk = [&](uint64_t s, int e, uint64_t m) { return (s << 63) | ((uint64_t)(e & 0x7FF) << 52) | m; };
    auto base = [&] { return 200 + (int)(rng() % 600); };     // exponent field, leaves ample room so results stay normal
    for (size_t i = 0; i < perCase; i++) {
        int e = base(); uint64_t s = rng() & 1;
        for (int dd : {0, 1, 2, 3, 10, 52, 53, 54, 55, 62, 63, 64, 65, 100, 1000})        // every alignment regime, incl. >= 64
            for (int opp = 0; opp < 2; opp++) {
                uint64_t a = mk(s, e, mant()), b = mk(opp ? 1 - s : s, e - dd, mant());
                v.push_back({a, b}); v.push_back({b, a});
            }
        // exact ties: operand b has the guard bit pattern 100..0 after alignment
        for (int dd : {1, 2, 11, 53}) {
            uint64_t a = mk(s, e, mant() & ~1ull), b = mk(s, e - dd, 0);
            v.push_back({a, b}); v.push_back({mk(s, e, mant() | 1ull), b});
        }
        // mantissa round-up overflow: all-ones mantissa plus a small value
        v.push_back({mk(s, e, 0x000FFFFFFFFFFFFFull), mk(s, e - 53, mant())});
        v.push_back({mk(s, e, 0x000FFFFFFFFFFFFFull), mk(s, e - 1, 0x000FFFFFFFFFFFFFull)});
        // x + (-x), near-cancellation, and zeros of both signs
        uint64_t x = mk(s, e, mant());
        v.push_back({x, x ^ (1ull << 63)});
        v.push_back({x, (x ^ (1ull << 63)) + 1});     // one ulp apart: cancels to a power of two or less
        v.push_back({x, 0}); v.push_back({0, x}); v.push_back({x, 1ull << 63}); v.push_back({1ull << 63, x});
    }
    v.push_back({0, 0}); v.push_back({1ull << 63, 1ull << 63}); v.push_back({0, 1ull << 63}); v.push_back({1ull << 63, 0});
    for (size_t i = 0; i < perCase * 40; i++) v.push_back({mk(rng() & 1, base(), mant()), mk(rng() & 1, base(), mant())});   // uniform
    return v;
}

static size_t checkPairs(const std::vector<std::pair<uint64_t, uint64_t>>& v, size_t* firstBad = nullptr) {
    size_t bad = 0;
    for (size_t i = 0; i < v.size(); i++) {
        double h = dbl(v[i].first) + dbl(v[i].second);
        if (f64_add(v[i].first, v[i].second) != bits(h)) { if (!bad && firstBad) *firstBad = i; bad++; }
    }
    return bad;
}


// Operand pairs for the multiply: significands with few bits (exact products and exact ties), all ones, just above 1 and just below 2 (carry into the next exponent),
// random, with both signs and zeros of both signs; exponents chosen so that the product stays normal.
static std::vector<std::pair<uint64_t, uint64_t>> makeMulPairs(size_t perCase, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<std::pair<uint64_t, uint64_t>> v;
    auto mk = [&](uint64_t s, int e, uint64_t m) { return (s << 63) | ((uint64_t)(e & 0x7FF) << 52) | (m & 0x000FFFFFFFFFFFFFull); };
    auto ex = [&] { return 900 + (int)(rng() % 200); };             // biased exponent field near 1023: the product's biased exponent ea+eb-1023 stays in [777, 1177], always normal
    auto few = [&] { uint64_t m = 0; int bits = 1 + (int)(rng() % 10); for (int i = 0; i < bits; i++) m |= 1ull << (rng() % 52); return m; };
    for (size_t i = 0; i < perCase; i++) {
        uint64_t sa = rng() & 1, sb = rng() & 1;
        v.push_back({mk(sa, ex(), rng()), mk(sb, ex(), rng())});                                   // random
        v.push_back({mk(sa, ex(), few()), mk(sb, ex(), few())});                                   // few bits: exact products, many ties
        v.push_back({mk(sa, ex(), 0xFFFFFFFFFFFFFull), mk(sb, ex(), 0xFFFFFFFFFFFFFull)});         // all ones x all ones
        v.push_back({mk(sa, ex(), 0xFFFFFFFFFFFFFull), mk(sb, ex(), rng())});
        v.push_back({mk(sa, ex(), rng() & 0xFFull), mk(sb, ex(), rng() & 0xFFull)});               // just above 1
        v.push_back({mk(sa, ex(), 0x8000000000000ull | few()), mk(sb, ex(), 0x8000000000000ull | few())});   // near 1.5: product near 2.25, carry
        v.push_back({mk(sa, ex(), 0xFFFFFFFFFFFFFull - (rng() & 0xFF)), mk(sb, ex(), 0xFFFFFFFFFFFFFull - (rng() & 0xFF))});   // just below 2
        // ties: (1 + 2^-k)^2 = 1 + 2^(1-k) + 2^(-2k), exactly representable only for small k; the half-way case needs a 54-bit product with the last bit set
        int k = 20 + (int)(rng() % 7); uint64_t m = 1ull << (52 - k);
        v.push_back({mk(sa, ex(), m), mk(sb, ex(), m)});
        v.push_back({mk(sa, ex(), m | 1ull), mk(sb, ex(), m | 1ull)});
        v.push_back({mk(sa, ex(), 0), mk(sb, ex(), rng())});                                       // power of two times anything
    }
    for (int sa = 0; sa < 2; sa++) for (int sb = 0; sb < 2; sb++) { v.push_back({(uint64_t)sa << 63, mk(sb, 1023, 12345)}); v.push_back({mk(sb, 1023, 12345), (uint64_t)sa << 63}); v.push_back({(uint64_t)sa << 63, (uint64_t)sb << 63}); }
    return v;
}
static size_t checkMulPairs(const std::vector<std::pair<uint64_t, uint64_t>>& v, size_t* firstBad = nullptr) {
    size_t bad = 0;
    for (size_t i = 0; i < v.size(); i++) {
        double h = dbl(v[i].first) * dbl(v[i].second);
        if (f64_mul(v[i].first, v[i].second) != bits(h)) { if (!bad && firstBad) *firstBad = i; bad++; }
    }
    return bad;
}

// DoubleDoubleSummation sequences: the integer implementation against the hardware-double one used by POC 2.
struct DDh { double sum = 0, addend = 0;
    void add(double x) { { double s = x + addend, z = s - x, t = addend - z; x = s; addend = t; }
                         { double s = sum + x, ap = s - x, bp = s - ap, da = sum - ap, db = x - bp; sum = s; x = da + db; } addend += x; } };
static size_t checkDds(size_t seqs, size_t len) {
    std::mt19937_64 rng(5); std::normal_distribution<double> nd(0, 1); size_t bad = 0;
    for (size_t k = 0; k < seqs; k++) {
        DDh h; uint64_t s = 0, a = 0;
        for (size_t i = 0; i < len; i++) {
            double x = nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng));
            h.add(x); ddsAdd(s, a, bits(x));
        }
        bad += (s != bits(h.sum)) || (a != bits(h.addend));
    }
    return bad;
}

static int host() {
    auto pairs = makePairs(20000, 1);
    size_t fb = 0, bad = checkPairs(pairs, &fb);
    printf("host: f64_add vs hardware double on %zu pairs: %s (%zu differ)\n", pairs.size(), bad ? "FAIL" : "PASS", bad);
    if (bad) printf("  first mismatch: %016llx + %016llx -> %016llx, expected %016llx\n", (unsigned long long)pairs[fb].first, (unsigned long long)pairs[fb].second,
                    (unsigned long long)f64_add(pairs[fb].first, pairs[fb].second), (unsigned long long)bits(dbl(pairs[fb].first) + dbl(pairs[fb].second)));
    size_t dbad = checkDds(200, 20000);
    printf("host: DoubleDoubleSummation, integer vs hardware, 200 sequences x 20000 values (sum AND addend bit for bit): %s (%zu differ)\n", dbad ? "FAIL" : "PASS", dbad);
    int rc = bad || dbad;
    auto mpairs = makeMulPairs(120000, 21);
    size_t mfb = 0, mbad = checkMulPairs(mpairs, &mfb);
    printf("host: f64_mul vs hardware double on %zu pairs: %s (%zu differ)\n", mpairs.size(), mbad ? "FAIL" : "PASS", mbad);
    if (mbad) printf("  first mismatch: %016llx * %016llx -> %016llx, expected %016llx\n", (unsigned long long)mpairs[mfb].first, (unsigned long long)mpairs[mfb].second,
                     (unsigned long long)f64_mul(mpairs[mfb].first, mpairs[mfb].second), (unsigned long long)bits(dbl(mpairs[mfb].first) * dbl(mpairs[mfb].second)));
    rc |= mbad != 0;
    {
        auto fewm = makeMulPairs(60000, 33);
        const char* mn[] = {"", "", "", "", "round half up", "truncate", "remainder low 20 bits ignored"};
        for (int m = 4; m <= 6; m++) {
            g_mut = m; size_t mb = checkMulPairs(fewm); g_mut = 0;
            printf("  mutant %d (multiply: %s): %s (%zu of %zu pairs differ)\n", m, mn[m], mb ? "caught" : "NOT CAUGHT", mb, fewm.size());
            rc |= mb == 0;
        }
    }
    auto few = makePairs(2000, 9);
    const char* names[] = {"", "drop the sticky bit", "round half up", "ties always down"};
    for (int m = 1; m <= 3; m++) {
        g_mut = m; size_t mb = checkPairs(few); g_mut = 0;
        printf("  mutant %d (%s): %s (%zu of %zu pairs differ)\n", m, names[m], mb ? "caught" : "NOT CAUGHT", mb, few.size());
        rc |= mb == 0;
    }
    return rc;
}

static int device() {
    gpu::Ctx ctx;
    if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    auto addPairs = makePairs(8000, 3);
    auto mulPairs = makeMulPairs(20000, 4);
    printf("device=%s memory=%s add pairs=%zu mul pairs=%zu\n", ctx.props.deviceName, ctx.memMode(), addPairs.size(), mulPairs.size());
    auto k = ctx.makeKernel("build/shaders/softadd_probe.spv", 2, 8);
    int rc = 0;
    for (int op = 0; op < 2; op++) {
        const auto& pairs = op == 0 ? addPairs : mulPairs;
        const uint32_t n = (uint32_t)pairs.size();
        auto A = ctx.alloc((size_t)n * 16), R = ctx.alloc((size_t)n * 8);
        memcpy(A.ptr, pairs.data(), (size_t)n * 16);
        uint32_t pc[2] = {n, (uint32_t)op};
        ctx.begin(); ctx.upload(A); ctx.dispatch(k, {&A, &R}, pc, (n + 63) / 64); ctx.download(R); ctx.submitWait();
        size_t bad = 0, first = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t want = op == 0 ? bits(dbl(pairs[i].first) + dbl(pairs[i].second)) : bits(dbl(pairs[i].first) * dbl(pairs[i].second));
            if (((uint64_t*)R.ptr)[i] != want) { if (!bad) first = i; bad++; }
        }
        printf("device: f64_%s vs hardware double: %s (%zu of %u differ)\n", op == 0 ? "add" : "mul", bad ? "FAIL" : "PASS", bad, n);
        if (bad) { printf("  first: %016llx %c %016llx -> %016llx, expected %016llx\n", (unsigned long long)pairs[first].first, op == 0 ? '+' : '*', (unsigned long long)pairs[first].second,
                          (unsigned long long)((uint64_t*)R.ptr)[first], (unsigned long long)(op == 0 ? bits(dbl(pairs[first].first) + dbl(pairs[first].second)) : bits(dbl(pairs[first].first) * dbl(pairs[first].second)))); rc = 3; }
        ctx.release(A); ctx.release(R);
    }
    ctx.release(k); ctx.destroy();
    return rc;
}

int main(int argc, char** argv) {
    std::string m = argc > 1 ? argv[1] : "host";
    return m == "device" ? device() : host();
}
